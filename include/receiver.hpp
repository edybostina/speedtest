#pragma once

#include "socket.hpp"
#include "speedtest.hpp"

#include <cstdint>
#include <expected>
#include <string>

std::expected<std::uint64_t, std::string> receive(Socket &socket);
