#pragma once

#include "socket.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

enum class Direction : char {
    download = 'd',
    upload = 'u',
};

struct TestRequest {
    Direction direction;
    std::uint8_t streams;
    std::chrono::seconds duration;
    bool operator==(const TestRequest &) const = default;
};

constexpr std::size_t REQUEST_SIZE = 8;

std::expected<void, std::string> send_request(Socket &sock, const TestRequest &request);
std::expected<TestRequest, std::string> read_request(Socket &sock);
