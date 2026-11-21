#pragma once

#include <span>
#include <cstdint>

void put_u64(std::span<char, 8> out, std::uint64_t value);
std::uint64_t get_u64(std::span<const char, 8> in);
