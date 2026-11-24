#include "engine.hpp"
#include <cerrno>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace logs {
namespace {
std::string quote(std::string_view s) {
    std::string field = "\"";
    for (char c : s) {
        if (c == '"') field += '"';
        field += c;
    }
    return field + '"';
}
std::string decimal(double value, int precision = 6) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << value;
    return os.str();
}
void fail(const char* what) { throw std::system_error(errno, std::generic_category(), what); }
} // namespace

std::string csv_report(const Comparison& c) {
    std::ostringstream out;
    out << "section,item,value,unit\n";
    auto row = [&](std::string_view section, std::string_view item, const std::string& value, std::string_view unit = "") {
        out << quote(section) << ',' << quote(item) << ',' << quote(value) << ',' << quote(unit) << '\n';
    };
    const auto& totals = c.multi.totals;
    const auto& counts = totals.counts;
    row("input", "file", c.plan.path.string());
    row("input", "size", std::to_string(c.plan.bytes), "bytes");
    row("summary", "processed", std::to_string(counts.lines), "lines");
    for (std::size_t i = 0; i < labels.size(); ++i)
        row("severity", labels[i], std::to_string(counts.severity[i]), "lines");
    row("security", "failed_login", std::to_string(counts.failed), "lines");
    row("security", "unattributed", std::to_string(counts.unattributed), "lines");
    row("security", "repeat_threshold", std::to_string(repeat_threshold), "failures per identity");
    row("security", "suspicious_patterns", std::to_string(counts.patterns), "distinct identities");
    row("security", "repeated_unattributed", counts.has_unattributed_alert() ? "yes" : "no", "file-level alert");
    row("security", "detected_patterns", std::to_string(counts.total_alerts()), "alerts");
    for (const auto& [id, n] : totals.identities)
        if (n >= repeat_threshold) row("flagged_identity", id, std::to_string(n), "failures");
    row("execution", "workers", std::to_string(c.plan.segments.size()));
    row("execution", "reader_limit", std::to_string(c.readers));
    row("execution", "peak_parallel_readers", std::to_string(c.multi.peak_readers));
    row("benchmark", "shared_index", decimal(c.plan.indexing_ms), "ms");
    row("benchmark", "single", decimal(c.single.milliseconds), "ms");
    row("benchmark", "parallel", decimal(c.multi.milliseconds), "ms");
    row("benchmark", "speedup", decimal(c.single.milliseconds / c.multi.milliseconds), "x");
    row("validation", "counts_and_identities_match", "yes");
    for (std::size_t i = 0; i < c.plan.segments.size(); ++i) {
        const Segment& s = c.plan.segments[i];
        const Counts& w = c.multi.workers[i];
        auto section = "worker_" + std::to_string(i + 1);
        row(section, "byte_range", "[" + std::to_string(s.begin) + "," + std::to_string(s.end) + ")", "zero-based");
        row(section, "line_range", s.size() ? std::to_string(s.first + 1) + "-" + std::to_string(s.limit) : "empty", "one-based inclusive");
        row(section, "processed", std::to_string(w.lines), "lines");
        for (std::size_t j = 0; j < labels.size(); ++j)
            row(section, labels[j], std::to_string(w.severity[j]), "lines");
        row(section, "failed_login", std::to_string(w.failed), "lines");
    }
    return out.str();
}

void save_report(const fs::path& path, const fs::path& input, std::string_view data) {
    if (fs::exists(path) && fs::equivalent(path, input))
        throw std::runtime_error("Report destination is the input file. Choose another file.");
    // A temporary sibling makes the final rename atomic, including on a retry.
    auto pattern = (path.parent_path() / ".log-report-XXXXXX").string();
    int fd = ::mkstemp(pattern.data());
    if (fd < 0) fail("Cannot create report temporary file");
    try {
        std::size_t offset = 0;
        while (offset < data.size()) {
            ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) fail("Cannot write report");
            offset += static_cast<std::size_t>(written);
        }
        int status = ::close(fd);
        fd = -1;
        if (status != 0) fail("Cannot finish report");
        fs::rename(pattern, path);
    } catch (...) {
        if (fd >= 0) ::close(fd);
        ::unlink(pattern.c_str());
        throw;
    }
}
} // namespace logs
