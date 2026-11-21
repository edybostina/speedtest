#pragma once

#include "socket.hpp"

#include <chrono>
#include <optional>
#include <span>

void print_build_note();
int run_send(std::span<Socket> sockets, std::chrono::seconds duration,
             std::optional<std::chrono::seconds> omit);
int run_receive(std::span<Socket> sockets, std::chrono::seconds omit,
                std::optional<std::chrono::seconds> requested);
