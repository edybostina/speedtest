#include "receiver.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <sys/socket.h>
#include <system_error>

std::expected<ReceiveResult, std::string> receive(Socket &socket)
{
    std::uint64_t total = 0;
    auto buffer = std::make_unique_for_overwrite<char[]>(BUFFER_SIZE);

    auto start = std::chrono::steady_clock::now();

    for (;;) {
        ssize_t bytes_read = ::recv(socket.get(), buffer.get(), BUFFER_SIZE, 0);

        if (bytes_read == 0) {
            auto elapsed = std::chrono::steady_clock::now() - start;
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

        total += static_cast<std::uint64_t>(bytes_read);
    }
}
