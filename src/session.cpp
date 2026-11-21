#include "session.hpp"

#include "receiver.hpp"
#include "report.hpp"
#include "sender.hpp"
#include "stats.hpp"
#include "units.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <print>
#include <string_view>
#include <thread>
#include <vector>

void print_build_note()
{
#ifdef SPEEDTEST_SANITIZED
    std::println(
        stderr, "note: ASan/UBSan build, rates are not representative (measure with build-bench/)");
#endif
}

namespace {
class IntervalLog
{
public:
    explicit IntervalLog(std::chrono::seconds omit): omit_(omit) {}

    void add(const IntervalReport &interval)
    {
        double rate = megabits_per_second(interval.bytes, interval.end - interval.start);
        const std::string_view suffix = interval.start < omit_ ? " (omitted)" : "";
        std::println("{:7.3f}-{:7.3f} s {:12} bytes {:10.2f} Mbit/s{}", interval.start.count(),
                     interval.end.count(), interval.bytes, rate, suffix);

        if (interval.start >= omit_) {
            kept_bytes_ += interval.bytes;
            kept_time_ += interval.end - interval.start;
            if (interval.end - interval.start >= std::chrono::duration<double>{0.5}) {
                kept_rates_.push_back(rate);
            }
        }
    }

    void print_summary(std::uint64_t bytes, std::chrono::duration<double> elapsed) const
    {
        std::println("{}", format_transfer(bytes, elapsed));
        if (omit_.count() > 0) {
            if (kept_time_.count() > 0) {
                std::println("after omitting {} s: {}", omit_.count(),
                             format_transfer(kept_bytes_, kept_time_));
            } else {
                std::println("after omitting {} s: nothing left to measure", omit_.count());
            }
        }

        if (auto stats = summarize(kept_rates_)) {
            std::println("per-second: {} samples, min {:.2f}, median {:.2f}, max {:.2f} Mbit/s",
                         stats->samples, stats->min, stats->median, stats->max);
        }
    }

private:
    std::chrono::seconds omit_{0};
    std::uint64_t kept_bytes_{0};
    std::chrono::duration<double> kept_time_{0};
    std::vector<double> kept_rates_{};
};
} // namespace

int run_send(std::span<Socket> sockets, std::chrono::seconds duration,
             std::optional<std::chrono::seconds> omit)
{
    print_build_note();

    auto start = std::chrono::steady_clock::now();
    bool live = omit.has_value();
    IntervalLog log{omit.value_or(std::chrono::seconds{0})};

    std::size_t n = sockets.size();
    std::vector<std::expected<SendResult, std::string>> sent(n);
    std::vector<std::expected<StreamReport, std::string>> reports(n);
    std::vector<std::chrono::steady_clock::duration> peer_done(n);

    {
        std::vector<std::jthread> threads;
        threads.reserve(n + (live ? 1 : 0));

        if (live) {
            threads.emplace_back([&sockets, &reports, &peer_done, &log, start] {
                reports[0] = read_report(
                    sockets[0], [&log](const IntervalReport &interval) { log.add(interval); });
                peer_done[0] = std::chrono::steady_clock::now() - start;
            });
        }

        for (std::size_t i = 0; i < n; ++i) {
            threads.emplace_back([i, &sockets, &sent, &reports, &peer_done, duration, live, start] {
                sent[i] = send_for(sockets[i], duration);
                if (sent[i] && !(live && i == 0)) {
                    reports[i] = read_report(sockets[i]);
                    peer_done[i] = std::chrono::steady_clock::now() - start;
                }
            });
        }
    }

    bool has_error = false;
    for (const auto &res: sent) {
        if (!res) {
            std::println(stderr, "{}", res.error());
            has_error = true;
        }
    }
    for (const auto &res: reports) {
        if (!res) {
            std::println(stderr, "receiver report failed: {}", res.error());
            has_error = true;
        }
    }
    if (has_error) {
        return 1;
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (reports[i]->bytes != sent[i]->bytes) {
            std::println(stderr, "stream {}: receiver counted {} of {} bytes", i, reports[i]->bytes,
                         sent[i]->bytes);
            return 1;
        }
    }

    std::uint64_t total_sent = 0;
    std::chrono::steady_clock::duration max_last_send{};
    std::chrono::steady_clock::duration max_peer_done{};
    std::uint64_t total_reported = 0;
    std::chrono::microseconds max_reported_elapsed{};

    for (std::size_t i = 0; i < n; ++i) {
        total_sent += sent[i]->bytes;
        max_last_send = std::max(max_last_send, sent[i]->last_send);
        max_peer_done = std::max(max_peer_done, peer_done[i]);
        total_reported += reports[i]->bytes;
        max_reported_elapsed = std::max(max_reported_elapsed, reports[i]->elapsed);
    }

    if (live) {
        log.print_summary(total_reported, max_reported_elapsed);
    } else {
        std::chrono::duration<double> last_send = max_last_send;
        std::chrono::duration<double> peer_done = max_peer_done;
        std::chrono::duration<double> reported_elapsed = max_reported_elapsed;

        std::println("last send returned: {}", format_transfer(total_sent, last_send));
        std::println("receiver finished:  {}", format_transfer(total_sent, peer_done));
        std::println("receiver counted:   {}", format_transfer(total_reported, reported_elapsed));
    }
    return 0;
}

int run_receive(std::span<Socket> sockets, std::chrono::seconds omit,
                std::optional<std::chrono::seconds> requested)
{
    print_build_note();

    IntervalLog log{omit};
    std::optional<std::string> send_error;

    auto callback = [&](const IntervalReport &interval) {
        log.add(interval);
        if (requested.has_value() && !send_error.has_value()) {
            auto res = send_interval(sockets[0], interval);
            if (!res) {
                send_error = res.error();
            }
        }
    };

    auto result = receive(sockets, callback);
    if (!result) {
        std::println(stderr, "{}", result.error());
        return 1;
    }

    if (send_error.has_value()) {
        std::println(stderr, "live report failed: {}", *send_error);
        return 1;
    }

    for (std::size_t i = 0; i < sockets.size(); ++i) {
        auto rep_res = send_report(
            sockets[i],
            StreamReport{
                .bytes = result->stream_bytes[i],
                .elapsed = std::chrono::duration_cast<std::chrono::microseconds>(result->elapsed)});
        if (!rep_res) {
            std::println(stderr, "{}", rep_res.error());
            return 1;
        }
    }

    log.print_summary(result->bytes, result->elapsed);

    if (requested.has_value()) {
        std::chrono::duration<double> req_sec = *requested;
        std::chrono::duration<double> elapsed_sec = result->elapsed;
        if (elapsed_sec < 0.9 * req_sec) {
            std::println("warning: test ended after {:.3f} s of {} s requested",
                         elapsed_sec.count(), requested->count());
        }
    }

    return 0;
}
