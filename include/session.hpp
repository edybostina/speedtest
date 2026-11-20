#pragma once

#include "socket.hpp"

#include <chrono>
#include <span>

void print_build_note();
int run_send(std::span<Socket> sockets, std::chrono::seconds duration);
int run_receive(std::span<Socket> sockets, std::chrono::seconds omit);
