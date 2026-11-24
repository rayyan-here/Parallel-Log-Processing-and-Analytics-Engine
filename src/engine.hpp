#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace logs {
using Count = std::uint64_t;
namespace fs = std::filesystem;
inline constexpr Count repeat_threshold = 3;
inline constexpr std::array<std::string_view, 4> labels{"INFO", "WARNING", "ERROR", "OTHER"};

struct Counts {
    Count lines{};
    std::array<Count, 4> severity{};
    Count failed{}, unattributed{}, patterns{}; // patterns counts flagged identities.
    bool has_unattributed_alert() const { return unattributed >= repeat_threshold; }
    Count total_alerts() const { return patterns + has_unattributed_alert(); }
    bool operator==(const Counts&) const = default;
};
using Identities = std::map<std::string, Count>;
struct Totals {
    Counts counts;
    Identities identities;
    bool operator==(const Totals&) const = default;
};
struct Segment {
    Count begin{}, end{};       // byte offsets, half open
    Count first{}, limit{};     // zero-based line indices, half open
    Count size() const { return limit - first; }
};
struct Plan {
    fs::path path;
    Count bytes{}, lines{};
    fs::file_time_type modified;
    std::vector<Segment> segments;
    double indexing_ms{};
};
struct Run {
    Totals totals;
    std::vector<Counts> workers;
    int peak_readers{};
    double milliseconds{};
};
struct Comparison {
    Plan plan;
    int readers{};
    Run single, multi;
};

// Invoked on the coordinator thread. Workers never call UI code.
using Progress = std::function<void(const Counts&)>;
enum class PassOrder { single_first, parallel_first };
Plan plan_file(const fs::path& path, unsigned workers);
Run run_pass(const Plan& plan, const std::vector<Segment>& segments, int readers,
             const Progress& progress = {});
Comparison compare(const Plan& plan, int readers,
                   const Progress& single_progress = {}, const Progress& multi_progress = {},
                   PassOrder order = PassOrder::single_first);
std::string csv_report(const Comparison& comparison);
void save_report(const fs::path& destination, const fs::path& input, std::string_view csv);
} // namespace logs
