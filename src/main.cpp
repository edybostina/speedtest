#include "receiver.hpp"
#include "sender.hpp"
#include "socket.hpp"
#include "units.hpp"

#include <charconv>
#include <chrono>
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

void print_build_note()
{
#ifdef SPEEDTEST_SANITIZED
    std::println(
        stderr, "note: ASan/UBSan build, rates are not representative (measure with build-bench/)");
#endif
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

        auto print_interval = [](const IntervalReport &report) {
            double rate = megabits_per_second(report.bytes, report.end - report.start);
            std::println("{:7.3f}-{:7.3f} s {:12} bytes {:10.2f} Mbit/s", report.start.count(),
                         report.end.count(), report.bytes, rate);
        };

        print_build_note();
        auto result = receive(*sock, print_interval);
        if (!result) {
            std::println(stderr, "{}", result.error());
            return 1;
        }

        std::uint64_t bytes = result->bytes;
        std::chrono::duration<double> seconds = result->elapsed;

        std::println("{}", format_transfer(bytes, seconds));
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

        print_build_note();
        auto send_res = send_bytes(accepted->socket, bytes);
        if (!send_res) {
            std::println(stderr, "{}", send_res.error());
            return 1;
        }

        std::uint64_t sent = send_res->bytes;
        std::chrono::duration<double> last_send = send_res->last_send;
        std::chrono::duration<double> peer_done = send_res->peer_done;

        std::println("last send returned: {}", format_transfer(sent, last_send));
        std::println("receiver finished:  {}", format_transfer(sent, peer_done));
        return 0;
    }

    print_usage();
    return 2;
}
