#!/bin/sh
set -eu
binary=$1
result=$(mktemp)
trap 'rm -f "$result"' EXIT
"$binary" data/sample.txt 3 2 > "$result"
awk '
  /^ *[1-6]  / {
    n++
    if ($1 != n || $2 != (n%2 ? "single" : "parallel") || $3 <= 0 || $4 <= 0 || $5 != "match") bad=1
    a[n]=$3; b[n]=$4
  }
  /^Median single:/ { single=$3; parallel=$8; speed=$NF+0; summary++ }
  END {
    for (i=1; i<=n; i++) for (j=i+1; j<=n; j++) {
      if (a[i]>a[j]) {t=a[i]; a[i]=a[j]; a[j]=t}
      if (b[i]>b[j]) {t=b[i]; b[i]=b[j]; b[j]=t}
    }
    ds=single-(a[3]+a[4])/2; dp=parallel-(b[3]+b[4])/2
    if (bad || n!=6 || summary!=1 || ds>0.0011 || ds< -0.0011 || dp>0.0011 || dp< -0.0011 || speed<=0) exit 1
  }
' "$result"
if "$binary" data/sample.txt 2 3 >/dev/null 2>&1; then exit 1; fi
if "$binary" data/sample.txt -2 1 >/dev/null 2>&1; then exit 1; fi
echo "PASS benchmark: six verified trials, alternating order, median times and range checks"
