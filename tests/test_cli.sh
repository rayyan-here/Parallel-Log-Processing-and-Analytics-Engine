#!/bin/sh
set -eu
bin=$1
gen=$2
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
reject() { if "$@" >"$temp/error.txt" 2>&1; then fail "accepted unexpected command"; fi; }
"$gen" "$temp/large log.txt" 100003 >/dev/null
"$bin" "$temp/large log.txt" --threads 4 --readers 2 --report "$temp/result.csv" --events > "$temp/events"
awk -F '\t' '
  $1=="PLAN" {if ($2!=100003) exit 1; plan++}
  $1=="SEGMENT" {if ($2!=++segs || $3!=prevByte || $5!=prevLine) exit 1; prevByte=$4; prevLine=$6}
  $1=="PROGRESS" { if ($3!=($4+$5+$6+$7) || $3<last[$2]) exit 1; last[$2]=$3; if ($2=="parallel" && $3==100003 && $8==20001 && $10==2) ok=1 }
  $1=="DONE" {if ($2<=0 || $3<=0 || $4>2) exit 1; done++}
  END {if (!(plan==1 && segs==4 && prevLine==100003 && done==1 && ok && last["single"]==100003)) exit 1}
' "$temp/events" || fail "invalid live event stream"
grep -Fq '"severity","INFO","50000","lines"' "$temp/result.csv" || fail "CSV counts"
grep -Fq '"severity","ERROR","20002","lines"' "$temp/result.csv" || fail "CSV counts"
grep -Fq '"flagged_identity","user=admin","10001","failures"' "$temp/result.csv" || fail "CSV identity"
grep -Fq '"validation","counts_and_identities_match","yes",""' "$temp/result.csv" || fail "CSV verification"
"$bin" data/repeated-logins.txt --report "$temp/repeats.csv" > "$temp/repeats.txt"
grep -Fq 'Detected patterns: 2' "$temp/repeats.txt" || fail "pattern summary"
grep -Fq '"security","repeated_unattributed","yes","file-level alert"' "$temp/repeats.csv" || fail "anonymous alert"
grep -Fq '"security","detected_patterns","2","alerts"' "$temp/repeats.csv" || fail "alert total"
grep -Fq '"flagged_identity","user=lab user","3","failures"' "$temp/repeats.csv" || fail "quoted identity"
grep -Fq '"benchmark","shared_index",' "$temp/repeats.csv" || fail "shared indexing time"
# Quoted paths are valid CSV fields.
cp data/sample.txt "$temp/a,\"b.txt"
"$bin" "$temp/a,\"b.txt" --threads 2 --report "$temp/result.csv" >/dev/null
grep -Fq 'a,""b.txt"' "$temp/result.csv" || fail "CSV escaping"

"$gen" "$temp/empty.txt" 0 >/dev/null
"$bin" "$temp/empty.txt" --report "$temp/empty.csv" >/dev/null
grep -Fq '"summary","processed","0","lines"' "$temp/empty.csv" || fail "empty report"
reject "$bin" "$temp/missing" --report "$temp/no.csv"
test ! -e "$temp/no.csv" || fail "report from bad input"
reject "$bin" data/sample.txt --threads 5
reject "$bin" data/sample.txt --threads 2 --readers 3
reject "$bin" data/sample.txt --readers 0
reject "$bin" data/sample.txt --threads 3x
reject "$bin" data/sample.txt --report
reject "$bin" data/sample.txt --nonsense 1
reject "$bin" data/sample.txt --report "$temp/no-directory/x.csv"
reject "$gen" "$temp/nope" -3
cp data/sample.txt "$temp/input"
ln "$temp/input" "$temp/alias"
reject "$bin" "$temp/input" --report "$temp/alias"
cmp data/sample.txt "$temp/input" || fail "overwrote input"
test -z "$(find "$temp" -name '.log-report-*' -print)" || fail "unremoved temporary report"
# Two concurrent runs may publish to one destination. Either full report is valid.
"$bin" data/sample.txt --threads 2 --report "$temp/both.csv" >/dev/null & first=$!
"$bin" data/sample.txt --threads 4 --report "$temp/both.csv" >/dev/null & second=$!
wait "$first"; wait "$second"
grep -Fq '"summary","processed","24","lines"' "$temp/both.csv" || fail "concurrent export"
echo "PASS CLI: generator, event stream, CSV, options, file errors, safe replacement and simultaneous exports"
