#pragma once

#include "socket.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

enum class Direction : char {
    download = 'd',
    upload = 'u',
    latency = 'l',
};

constexpr std::expected<Direction, std::string> parse_direction(char c)
{
    switch (static_cast<Direction>(c)) {
        case Direction::download:
        case Direction::upload:
        case Direction::latency:
            return static_cast<Direction>(c);
        default:
            return std::unexpected("bad direction");
    }
}

constexpr std::string_view direction_name(Direction d)
{
    switch (d) {
        case Direction::download:
            return "download";
        case Direction::upload:
            return "upload";
        case Direction::latency:
            return "latency";
    }
    std::unreachable();
}

struct TestRequest {
    Direction direction;
    std::uint8_t streams;
    std::chrono::seconds duration;
    bool operator==(const TestRequest &) const = default;
};

constexpr std::size_t REQUEST_SIZE = 8;

std::expected<void, std::string> send_request(Socket &sock, const TestRequest &request);
std::expected<TestRequest, std::string> read_request(Socket &sock);
