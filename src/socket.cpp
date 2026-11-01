#include "socket.hpp"

#include <cerrno>
#include <format>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
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

std::expected<Socket, std::string> listen_on(const std::string &port)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    struct addrinfo *result = nullptr;
    int rc = getaddrinfo(nullptr, port.c_str(), &hints, &result);
    if (rc != 0) {
        int err = errno;
        std::string err_msg =
            (rc == EAI_SYSTEM) ? std::system_category().message(err) : gai_strerror(rc);
        return std::unexpected(std::format("failed to resolve port {}: {}", port, err_msg));
    }

    std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> list(result, freeaddrinfo);

    auto try_bind_listen = [&](const struct addrinfo *rp) -> std::expected<Socket, std::string> {
        int fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            int err = errno;
            return std::unexpected(std::format("socket creation on port {} failed: {}", port,
                                               std::system_category().message(err)));
        }

        Socket sock(fd);

        int opt = 1;
        if (::setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            int err = errno;
            return std::unexpected(std::format("setsockopt SO_REUSEADDR on port {} failed: {}",
                                               port, std::system_category().message(err)));
        }

        if (rp->ai_family == AF_INET6) {
            int v6only = 0;
            if (::setsockopt(sock.get(), IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0) {
                int err = errno;
                return std::unexpected(std::format("setsockopt IPV6_V6ONLY on port {} failed: {}",
                                                   port, std::system_category().message(err)));
            }
        }

        if (::bind(sock.get(), rp->ai_addr, rp->ai_addrlen) < 0) {
            int err = errno;
            return std::unexpected(std::format("bind on port {} failed: {}", port,
                                               std::system_category().message(err)));
        }

        if (::listen(sock.get(), SOMAXCONN) < 0) {
            int err = errno;
            return std::unexpected(std::format("listen on port {} failed: {}", port,
                                               std::system_category().message(err)));
        }

        return sock;
    };

    std::string last_error =
        std::format("failed to listen on port {}: no suitable addresses found", port);

    for (const struct addrinfo *rp = list.get(); rp != nullptr; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET6) {
            auto res = try_bind_listen(rp);
            if (res) {
                return res;
            }
            last_error = std::move(res.error());
        }
    }

    for (const struct addrinfo *rp = list.get(); rp != nullptr; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            auto res = try_bind_listen(rp);
            if (res) {
                return res;
            }
            last_error = std::move(res.error());
        }
    }

    return std::unexpected(std::move(last_error));
}

std::expected<Accepted, std::string> accept_from(Socket &listener)
{
    struct sockaddr_storage storage{};
    socklen_t addr_len = 0;
    int client_fd = -1;

    for (;;) {
        addr_len = sizeof(storage);
        client_fd =
            ::accept(listener.get(), reinterpret_cast<struct sockaddr *>(&storage), &addr_len);

        if (client_fd < 0) {
            int err = errno;
            if (err == EINTR || err == ECONNABORTED) {
                continue;
            }
            return std::unexpected(
                std::format("accept failed: {}", std::system_category().message(err)));
        }
        break;
    }

    Socket client_sock(client_fd);

    char host_buf[NI_MAXHOST]{};
    char serv_buf[NI_MAXSERV]{};
    std::string peer = "unknown";

    int rc =
        getnameinfo(reinterpret_cast<const struct sockaddr *>(&storage), addr_len, host_buf,
                    sizeof(host_buf), serv_buf, sizeof(serv_buf), NI_NUMERICHOST | NI_NUMERICSERV);

    if (rc == 0) {
        if (storage.ss_family == AF_INET6) {
            peer = std::format("[{}]:{}", host_buf, serv_buf);
        } else {
            peer = std::format("{}:{}", host_buf, serv_buf);
        }
    }

    return Accepted{
        .socket = std::move(client_sock),
        .peer = std::move(peer),
    };
}
