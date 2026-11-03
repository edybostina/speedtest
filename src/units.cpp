#include "units.hpp"
#include <format>

double megabits_per_second(std::uint64_t bytes, std::chrono::duration<double> seconds)
{
    if (seconds.count() <= 0) {
        return 0.0;
    }

    double bits = static_cast<double>(bytes) * 8;

    return (bits / seconds.count()) / 1'000'000;
}

std::string format_transfer(std::uint64_t bytes, std::chrono::duration<double> seconds)
{
    return std::format("{} bytes in {:.3f} s = {:.2f} Mbit/s", bytes, seconds.count(),
                       megabits_per_second(bytes, seconds));
}
