#include "receiver.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <sys/socket.h>
#include <system_error>

constexpr std::size_t BUFFER_SIZE = 128 * 1024;

std::expected<std::uint64_t, std::string> receive(Socket &socket)
{
    std::uint64_t total = 0;
    auto buffer = std::make_unique_for_overwrite<char[]>(BUFFER_SIZE);

    for (;;) {
        ssize_t bytes_read = ::recv(socket.get(), buffer.get(), BUFFER_SIZE, 0);

        if (bytes_read == 0) {
            return total;
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
