#include "receiver.hpp"
#include "speedtest.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <poll.h>
#include <span>
#include <sys/socket.h>
#include <system_error>
#include <vector>

std::expected<ReceiveResult, std::string>
receive(std::span<Socket> sockets, const std::function<void(const IntervalReport &)> &on_interval)
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

    std::vector<pollfd> fds;
    fds.reserve(sockets.size());
    for (const auto &sock: sockets) {
        fds.push_back(pollfd{.fd = sock.get(), .events = POLLIN, .revents = 0});
    }
    std::size_t open = sockets.size();
    std::vector<std::uint64_t> stream_bytes(sockets.size(), 0);

    while (open > 0) {
        auto wait = next_report - std::chrono::steady_clock::now();
        auto wait_ms = std::chrono::ceil<std::chrono::milliseconds>(wait);
        int timeout = static_cast<int>(wait_ms.count());
        if (timeout < 0) {
            timeout = 0;
        }

        int poll_res = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout);

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

        for (std::size_t i = 0; i < fds.size(); ++i) {
            auto &pfd = fds[i];
            if (pfd.revents == 0) {
                continue;
            }

            ssize_t bytes_read = ::recv(pfd.fd, buffer.get(), BUFFER_SIZE, 0);

            if (bytes_read == 0) {
                pfd.fd = -1;
                --open;
            } else if (bytes_read < 0) {
                int err = errno;
                if (err == EINTR) {
                    continue;
                }
                return std::unexpected(std::format("recv failed after {} bytes: {}", total,
                                                   std::system_category().message(err)));
            } else {
                auto count = static_cast<std::uint64_t>(bytes_read);
                interval_bytes += count;
                total += count;
                stream_bytes[i] += count;
            }
        }

        auto now = std::chrono::steady_clock::now();
        finish_interval(now);
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - start;

    if (interval_bytes > 0 && on_interval) {
        on_interval({interval_start - start, now - start, interval_bytes});
    }
    return ReceiveResult{
        .bytes = total, .elapsed = elapsed, .stream_bytes = std::move(stream_bytes)};
}

std::expected<ReceiveResult, std::string>
receive(Socket &socket, const std::function<void(const IntervalReport &)> &on_interval)
{
    return receive(std::span<Socket>{&socket, 1}, on_interval);
}

std::expected<void, std::string> recv_all(Socket &sock, std::span<char> data)
{
    std::size_t want = data.size();
    while (!data.empty()) {
        ssize_t rc = ::recv(sock.get(), data.data(), data.size(), 0);
        if (rc > 0) {
            data = data.subspan(static_cast<std::size_t>(rc));
        } else if (rc == 0) {
            std::size_t got = want - data.size();
            return std::unexpected(std::format("peer closed after {} of {} bytes", got, want));
        } else {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            return std::unexpected(
                std::format("recv failed: {}", std::system_category().message(err)));
        }
    }
    return {};
}
