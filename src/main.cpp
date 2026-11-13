#include "receiver.hpp"
#include "sender.hpp"
#include "socket.hpp"
#include "units.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <print>
#include <string_view>
#include <variant>

namespace {
constexpr std::string_view USAGE = "usage: speedtest recv HOST PORT [OMITs]\n"
                                   "       speedtest send PORT BYTES\n"
                                   "       speedtest send PORT SECONDSs    (e.g. 10s)";

using SendAmount = std::variant<std::uint64_t, std::chrono::seconds>;

std::optional<std::chrono::seconds> parse_seconds(std::string_view text, std::uint64_t min,
                                                  std::uint64_t max)
{
    if (!text.ends_with('s')) {
        return std::nullopt;
    }

    text.remove_suffix(1);
    std::uint64_t secs = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), secs);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }

    if (secs < min || secs > max) {
        return std::nullopt;
    }

    return std::chrono::seconds{static_cast<std::chrono::seconds::rep>(secs)};
}

std::optional<SendAmount> parse_amount(std::string_view text)
{
    if (text.ends_with('s')) {
        return parse_seconds(text, 1, 3600);
    }

    std::uint64_t bytes = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), bytes);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }

    return bytes;
}

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
        if (argc != 4 && argc != 5) {
            print_usage();
            return 2;
        }

        const std::string host = argv[2];
        const std::string port = argv[3];

        std::chrono::seconds omit{0};
        if (argc == 5) {
            auto parsed_omit = parse_seconds(argv[4], 0, 60);
            if (!parsed_omit) {
                print_usage();
                return 2;
            }
            omit = *parsed_omit;
        }

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

        std::uint64_t kept_bytes = 0;
        std::chrono::duration<double> kept_time{0};

        auto print_interval = [&kept_bytes, &kept_time, &omit](const IntervalReport &report) {
            double rate = megabits_per_second(report.bytes, report.end - report.start);
            if (report.start < omit) {
                std::println("{:7.3f}-{:7.3f} s {:12} bytes {:10.2f} Mbit/s (omitted)",
                             report.start.count(), report.end.count(), report.bytes, rate);
            } else {
                std::println("{:7.3f}-{:7.3f} s {:12} bytes {:10.2f} Mbit/s", report.start.count(),
                             report.end.count(), report.bytes, rate);
                kept_bytes += report.bytes;
                kept_time += report.end - report.start;
            }
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
        if (omit.count() > 0) {
            if (kept_time.count() > 0) {
                std::println("after omitting {} s:  {}", omit.count(),
                             format_transfer(kept_bytes, kept_time));
            } else {
                std::println("after omitting {} s: nothing left to measure", omit.count());
            }
        }
        return 0;
    }

    if (mode == "send") {
        if (argc != 4) {
            print_usage();
            return 2;
        }

        const std::string port = argv[2];

        auto amount = parse_amount(argv[3]);
        if (!amount) {
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
        const auto *secs = std::get_if<std::chrono::seconds>(&*amount);
        auto send_res = secs ? send_for(accepted->socket, *secs)
                             : send_bytes(accepted->socket, std::get<std::uint64_t>(*amount));
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
