#include "engine.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
constexpr std::size_t trial_count = 6;

int integer(std::string_view text) {
    int value{};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("The worker and reader counts must be integers.");
    return value;
}

double median(std::array<double, trial_count> values) {
    std::sort(values.begin(), values.end());
    return (values[trial_count / 2 - 1] + values[trial_count / 2]) / 2.0;
}
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: benchmark INPUT WORKERS READERS\n";
        return 1;
    }
    try {
        const int workers = integer(argv[2]);
        const int readers = integer(argv[3]);
        const auto plan = logs::plan_file(argv[1], workers);
        std::array<double, trial_count> single{}, multi{};
        std::cout << plan.path.string() << " | " << plan.lines << " lines | "
                  << workers << " workers | " << readers << " reader permits\n";
        std::cout << "Shared indexing (once): " << std::fixed << std::setprecision(3)
                  << plan.indexing_ms << " ms\n";
        std::cout << "Trial  First pass    Single (ms)  Parallel (ms)  Counts and identities\n"
                  << std::fixed << std::setprecision(3);
        for (std::size_t i = 0; i < trial_count; ++i) {
            const bool single_first = i % 2 == 0;
            auto result = logs::compare(plan, readers, {}, {}, single_first
                ? logs::PassOrder::single_first : logs::PassOrder::parallel_first);
            single[i] = result.single.milliseconds;
            multi[i] = result.multi.milliseconds;
            std::cout << std::setw(5) << i + 1 << "  " << std::left << std::setw(11)
                      << (single_first ? "single" : "parallel") << std::right << std::setw(12)
                      << single[i] << std::setw(15) << multi[i] << "  match\n";
        }
        const double single_median = median(single), multi_median = median(multi);
        std::cout << "Median single: " << single_median << " ms | Median parallel: "
                  << multi_median << " ms | Speedup (ratio of medians): "
                  << single_median / multi_median << "x\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
