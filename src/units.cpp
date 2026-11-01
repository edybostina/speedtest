#include "units.hpp"

double megabits_per_second(std::uint64_t bytes, std::chrono::duration<double> seconds)
{
    if (seconds.count() <= 0) {
        return 0.0;
    }

    double bits = static_cast<double>(bytes) * 8;

    return (bits / seconds.count()) / 1'000'000;
}
