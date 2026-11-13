#include "stats.hpp"
#include <algorithm>
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
