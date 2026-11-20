#include "session.hpp"

#include "receiver.hpp"
#include "report.hpp"
#include "sender.hpp"
#include "stats.hpp"
#include "units.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
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

int run_send(std::span<Socket> sockets, std::chrono::seconds duration)
{
    print_build_note();

    std::size_t streams = sockets.size();
    std::vector<std::expected<SendResult, std::string>> results(streams);
    {
        std::vector<std::jthread> threads;
        threads.reserve(streams);
        for (std::size_t i = 0; i < streams; ++i) {
            threads.emplace_back(
                [i, &sockets, &results, duration] { results[i] = send_for(sockets[i], duration); });
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

int run_receive(std::span<Socket> sockets, std::chrono::seconds omit)
{
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
            StreamReport{
                .bytes = result->stream_bytes[i],
                .elapsed = std::chrono::duration_cast<std::chrono::microseconds>(result->elapsed)});
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

    return 0;
}
