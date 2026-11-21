#include "stats.hpp"
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

std::optional<RateStats> summarize(std::span<const double> rates)
{
    if (rates.empty()) {
        return std::nullopt;
    }

    std::vector<double> sorted_rates(rates.begin(), rates.end());

    std::ranges::sort(sorted_rates);

    double min = sorted_rates.front();
    double max = sorted_rates.back();
    double median =
        (sorted_rates.size() & 1) == 0
            ? (sorted_rates[sorted_rates.size() / 2 - 1] + sorted_rates[sorted_rates.size() / 2])
                  / 2
            : sorted_rates[sorted_rates.size() / 2];

    return RateStats{.samples = sorted_rates.size(), .min = min, .median = median, .max = max};
}

std::optional<double> jitter(std::span<const double> values)
{
    if (values.size() < 2) {
        return std::nullopt;
    }
    double mean = 0.0;

    for (size_t i = 1; i < values.size(); i++) {
        mean += std::abs(values[i] - values[i - 1]);
    }

    return mean / static_cast<double>(values.size() - 1);
}
