#include "engine.hpp"
#include <charconv>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>

using namespace logs;
namespace {
const char* usage = "Usage: log_analyzer INPUT [--threads 2|3|4] [--readers 1..threads] [--report FILE.csv] [--events | --ui-events]\n";
struct Options {
    fs::path input;
    int threads = 4;
    std::optional<int> readers;
    std::optional<fs::path> report;
    bool events = false;
    bool ui_details = false;
};
int number(std::string_view text) {
    int n = 0;
    auto [pos, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (ec != std::errc{} || pos != text.data() + text.size())
        throw std::invalid_argument("Expected a whole number.");
    return n;
}
Options parse(int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument(usage);
    Options o;
    o.input = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string_view key = argv[i];
        if (key == "--events" || key == "--ui-events") {
            o.events = true;
            if (key == "--ui-events") o.ui_details = true;
            continue;
        }
        if (i + 1 == argc) throw std::invalid_argument("Missing value for " + std::string(key));
        if (key == "--threads") o.threads = number(argv[++i]);
        else if (key == "--readers") o.readers = number(argv[++i]);
        else if (key == "--report") o.report = argv[++i];
        else throw std::invalid_argument("Unknown option: " + std::string(key));
    }
    if (o.threads < 2 || o.threads > 4) throw std::invalid_argument("--threads must be 2, 3, or 4.");
    if (o.readers && (*o.readers < 1 || *o.readers > o.threads))
        throw std::invalid_argument("--readers must be between 1 and the worker count.");
    return o;
}
void progress(std::string_view phase, const Counts& c) {
    std::cout << "PROGRESS\t" << phase << '\t' << c.lines;
    for (auto value : c.severity) std::cout << '\t' << value;
    std::cout << '\t' << c.failed << '\t' << c.unattributed << '\t' << c.patterns << std::endl;
}
void print_ui_details(const Comparison& result) {
    for (std::size_t i = 0; i < result.multi.workers.size(); ++i)
        std::cout << "WORKER\t" << i + 1 << '\t' << result.multi.workers[i].failed << '\n';
    // Keep the window responsive even when the file contains many flagged identities.
    // The CSV always contains every qualifying identity.
    unsigned shown = 0;
    for (const auto& [key, count] : result.multi.totals.identities) {
        if (count < repeat_threshold) continue;
        std::cout << "FLAGGED\t" << key << '\t' << count << '\n';
        if (++shown == 8) break;
    }
}
void summary(const Comparison& result) {
    const auto& c = result.multi.totals.counts;
    std::cout << "Processed " << c.lines << " lines (" << result.plan.bytes << " bytes)\n";
    for (std::size_t i = 0; i < labels.size(); ++i) std::cout << labels[i] << ": " << c.severity[i] << '\n';
    std::cout << "FAILED_LOGIN lines: " << c.failed << " (" << c.unattributed << " unattributed)\n"
              << "Suspicious identities: " << c.patterns << '\n'
              << "Repeated unattributed failures: " << (c.has_unattributed_alert() ? "yes" : "no") << '\n'
              << "Detected patterns: " << c.total_alerts() << '\n';
    for (const auto& [key, count] : result.multi.totals.identities)
        if (count >= repeat_threshold) std::cout << "  " << key << ": " << count << " failures\n";
    for (std::size_t i = 0; i < result.plan.segments.size(); ++i) {
        auto s = result.plan.segments[i];
        std::cout << "Worker " << i + 1 << " | bytes [" << s.begin << ", " << s.end << ") | "
                  << result.multi.workers[i].lines << " lines\n";
    }
    std::cout << "Reader permits: " << result.readers << " | Observed peak: " << result.multi.peak_readers << '\n'
              << std::fixed << std::setprecision(3) << "Shared indexing: " << result.plan.indexing_ms << " ms\n"
              << "Single: " << result.single.milliseconds << " ms | Parallel: "
              << result.multi.milliseconds << " ms | Speedup: " << result.single.milliseconds / result.multi.milliseconds
              << "x\nVerification: identical counts and identity frequencies\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") { std::cout << usage; return 0; }
        auto o = parse(argc, argv);
        auto plan = plan_file(o.input, static_cast<unsigned>(o.threads));
        int readers = o.readers.value_or(2);
        Progress baseline, parallel;
        if (o.events) {
            std::cout << "PLAN\t" << plan.lines << '\t' << plan.bytes << std::endl;
            if (o.ui_details)
                std::cout << std::fixed << std::setprecision(6) << "INDEX_MS\t" << plan.indexing_ms << std::endl;
            for (std::size_t i = 0; i < plan.segments.size(); ++i) {
                const auto& s = plan.segments[i];
                std::cout << "SEGMENT\t" << i + 1 << '\t' << s.begin << '\t' << s.end << '\t'
                          << s.first << '\t' << s.limit << std::endl;
            }
            baseline = [](const Counts& c) { progress("single", c); };
            parallel = [](const Counts& c) { progress("parallel", c); };
        }
        auto result = compare(plan, readers, baseline, parallel);
        if (o.report) save_report(*o.report, o.input, csv_report(result));
        if (o.events) {
            if (o.ui_details) print_ui_details(result);
            std::cout << std::fixed << std::setprecision(6) << "DONE\t" << result.single.milliseconds
                      << '\t' << result.multi.milliseconds << '\t' << result.multi.peak_readers << std::endl;
        } else {
            summary(result);
            if (o.report) std::cout << "CSV saved to " << o.report->string() << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
