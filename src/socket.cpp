#include "socket.hpp"

#include <cerrno>
#include <format>
#include <memory>
#include <netdb.h>
#include <sys/socket.h>
#include <system_error>
#include <utility>

std::expected<Socket, std::string> connect_to(const std::string &host, const std::string &port)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags = 0;

    struct addrinfo *result = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0) {
        int err = errno;
        std::string err_msg =
            (rc == EAI_SYSTEM) ? std::system_category().message(err) : gai_strerror(rc);
        return std::unexpected(std::format("failed to resolve {}:{}: {}", host, port, err_msg));
    }

    std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> list(result, freeaddrinfo);

    std::string last_error =
        std::format("failed to connect to {}:{}: no addresses found", host, port);

    for (const struct addrinfo *rp = list.get(); rp != nullptr; rp = rp->ai_next) {
        int fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            int err = errno;
            last_error = std::format("failed to create socket for {}:{}: {}", host, port,
                                     std::system_category().message(err));
            continue;
        }

        Socket sock(fd);

        if (::connect(sock.get(), rp->ai_addr, rp->ai_addrlen) == 0) {
            return sock;
        }

        int err = errno;
        last_error = std::format("failed to connect to {}:{}: {}", host, port,
                                 std::system_category().message(err));
    }

    return std::unexpected(std::move(last_error));
}
