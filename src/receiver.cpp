#include "receiver.hpp"
#include "speedtest.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <poll.h>
#include <sys/socket.h>
#include <system_error>

std::expected<ReceiveResult, std::string>
receive(Socket &socket, const std::function<void(const IntervalReport &)> &on_interval)
{
    std::uint64_t total = 0;
    auto buffer = std::make_unique_for_overwrite<char[]>(BUFFER_SIZE);

    auto start = std::chrono::steady_clock::now();
    auto interval_start = start;
    std::uint64_t interval_bytes = 0;
    auto next_report = start + REPORT_INTERVAL;

    auto finish_interval = [&](std::chrono::steady_clock::time_point now) {
        if (now >= next_report) {
            if (on_interval) {
                on_interval({interval_start - start, now - start, interval_bytes});
            }
            interval_start = now;
            interval_bytes = 0;

            while (now >= next_report) {
                next_report += REPORT_INTERVAL;
            }
        }
    };

    for (;;) {
        auto wait = next_report - std::chrono::steady_clock::now();
        auto wait_ms = std::chrono::ceil<std::chrono::milliseconds>(wait);
        int timeout = static_cast<int>(wait_ms.count());
        if (timeout < 0) {
            timeout = 0;
        }

        pollfd pfd{.fd = socket.get(), .events = POLLIN, .revents = 0};
        int poll_res = ::poll(&pfd, 1, timeout);

        if (poll_res < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            return std::unexpected(std::format("poll failed after {} bytes: {}", total,
                                               std::system_category().message(err)));
        }

        if (poll_res == 0) {
            finish_interval(std::chrono::steady_clock::now());
            continue;
        }

        ssize_t bytes_read = ::recv(socket.get(), buffer.get(), BUFFER_SIZE, 0);

        if (bytes_read == 0) {
            auto elapsed = std::chrono::steady_clock::now() - start;

            if (interval_bytes > 0 && on_interval) {
                auto now = std::chrono::steady_clock::now();
                on_interval({interval_start - start, now - start, interval_bytes});
            }
            return ReceiveResult{.bytes = total, .elapsed = elapsed};
        }

        if (bytes_read < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            return std::unexpected(std::format("recv failed after {} bytes: {}", total,
                                               std::system_category().message(err)));
        }

        interval_bytes += static_cast<std::uint64_t>(bytes_read);
        total += static_cast<std::uint64_t>(bytes_read);

        auto now = std::chrono::steady_clock::now();
        finish_interval(now);
    }
}
