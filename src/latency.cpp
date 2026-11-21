#include "latency.hpp"

#include "receiver.hpp"
#include "sender.hpp"
#include "speedtest.hpp"
#include "wire.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <print>
#include <sys/socket.h>
#include <system_error>
#include <thread>

std::expected<std::uint64_t, std::string> echo_probes(Socket &sock)
{
    std::uint64_t count = 0;
    std::size_t have = 0;
    std::array<char, 8> buf{};

    for (;;) {
        ssize_t rc = ::recv(sock.get(), buf.data() + have, 8 - have, 0);
        if (rc == 0) {
            if (have == 0) {
                return count;
            }
            return std::unexpected("peer closed in the middle of a probe");
        }

        if (rc < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            return std::unexpected(std::system_category().message(err));
        }

        have += static_cast<std::size_t>(rc);
        if (have == 8) {
            auto res = send_all(sock, buf);
            if (!res) {
                return std::unexpected(res.error());
            }
            have = 0;
            ++count;
        }
    }
}

std::expected<std::vector<double>, std::string> measure_latency(Socket &sock,
                                                                std::chrono::seconds duration)
{
    auto start = std::chrono::steady_clock::now();
    auto deadline = start + duration;
    auto next = start;

    std::uint64_t seq = 0;
    std::vector<double> result{};
    result.reserve(static_cast<std::size_t>(duration / PROBE_INTERVAL) + 1);

    while (std::chrono::steady_clock::now() < deadline) {
        std::array<char, 8> buf{};
        put_u64(std::span{buf}, seq);

        auto t0 = std::chrono::steady_clock::now();

        auto snd = send_all(sock, buf);
        if (!snd) {
            return std::unexpected(snd.error());
        }

        auto rcv = recv_all(sock, buf);
        if (!rcv) {
            return std::unexpected(rcv.error());
        }

        auto t1 = std::chrono::steady_clock::now();

        std::uint64_t recv_seq = get_u64(buf);
        if (recv_seq != seq) {
            return std::unexpected(std::format("probe {} came back as {}", seq, recv_seq));
        }

        std::chrono::duration<double, std::milli> prb = t1 - t0;
        result.push_back(prb.count());
        std::println("probe {}: {:.3f} ms", seq, prb.count());
        ++seq;

        next += PROBE_INTERVAL;
        if (next < std::chrono::steady_clock::now()) {
            next = std::chrono::steady_clock::now();
        }

        std::this_thread::sleep_until(next);
    }

    int rc = ::shutdown(sock.get(), SHUT_WR);
    if (rc != 0) {
        int err = errno;

        return std::unexpected(
            std::format("shutdown failed: {}", std::system_category().message(err)));
    }

    char extra = 0;
    for (;;) {
        ssize_t n = ::recv(sock.get(), &extra, sizeof(extra), 0);
        if (n == 0) {
            break;
        }
        if (n > 0) {
            return std::unexpected("unexpected data after probes");
        }
        int err = errno;
        if (err == EINTR) {
            continue;
        }
        return std::unexpected(std::system_category().message(err));
    }

    return result;
}
