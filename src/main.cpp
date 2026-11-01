#include "receiver.hpp"
#include "sender.hpp"
#include "socket.hpp"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <print>
#include <string_view>

namespace {
constexpr std::string_view USAGE = "usage: speedtest recv HOST PORT\n"
                                   "       speedtest send PORT BYTES";

void print_usage()
{
    std::println(stderr, "{}", USAGE);
}
} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage();
        return 2;
    }

    const std::string_view mode = argv[1];

    if (mode == "recv") {
        if (argc != 4) {
            print_usage();
            return 2;
        }

        const std::string host = argv[2];
        const std::string port = argv[3];

        auto sock = connect_to(host, port);
        if (!sock) {
            std::println(stderr, "{}", sock.error());
            return 1;
        }

        if (host.find(':') != std::string::npos && !host.starts_with('[')) {
            std::println(stderr, "connected to [{}]:{}", host, port);
        } else {
            std::println(stderr, "connected to {}:{}", host, port);
        }

        auto result = receive(*sock);
        if (!result) {
            std::println(stderr, "{}", result.error());
            return 1;
        }

        std::println("{}", *result);
        return 0;
    }

    if (mode == "send") {
        if (argc != 4) {
            print_usage();
            return 2;
        }

        const std::string port = argv[2];

        std::uint64_t bytes = 0;
        const char *bytes_str = argv[3];
        const char *bytes_end = bytes_str + std::strlen(bytes_str);
        auto [ptr, ec] = std::from_chars(bytes_str, bytes_end, bytes);
        if (ec != std::errc{} || ptr != bytes_end) {
            print_usage();
            return 2;
        }

        auto listener = listen_on(port);
        if (!listener) {
            std::println(stderr, "{}", listener.error());
            return 1;
        }

        std::println(stderr, "listening on port {}", port);

        auto accepted = accept_from(*listener);
        if (!accepted) {
            std::println(stderr, "{}", accepted.error());
            return 1;
        }

        std::println(stderr, "accepted connection from {}", accepted->peer);

        auto send_res = send_bytes(accepted->socket, bytes);
        if (!send_res) {
            std::println(stderr, "{}", send_res.error());
            return 1;
        }

        std::println(stderr, "sent {} bytes", bytes);
        return 0;
    }

    print_usage();
    return 2;
}
