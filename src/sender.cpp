#include "sender.hpp"

#include "speedtest.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <sys/socket.h>
#include <system_error>

#ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SEND_FLAGS = 0;
#endif

std::expected<void, std::string> send_all(Socket &sock, std::span<const char> data)
{
    while (!data.empty()) {
        ssize_t rc = ::send(sock.get(), data.data(), data.size(), SEND_FLAGS);

        if (rc > 0) {
            data = data.subspan(static_cast<size_t>(rc));
        } else if (rc == 0) {
            return std::unexpected("send returned 0");
        } else {
            const int err = errno;

            if (err == EINTR) {
                continue;
            }

            if (err == EAGAIN || err == EWOULDBLOCK) {
                // idk
            }

            return std::unexpected(std::format("send failed: {} (errno {})",
                                               std::system_category().message(err), err));
        }
    }

    return {};
}

std::expected<SendResult, std::string> send_for(Socket &sock, std::chrono::seconds duration)
{
    std::uint64_t sent = 0;
    auto buffer = std::make_unique<char[]>(BUFFER_SIZE);

    auto start = std::chrono::steady_clock::now();
    auto deadline = start + duration;

    while (std::chrono::steady_clock::now() < deadline) {
        auto send = send_all(sock, std::span<const char>{buffer.get(), BUFFER_SIZE});
        if (!send) {
            return std::unexpected(
                std::format("send failed after {} bytes: {}", sent, send.error()));
        }
        sent += BUFFER_SIZE;
    }

    auto last_send = std::chrono::steady_clock::now() - start;

    int rc = ::shutdown(sock.get(), SHUT_WR);
    if (rc != 0) {
        int err = errno;

        return std::unexpected(
            std::format("shutdown failed: {}", std::system_category().message(err)));
    }

    return SendResult{.bytes = sent, .last_send = last_send};
}
