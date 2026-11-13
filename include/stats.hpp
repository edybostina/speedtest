#pragma once

#include <cstddef>
#include <optional>
#include <span>

struct RateStats {
    std::size_t samples;
    double min;
    double median;
    double max;
};

std::optional<RateStats> summarize(std::span<const double> rates);
