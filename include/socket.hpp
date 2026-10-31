#pragma once

#include <expected>
#include <string>
#include <unistd.h>

class Socket
{
private:
    int _fd;

public:
    Socket(): _fd(-1) {}

    explicit Socket(int fd): _fd(fd) {}

    // copy
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    // move
    Socket(Socket &&other) noexcept: _fd(other._fd)
    {
        other._fd = -1;
    }
    Socket &operator=(Socket &&other) noexcept
    {
        if (this != &other) {
            if (_fd >= 0) {
                close(_fd);
            }
            _fd = other._fd;
            other._fd = -1;
        }
        return *this;
    }

    // destructor
    ~Socket()
    {
        if (_fd >= 0) {
            close(_fd);
        }
    }

    [[nodiscard]] int get() const noexcept
    {
        return _fd;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return _fd >= 0;
    }
};

std::expected<Socket, std::string> connect_to(const std::string &host, const std::string &port);
