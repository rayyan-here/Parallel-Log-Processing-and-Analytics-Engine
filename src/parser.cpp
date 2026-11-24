#include "parser.hpp"
#include <utility>

namespace logs {
namespace {
char upper_ascii(char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; }
bool matches(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (upper_ascii(a[i]) != b[i]) return false;
    return true;
}
bool separator(char c) {
    switch (c) {
        case ' ': case '\t': case '\r': case '\v': case '\f':
        case '[': case ']': case '(': case ')': case ',': case ';': case '|': case '"':
            return true;
        default: return false;
    }
}

// Called immediately after user= or ip=. Quoted values may contain separators.
std::string read_identity(std::string_view line, std::size_t& pos) {
    if (pos == line.size()) return {};
    const char quote = line[pos];
    if (quote != '"' && quote != '\'') {
        const auto start = pos;
        while (pos < line.size() && !separator(line[pos])) ++pos;
        return std::string(line.substr(start, pos - start));
    }
    ++pos;
    std::string value;
    while (pos < line.size()) {
        char c = line[pos++];
        if (c == quote) return value;
        if (c == '\\' && pos < line.size() && (line[pos] == quote || line[pos] == '\\'))
            c = line[pos++];
        value += c;
    }
    return {}; // An incomplete field cannot identify a source.
}
} // namespace

void classify(std::string_view line, Totals& batch) {
    std::size_t severity = labels.size() - 1;
    bool failed = false;
    std::string user, ip;
    for (std::size_t pos = 0; pos < line.size();) {
        if (separator(line[pos])) { ++pos; continue; }
        const bool user_key = matches(line.substr(pos, 5), "USER=");
        if (user_key || matches(line.substr(pos, 3), "IP=")) {
            pos += user_key ? 5 : 3;
            auto value = read_identity(line, pos);
            auto& first = user_key ? user : ip;
            if (first.empty()) first = std::move(value);
            continue;
        }
        auto start = pos;
        while (pos < line.size() && !separator(line[pos])) ++pos;
        auto token = line.substr(start, pos - start);
        if (token.back() == ':') token.remove_suffix(1);
        if (matches(token, "FAILED_LOGIN")) failed = true;
        if (severity == 3) {
            for (std::size_t i = 0; i < 3; ++i) {
                if (matches(token, labels[i])) { severity = i; break; }
            }
        }
    }
    ++batch.counts.lines;
    ++batch.counts.severity[severity];
    if (!failed) return;
    ++batch.counts.failed;
    if (!user.empty()) ++batch.identities["user=" + user];
    else if (!ip.empty()) ++batch.identities["ip=" + ip];
    else ++batch.counts.unattributed;
}

void combine_counts(Counts& target, const Counts& part) {
    target.lines += part.lines;
    for (std::size_t i = 0; i < labels.size(); ++i) target.severity[i] += part.severity[i];
    target.failed += part.failed;
    target.unattributed += part.unattributed;
    // Patterns depend on global identity totals. They cannot be summed per worker.
}
} // namespace logs
