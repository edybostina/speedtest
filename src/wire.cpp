#include "wire.hpp"

#include <algorithm>
#include <bit>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>

void put_u64(std::span<char, 8> out, std::uint64_t value)
{
    if (std::endian::native == std::endian::little) {
        value = std::byteswap(value);
    }
    auto arr = std::bit_cast<std::array<char, 8>>(value);
    std::ranges::copy(arr, out.begin());
}

std::uint64_t get_u64(std::span<const char, 8> in)
{
    std::array<char, 8> arr{};
    std::ranges::copy(in, arr.begin());
    auto value = std::bit_cast<std::uint64_t>(arr);
    if (std::endian::native == std::endian::little) {
        value = std::byteswap(value);
    }
    return value;
}
