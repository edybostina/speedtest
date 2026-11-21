#include "latency.hpp"
#include "request.hpp"
#include "session.hpp"
#include "socket.hpp"
#include "stats.hpp"
#include "units.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <expected>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view USAGE =
    "usage: speedtest serve PORT\n"
    "       speedtest down HOST PORT SECONDSs [OMITs] [STREAMS] [BUFk|BUFm]\n"
    "       speedtest up HOST PORT SECONDSs [OMITs] [STREAMS]\n"
    "       speedtest ping HOST PORT SECONDSs";

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

struct RecvOptions {
    std::chrono::seconds omit{0};
    std::size_t streams = 1;
    int rcvbuf = 0;
};

std::optional<RecvOptions> parse_recv_options(std::span<char *const> args)
{
    std::chrono::seconds omit{0};
    bool has_omit = false;
    std::size_t streams = 1;
    bool has_streams = false;
    int rcvbuf = 0;
    bool has_rcvbuf = false;

    for (const char *arg_str: args) {
        std::string_view arg = arg_str;
        if (arg.ends_with('s')) {
            if (has_omit) {
                return std::nullopt;
            }
            auto parsed_omit = parse_seconds(arg, 0, 60);
            if (!parsed_omit) {
                return std::nullopt;
            }
            omit = *parsed_omit;
            has_omit = true;
        } else if (arg.ends_with('k') || arg.ends_with('m')) {
            if (has_rcvbuf) {
                return std::nullopt;
            }
            auto parsed_buf = parse_size(arg);
            if (!parsed_buf) {
                return std::nullopt;
            }
            rcvbuf = *parsed_buf;
            has_rcvbuf = true;
        } else {
            if (has_streams) {
                return std::nullopt;
            }
            auto parsed_streams = parse_streams(arg);
            if (!parsed_streams) {
                return std::nullopt;
            }
            streams = *parsed_streams;
            has_streams = true;
        }
    }

    return RecvOptions{.omit = omit, .streams = streams, .rcvbuf = rcvbuf};
}

struct Connections {
    std::vector<Socket> sockets;
    std::chrono::steady_clock::duration min_handshake;
};

std::expected<Connections, std::string> connect_streams(const std::string &host,
                                                        const std::string &port,
                                                        const TestRequest &request, int rcvbuf)
{
    std::vector<Socket> sockets;
    sockets.reserve(request.streams);
    std::chrono::steady_clock::duration min_handshake = std::chrono::steady_clock::duration::max();

    for (std::size_t i = 0; i < request.streams; ++i) {
        auto conn = connect_to(host, port, rcvbuf);
        if (!conn) {
            return std::unexpected(conn.error());
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

        auto req_res = send_request(conn->socket, request);
        if (!req_res) {
            return std::unexpected(req_res.error());
        }

        sockets.push_back(std::move(conn->socket));
    }

    return Connections{.sockets = std::move(sockets), .min_handshake = min_handshake};
}

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

    if (mode == "serve") {
        if (argc != 3) {
            print_usage();
            return 2;
        }

        const std::string port = argv[2];

        auto listener = listen_on(port);
        if (!listener) {
            std::println(stderr, "{}", listener.error());
            return 1;
        }

        std::println(stderr, "listening on port {}", port);

        while (true) {
            auto first_acc = accept_from(*listener);
            if (!first_acc) {
                std::println(stderr, "{}", first_acc.error());
                continue;
            }

            auto first_req = read_request(first_acc->socket);
            if (!first_req) {
                std::println(stderr, "{}: {}", first_acc->peer, first_req.error());
                continue;
            }

            std::vector<Socket> sockets;
            sockets.reserve(first_req->streams);
            sockets.push_back(std::move(first_acc->socket));

            bool failed = false;
            for (std::size_t i = 1; i < first_req->streams; ++i) {
                auto acc = accept_from(*listener);
                if (!acc) {
                    std::println(stderr, "{}", acc.error());
                    failed = true;
                    break;
                }

                auto req = read_request(acc->socket);
                if (!req) {
                    std::println(stderr, "{}: {}", acc->peer, req.error());
                    failed = true;
                    break;
                }

                if (*req != *first_req) {
                    std::println(stderr, "{}: request does not match the first", acc->peer);
                    failed = true;
                    break;
                }

                sockets.push_back(std::move(acc->socket));
            }

            if (failed) {
                continue;
            }

            std::string_view direction = direction_name(first_req->direction);
            std::println("test from {}: {}, {} streams, {} s", first_acc->peer, direction,
                         +first_req->streams, first_req->duration.count());

            if (first_req->direction == Direction::download) {
                run_send(sockets, first_req->duration, std::nullopt);
            } else if (first_req->direction == Direction::upload) {
                run_receive(sockets, std::chrono::seconds{0}, first_req->duration);
            } else if (first_req->direction == Direction::latency) {
                auto nd = set_no_delay(sockets[0]);
                if (!nd) {
                    std::println(stderr, "{}", nd.error());
                } else {
                    auto res = echo_probes(sockets[0]);
                    if (!res) {
                        std::println(stderr, "{}", res.error());
                    } else {
                        std::println("echoed {} probes", *res);
                    }
                }
            }

            std::fflush(stdout);
        }
    }

    if (mode == "down") {
        if (argc < 5 || argc > 8) {
            print_usage();
            return 2;
        }

        const std::string host = argv[2];
        const std::string port = argv[3];

        auto duration = parse_seconds(argv[4], 1, 3600);
        if (!duration) {
            print_usage();
            return 2;
        }

        auto opts = parse_recv_options({argv + 5, argv + argc});
        if (!opts) {
            print_usage();
            return 2;
        }

        TestRequest request{.direction = Direction::download,
                            .streams = static_cast<std::uint8_t>(opts->streams),
                            .duration = *duration};

        auto conn = connect_streams(host, port, request, opts->rcvbuf);
        if (!conn) {
            std::println(stderr, "{}", conn.error());
            return 1;
        }

        auto initial_buf = receive_buffer_size(conn->sockets.front());
        if (!initial_buf) {
            std::println(stderr, "{}", initial_buf.error());
            return 1;
        }

        if (opts->rcvbuf > 0) {
            std::println(stderr, "receive buffer: requested {} bytes, kernel reports {} bytes",
                         opts->rcvbuf, *initial_buf);
        } else {
            std::println(stderr, "receive buffer: {} bytes (kernel default)", *initial_buf);
        }

        if (int rc = run_receive(conn->sockets, opts->omit, std::nullopt); rc != 0) {
            return rc;
        }

        auto end_buf = receive_buffer_size(conn->sockets.front());
        if (!end_buf) {
            std::println(stderr, "{}", end_buf.error());
            return 1;
        }

        std::println("receive buffer at end: {} bytes", *end_buf);

        if (opts->rcvbuf > 0) {
            std::chrono::duration<double, std::milli> rtt_ms = conn->min_handshake;
            std::chrono::duration<double> rtt_sec = conn->min_handshake;
            double limit = megabits_per_second(static_cast<std::uint64_t>(opts->rcvbuf), rtt_sec);
            std::println("window limit: {} bytes / {:.1f} ms = {:.2f} Mbit/s per stream",
                         opts->rcvbuf, rtt_ms.count(), limit);
        }
        return 0;
    }

    if (mode == "up") {
        if (argc < 5 || argc > 7) {
            print_usage();
            return 2;
        }

        const std::string host = argv[2];
        const std::string port = argv[3];

        auto duration = parse_seconds(argv[4], 1, 3600);
        if (!duration) {
            print_usage();
            return 2;
        }

        auto opts = parse_recv_options({argv + 5, argv + argc});
        if (!opts || opts->rcvbuf != 0) {
            print_usage();
            return 2;
        }

        TestRequest request{.direction = Direction::upload,
                            .streams = static_cast<std::uint8_t>(opts->streams),
                            .duration = *duration};

        auto conn = connect_streams(host, port, request, 0);
        if (!conn) {
            std::println(stderr, "{}", conn.error());
            return 1;
        }

        return run_send(conn->sockets, *duration, opts->omit);
    }

    if (mode == "ping") {
        if (argc != 5) {
            print_usage();
            return 2;
        }

        const std::string host = argv[2];
        const std::string port = argv[3];

        auto duration = parse_seconds(argv[4], 1, 3600);
        if (!duration) {
            print_usage();
            return 2;
        }

        TestRequest request{.direction = Direction::latency, .streams = 1, .duration = *duration};

        auto conn = connect_streams(host, port, request, 0);
        if (!conn) {
            std::println(stderr, "{}", conn.error());
            return 1;
        }

        auto nd = set_no_delay(conn->sockets[0]);
        if (!nd) {
            std::println(stderr, "{}", nd.error());
            return 1;
        }

        auto rtts = measure_latency(conn->sockets[0], *duration);
        if (!rtts) {
            std::println(stderr, "{}", rtts.error());
            return 1;
        }

        auto stats = summarize(*rtts);
        if (stats) {
            auto jit = jitter(*rtts);
            std::string jitter_str = jit ? std::format("{:.3f} ms", *jit) : "n/a";
            std::println("latency: {} probes, min {:.3f}, median {:.3f}, max {:.3f} ms, jitter {}",
                         stats->samples, stats->min, stats->median, stats->max, jitter_str);
        }
        return 0;
    }

    print_usage();
    return 2;
}
