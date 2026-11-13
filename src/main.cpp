#include "receiver.hpp"
#include "sender.hpp"
#include "socket.hpp"
#include "stats.hpp"
#include "units.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <print>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace {
constexpr std::string_view USAGE =
    "usage: speedtest recv HOST PORT [OMITs] [STREAMS]\n"
    "       speedtest send PORT BYTES [STREAMS]      (BYTES per stream)\n"
    "       speedtest send PORT SECONDSs [STREAMS]   (e.g. 10s)";

using SendAmount = std::variant<std::uint64_t, std::chrono::seconds>;

std::optional<std::size_t> parse_streams(std::string_view text)
{
    std::size_t streams = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), streams);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }

    if (streams < 1 || streams > 16) {
        return std::nullopt;
    }

    return streams;
}

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
        if (argc < 4 || argc > 6) {
            print_usage();
            return 2;
        }

        const std::string host = argv[2];
        const std::string port = argv[3];

        std::chrono::seconds omit{0};
        bool has_omit = false;
        std::size_t streams = 1;
        bool has_streams = false;

        for (int i = 4; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg.ends_with('s')) {
                if (has_omit) {
                    print_usage();
                    return 2;
                }
                auto parsed_omit = parse_seconds(arg, 0, 60);
                if (!parsed_omit) {
                    print_usage();
                    return 2;
                }
                omit = *parsed_omit;
                has_omit = true;
            } else {
                if (has_streams) {
                    print_usage();
                    return 2;
                }
                auto parsed_streams = parse_streams(arg);
                if (!parsed_streams) {
                    print_usage();
                    return 2;
                }
                streams = *parsed_streams;
                has_streams = true;
            }
        }

        std::vector<Socket> sockets;
        sockets.reserve(streams);
        for (std::size_t i = 0; i < streams; ++i) {
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
            sockets.push_back(std::move(*sock));
        }

        std::uint64_t kept_bytes = 0;
        std::chrono::duration<double> kept_time{0};
        std::vector<double> kept_rates;

        auto print_interval = [&kept_bytes, &kept_time, &kept_rates,
                               &omit](const IntervalReport &report) {
            double rate = megabits_per_second(report.bytes, report.end - report.start);
            const std::string_view suffix = report.start < omit ? " (omitted)" : "";
            std::println("{:7.3f}-{:7.3f} s {:12} bytes {:10.2f} Mbit/s{}", report.start.count(),
                         report.end.count(), report.bytes, rate, suffix);

            if (report.start >= omit) {
                kept_bytes += report.bytes;
                kept_time += report.end - report.start;
                if (report.end - report.start >= std::chrono::duration<double>{0.5}) {
                    kept_rates.push_back(rate);
                }
            }
        };

        print_build_note();
        auto result = receive(sockets, print_interval);
        if (!result) {
            std::println(stderr, "{}", result.error());
            return 1;
        }

        std::uint64_t bytes = result->bytes;
        std::chrono::duration<double> seconds = result->elapsed;

        std::println("{}", format_transfer(bytes, seconds));
        if (omit.count() > 0) {
            if (kept_time.count() > 0) {
                std::println("after omitting {} s: {}", omit.count(),
                             format_transfer(kept_bytes, kept_time));
            } else {
                std::println("after omitting {} s: nothing left to measure", omit.count());
            }
        }

        if (auto stats = summarize(kept_rates)) {
            std::println("per-second: {} samples, min {:.2f}, median {:.2f}, max {:.2f} Mbit/s",
                         stats->samples, stats->min, stats->median, stats->max);
        }
        return 0;
    }

    if (mode == "send") {
        if (argc != 4 && argc != 5) {
            print_usage();
            return 2;
        }

        const std::string port = argv[2];

        auto amount = parse_amount(argv[3]);
        if (!amount) {
            print_usage();
            return 2;
        }

        std::size_t streams = 1;
        if (argc == 5) {
            auto parsed_streams = parse_streams(argv[4]);
            if (!parsed_streams) {
                print_usage();
                return 2;
            }
            streams = *parsed_streams;
        }

        auto listener = listen_on(port);
        if (!listener) {
            std::println(stderr, "{}", listener.error());
            return 1;
        }

        std::println(stderr, "listening on port {}", port);

        std::vector<Accepted> accepted;
        accepted.reserve(streams);
        for (std::size_t i = 0; i < streams; ++i) {
            auto acc = accept_from(*listener);
            if (!acc) {
                std::println(stderr, "{}", acc.error());
                return 1;
            }
            std::println(stderr, "accepted connection from {}", acc->peer);
            accepted.push_back(std::move(*acc));
        }

        print_build_note();
        const auto *secs = std::get_if<std::chrono::seconds>(&*amount);
        std::vector<std::expected<SendResult, std::string>> results(streams);
        {
            std::vector<std::jthread> threads;
            threads.reserve(streams);
            for (std::size_t i = 0; i < streams; ++i) {
                threads.emplace_back([i, &accepted, &results, secs, &amount] {
                    results[i] =
                        secs ? send_for(accepted[i].socket, *secs)
                             : send_bytes(accepted[i].socket, std::get<std::uint64_t>(*amount));
                });
            }
        }

        bool has_error = false;
        for (const auto &res: results) {
            if (!res) {
                std::println(stderr, "{}", res.error());
                has_error = true;
            }
        }
        if (has_error) {
            return 1;
        }

        std::uint64_t total_sent = 0;
        std::chrono::steady_clock::duration max_last_send{};
        std::chrono::steady_clock::duration max_peer_done{};

        for (const auto &res: results) {
            total_sent += res->bytes;
            max_last_send = std::max(max_last_send, res->last_send);
            max_peer_done = std::max(max_peer_done, res->peer_done);
        }

        std::chrono::duration<double> last_send = max_last_send;
        std::chrono::duration<double> peer_done = max_peer_done;

        std::println("last send returned: {}", format_transfer(total_sent, last_send));
        std::println("receiver finished:  {}", format_transfer(total_sent, peer_done));
        return 0;
    }

    print_usage();
    return 2;
}
