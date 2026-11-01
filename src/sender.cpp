#include "sender.hpp"
#include "receiver.hpp"

#include <system_error>
#include <format>
#include <cerrno>
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <sys/socket.h>

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

    return {}; // great succes!
}

std::expected<void, std::string> send_bytes(Socket &sock, std::uint64_t total)
{
    std::uint64_t sent = 0;
    auto buffer = std::make_unique<char[]>(BUFFER_SIZE);
    while (sent < total) {
        std::size_t chunk = static_cast<std::size_t>(
            std::min(total - sent, static_cast<std::uint64_t>(BUFFER_SIZE)));

        auto send = send_all(sock, std::span<const char>{buffer.get(), chunk});
        if (!send) {
            return std::unexpected(
                std::format("send failed after {} of {} bytes: {}", sent, total, send.error()));
        }
        sent += chunk;
    }

    int rc = ::shutdown(sock.get(), SHUT_WR);
    if (rc != 0) {
        int err = errno;

        return std::unexpected(
            std::format("shutdown failed: {}", std::system_category().message(err)));
    }

    auto rec = receive(sock);
    if (!rec) {
        return std::unexpected(std::format("receive failed: {}", rec.error()));
    }

    return {};
}
