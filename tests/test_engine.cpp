#include "engine.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <unistd.h>

using namespace logs;
namespace {
void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
struct TempDir {
    fs::path path;
    TempDir() {
        std::string pattern = (fs::temp_directory_path() / "log-engine-test-XXXXXX").string();
        if (!::mkdtemp(pattern.data())) throw std::runtime_error("Cannot make test directory.");
        path = pattern;
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};
fs::path fixture(const fs::path& dir, const std::string& name, const std::string& content) {
    auto path = dir / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    check(bool(out), "fixture write");
    return path;
}

void verify(const fs::path& path, const Totals& expected, int workers, int readers) {
    auto p = plan_file(path, workers);
    check(p.segments.size() == static_cast<unsigned>(workers), "worker count");
    Count lines = 0, bytes = 0, active = 0;
    for (const auto& seg : p.segments) {
        check(seg.begin == bytes && seg.first == lines && seg.limit >= lines && seg.end >= bytes, "coverage gap or overlap");
        lines = seg.limit;
        bytes = seg.end;
        active += seg.size() != 0;
    }
    check(lines == expected.counts.lines && bytes == fs::file_size(path), "index coverage");
    check(active == std::min<Count>(workers, lines), "avoidable empty worker segment");
    Count previous = 0;
    auto watch = [&](const Counts& c) {
        check(c.lines >= previous && c.lines <= lines, "progress must be monotonic");
        check(std::accumulate(c.severity.begin(), c.severity.end(), Count{}) == c.lines, "inconsistent progress snapshot");
        previous = c.lines;
    };
    auto result = compare(p, readers, {}, watch);
    check(result.multi.totals == expected && result.single.totals == expected, "incorrect totals for " + path.string());
    check(previous == lines && result.multi.peak_readers <= readers, "reader limit or progress");
    check(result.multi.peak_readers >= (lines ? 1 : 0), "reader observation");
    for (std::size_t i = 0; i < p.segments.size(); ++i)
        check(result.multi.workers[i].lines == p.segments[i].size(), "worker segment count");
}
void modes(const fs::path& path, const Totals& expected) {
    for (int w = 2; w <= 4; ++w)
        for (int r = 1; r <= w; ++r) verify(path, expected, w, r);
}
} // namespace

int main() {
    try {
        TempDir temp;
        modes(fixture(temp.path, "empty.txt", ""), {});
        modes(fixture(temp.path, "blank.txt", "\n\n"), {{2, {0, 0, 0, 2}, 0, 0, 0}, {}});
        modes(fixture(temp.path, "one.txt", "INFO"), {{1, {1, 0, 0, 0}, 0, 0, 0}, {}});
        modes(fixture(temp.path, "unterminated.txt", "ERROR\r\n\r\n[warning]: tail"),
              {{3, {0, 1, 1, 1}, 0, 0, 0}, {}});

        std::string long_line(65535, 'z');
        modes(fixture(temp.path, "boundaries.txt",
            long_line + "\n" + std::string(170000, 'x') + " ERROR\ninfo\r\n"),
            {{3, {1, 0, 1, 1}, 0, 0, 0}, {}});

        for (const int long_at : {0, 5, 11}) {
            std::string content;
            for (int i = 0; i < 12; ++i) {
                content += i == long_at ? "INFO " + long_line : "INFO ok";
                if (i < 11) content += '\n';
            }
            auto path = fixture(temp.path, "skew.txt", content);
            modes(path, {{12, {12, 0, 0, 0}, 0, 0, 0}, {}});
            for (const auto& segment : plan_file(path, 4).segments)
                check(segment.size() == 3, "skew fallback should assign three lines to each worker");
        }

        auto mixed = fixture(temp.path, "security file.txt",
            "info: FAILED_LOGIN user=A ip=::1\r\n"
            "WARNING FAILED_LOGIN ip=::1\n"
            "ERROR FAILED_LOGIN user=A\n"
            "info normal user=info\n"
            "ERROR FAILED_LOGIN_ERROR user=A\n"
            "XERROR YINFO XFAILED_LOGIN\n"
            "WARNING FAILED_LOGIN FAILED_LOGIN user=A\n"
            "UNKNOWN failed_login IP=::1\n"
            "ERROR FAILED_LOGIN USER= ip=::1\n"
            "INFO FAILED_LOGIN user=A\n"
            "FAILED_LOGIN\n"
            "ERROR WARNING first severity wins\n"
            "ERROR FAILED_LOGIN user=a");
        modes(mixed, {{13, {3, 2, 5, 3}, 9, 1, 2},
                      {{"user=A", 4}, {"user=a", 1}, {"ip=::1", 3}}});
        modes("data/sample.txt", {{24, {10, 5, 7, 2}, 9, 1, 2},
                                  {{"user=ali", 3}, {"user=sara", 2}, {"ip=198.51.100.7", 3}}});
        modes("data/repeated-logins.txt", {{9, {3, 2, 4, 0}, 6, 3, 1}, {{"user=lab user", 3}}});
        auto quoted = fixture(temp.path, "quoted.txt",
            "INFO FAILED_LOGIN user=\"ali\" ip='::1'\n"
            "INFO FAILED_LOGIN user='ali'\n"
            "INFO FAILED_LOGIN USER=ali\n"
            "WARNING FAILED_LOGIN user='' ip=\"2001:db8::1\"\n"
            "WARNING FAILED_LOGIN user=\"A\\\" B\" user=second\n"
            "WARNING FAILED_LOGIN user=\"ERROR\tFAILED_LOGIN\"\n"
            "user=\"ERROR FAILED_LOGIN\" [INFO] heartbeat\n"
            "ERROR FAILED_LOGIN user=\"unterminated");
        modes(quoted, {{8, {4, 3, 1, 0}, 7, 1, 1},
              {{"user=ali", 3}, {"ip=2001:db8::1", 1}, {"user=A\" B", 1}, {"user=ERROR\tFAILED_LOGIN", 1}}});
        for (Count missing = 0; missing <= 5; ++missing) {
            std::string content;
            for (Count i = 0; i < missing; ++i) content += "ERROR FAILED_LOGIN\n";
            auto path = fixture(temp.path, "unattributed.txt", content);
            auto result = compare(plan_file(path, 4), 2);
            const auto& counts = result.multi.totals.counts;
            check(counts.unattributed == missing && counts.patterns == 0 &&
                  counts.total_alerts() == (missing >= 3 ? 1u : 0u), "anonymous group counted incorrectly");
        }

        std::string large;
        for (int i = 0; i < 12000; ++i)
            large += (i % 3 == 0 ? "ERROR FAILED_LOGIN user=shared\n" : "INFO idle\n");
        auto stress = fixture(temp.path, "stress.txt", large);
        for (int i = 0; i < 12; ++i)
            verify(stress, {{12000, {8000, 0, 4000, 0}, 4000, 0, 1}, {{"user=shared", 4000}}}, 4, 1 + i % 4);

        auto changed = fixture(temp.path, "changing.txt", "INFO before\n");
        auto plan = plan_file(changed, 4);
        std::ofstream(changed, std::ios::app) << "ERROR after\n";
        bool refused = false;
        try { (void)compare(plan, 2); } catch (const std::exception&) { refused = true; }
        check(refused, "changed input accepted");
        auto broken = plan_file(changed, 2);
        ++broken.segments.back().limit; // Force a read error while a permit is held.
        refused = false;
        try { (void)compare(broken, 1); } catch (const std::exception&) { refused = true; }
        check(refused, "truncated segment accepted");
        std::cout << "PASS engine: known totals, nine worker/permit combinations, boundary coverage, "
                     "skewed inputs, quoted identities, unattributed alerts, progress, stress and read-error cleanup\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
