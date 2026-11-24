#pragma once
#include "engine.hpp"

namespace logs {
// Classify one physical line. The batch carries counts and identity frequencies.
void classify(std::string_view line, Totals& batch);
void combine_counts(Counts& target, const Counts& part);
} // namespace logs
