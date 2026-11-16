#include "receiver.hpp"
#include "report.hpp"
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
    "usage: speedtest recv HOST PORT [OMITs] [STREAMS] [BUFk|BUFm]\n"
    "       speedtest send PORT BYTES [STREAMS]      (BYTES per stream)\n"
    "       speedtest send PORT SECONDSs [STREAMS]   (e.g. 10s)";

using SendAmount = std::variant<std::uint64_t, std::chrono::seconds>;

std::optional<int> parse_size(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }

    char suffix = text.back();
    if (suffix != 'k' && suffix != 'm') {
        return std::nullopt;
    }

    std::uint64_t multiplier = (suffix == 'k') ? 1024ULL : (1024ULL * 1024ULL);
    text.remove_suffix(1);

    std::uint64_t num = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), num);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }

    constexpr std::uint64_t min_bytes = 4ULL * 1024ULL;
    constexpr std::uint64_t max_bytes = 8ULL * 1024ULL * 1024ULL;

    if (num > max_bytes / multiplier) {
        return std::nullopt;
    }

    std::uint64_t total = num * multiplier;
    if (total < min_bytes || total > max_bytes) {
        return std::nullopt;
    }

    return static_cast<int>(total);
}

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
        if (argc < 4 || argc > 7) {
            print_usage();
            return 2;
        }

        const std::string host = argv[2];
        const std::string port = argv[3];

        std::chrono::seconds omit{0};
        bool has_omit = false;
        std::size_t streams = 1;
        bool has_streams = false;
        int rcvbuf = 0;
        bool has_rcvbuf = false;

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
            } else if (arg.ends_with('k') || arg.ends_with('m')) {
                if (has_rcvbuf) {
                    print_usage();
                    return 2;
                }
                auto parsed_buf = parse_size(arg);
                if (!parsed_buf) {
                    print_usage();
                    return 2;
                }
                rcvbuf = *parsed_buf;
                has_rcvbuf = true;
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
        std::chrono::steady_clock::duration min_handshake =
            std::chrono::steady_clock::duration::max();

        for (std::size_t i = 0; i < streams; ++i) {
            auto conn = connect_to(host, port, rcvbuf);
            if (!conn) {
                std::println(stderr, "{}", conn.error());
                return 1;
            }

            min_handshake = std::min(min_handshake, conn->handshake);
            std::chrono::duration<double, std::milli> hs_ms = conn->handshake;

            if (host.find(':') != std::string::npos && !host.starts_with('[')) {
                std::println(stderr, "connected to [{}]:{} (handshake {:.1f} ms)", host, port,
                             hs_ms.count());
            } else {
                std::println(stderr, "connected to {}:{} (handshake {:.1f} ms)", host, port,
                             hs_ms.count());
            }
            sockets.push_back(std::move(conn->socket));
        }

        auto initial_buf = receive_buffer_size(sockets.front());
        if (!initial_buf) {
            std::println(stderr, "{}", initial_buf.error());
            return 1;
        }

        if (has_rcvbuf) {
            std::println(stderr, "receive buffer: requested {} bytes, kernel reports {} bytes",
                         rcvbuf, *initial_buf);
        } else {
            std::println(stderr, "receive buffer: {} bytes (kernel default)", *initial_buf);
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

        for (std::size_t i = 0; i < sockets.size(); ++i) {
            auto rep_res = send_report(
                sockets[i],
                StreamReport{.bytes = result->stream_bytes[i],
                             .elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 result->elapsed)});
            if (!rep_res) {
                std::println(stderr, "{}", rep_res.error());
                return 1;
            }
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

        auto end_buf = receive_buffer_size(sockets.front());
        if (!end_buf) {
            std::println(stderr, "{}", end_buf.error());
            return 1;
        }

        std::println("receive buffer at end: {} bytes", *end_buf);

        if (has_rcvbuf) {
            std::chrono::duration<double, std::milli> rtt_ms = min_handshake;
            std::chrono::duration<double> rtt_sec = min_handshake;
            double limit = megabits_per_second(static_cast<std::uint64_t>(rcvbuf), rtt_sec);
            std::println("window limit: {} bytes / {:.1f} ms = {:.2f} Mbit/s per stream", rcvbuf,
                         rtt_ms.count(), limit);
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
        std::uint64_t total_reported = 0;
        std::chrono::microseconds max_reported_elapsed{};

        for (const auto &res: results) {
            total_sent += res->bytes;
            max_last_send = std::max(max_last_send, res->last_send);
            max_peer_done = std::max(max_peer_done, res->peer_done);
            total_reported += res->report.bytes;
            max_reported_elapsed = std::max(max_reported_elapsed, res->report.elapsed);
        }

        std::chrono::duration<double> last_send = max_last_send;
        std::chrono::duration<double> peer_done = max_peer_done;
        std::chrono::duration<double> reported_elapsed = max_reported_elapsed;

        std::println("last send returned: {}", format_transfer(total_sent, last_send));
        std::println("receiver finished:  {}", format_transfer(total_sent, peer_done));
        std::println("receiver counted:   {}", format_transfer(total_reported, reported_elapsed));
        return 0;
    }

    print_usage();
    return 2;
}
