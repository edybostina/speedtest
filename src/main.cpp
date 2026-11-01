#include "receiver.hpp"
#include "socket.hpp"

#include <print>
#include <string_view>

int main(int argc, char *argv[])
{
    if (argc != 4 || std::string_view(argv[1]) != "recv") {
        std::println(stderr, "usage: speedtest recv HOST PORT");
        return 2;
    }

    const std::string host = argv[2];
    const std::string port = argv[3];

    auto sock = connect_to(host, port);
    if (!sock) {
        std::println(stderr, "{}", sock.error());
        return 1;
    }

    std::println(stderr, "connected to {}:{}", host, port);

    auto result = receive(*sock);
    if (!result) {
        std::println(stderr, "{}", result.error());
        return 1;
    }

    std::println("{}", *result);
    return 0;
}
