#include "report.hpp"

#include "receiver.hpp"
#include "sender.hpp"
#include "wire.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <span>
#include <sys/socket.h>
#include <system_error>

std::expected<void, std::string> send_report(Socket &sock, const StreamReport &report)
{
    std::array<char, REPORT_SIZE> rep{};
    rep[0] = REPORT_TAG;

    put_u64(std::span{rep}.subspan<1, 8>(), report.bytes);
    put_u64(std::span{rep}.subspan<9, 8>(), static_cast<std::uint64_t>(report.elapsed.count()));

    return send_all(sock, rep);
}

std::expected<void, std::string> send_interval(Socket &sock, const IntervalReport &interval)
{
    std::array<char, INTERVAL_SIZE> rep{};
    rep[0] = INTERVAL_TAG;

    std::uint64_t start = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(interval.start).count());
    std::uint64_t end = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(interval.end).count());

    put_u64(std::span{rep}.subspan<1, 8>(), start);
    put_u64(std::span{rep}.subspan<9, 8>(), end);
    put_u64(std::span{rep}.subspan<17, 8>(), interval.bytes);

    return send_all(sock, rep);
}

std::expected<StreamReport, std::string>
read_report(Socket &sock, const std::function<void(const IntervalReport &)> &on_interval)
{
    for (;;) {
        std::array<char, 1> byte{};
        auto rec = recv_all(sock, byte);
        if (!rec) {
            return std::unexpected(rec.error());
        }

        if (byte[0] == INTERVAL_TAG) {
            std::array<char, INTERVAL_SIZE - 1> rep{};
            rec = recv_all(sock, rep);
            if (!rec) {
                return std::unexpected(rec.error());
            }

            auto start = get_u64(std::span{rep}.subspan<0, 8>());
            auto end = get_u64(std::span{rep}.subspan<8, 8>());
            auto bytes = get_u64(std::span{rep}.subspan<16, 8>());

            IntervalReport interval = {
                .start = std::chrono::duration<double>(std::chrono::microseconds(start)),
                .end = std::chrono::duration<double>(std::chrono::microseconds(end)),
                .bytes = bytes};
            if (on_interval) {
                on_interval(interval);
            }

        } else if (byte[0] == REPORT_TAG) {
            std::array<char, REPORT_SIZE - 1> rep{};
            rec = recv_all(sock, rep);
            if (!rec) {
                return std::unexpected(rec.error());
            }

            auto bytes = get_u64(std::span{rep}.subspan<0, 8>());
            auto elapsed = get_u64(std::span{rep}.subspan<8, 8>());

            char extra = 0;
            for (;;) {
                ssize_t n = ::recv(sock.get(), &extra, sizeof(extra), 0);
                if (n == 0) {
                    break;
                }
                if (n > 0) {
                    return std::unexpected("unexpected data after report");
                }
                int err = errno;
                if (err == EINTR) {
                    continue;
                }
                return std::unexpected(std::system_category().message(err));
            }

            return StreamReport{
                .bytes = bytes,
                .elapsed =
                    std::chrono::microseconds{static_cast<std::chrono::microseconds::rep>(elapsed)},
            };
        } else {
            return std::unexpected(
                std::format("unknown message tag {}", static_cast<unsigned char>(byte[0])));
        }
    }
}
