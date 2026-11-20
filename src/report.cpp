#include "report.hpp"
#include "receiver.hpp"
#include "sender.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <sys/socket.h>
#include <system_error>

std::expected<void, std::string> send_report(Socket &sock, const StreamReport &report)
{
    std::array<char, REPORT_SIZE> rep{};
    std::uint64_t bytes = report.bytes;
    std::uint64_t elapsed = static_cast<std::uint64_t>(report.elapsed.count());
    if (std::endian::native == std::endian::little) {
        bytes = std::byteswap(bytes);
        elapsed = std::byteswap(elapsed);
    }
    auto b_arr = std::bit_cast<std::array<char, 8>>(bytes);
    auto e_arr = std::bit_cast<std::array<char, 8>>(elapsed);
    std::ranges::copy(b_arr, rep.begin());
    std::ranges::copy(e_arr, rep.begin() + 8);

    return send_all(sock, rep);
}

std::expected<StreamReport, std::string> read_report(Socket &sock)
{
    std::array<char, REPORT_SIZE> rep{};
    auto rec = recv_all(sock, rep);
    if (!rec) {
        return std::unexpected(rec.error());
    }

    std::array<char, 8> b_raw{};
    std::array<char, 8> e_raw{};
    std::ranges::copy(rep.begin(), rep.begin() + 8, b_raw.begin());
    std::ranges::copy(rep.begin() + 8, rep.end(), e_raw.begin());

    auto bytes = std::bit_cast<std::uint64_t>(b_raw);
    auto elapsed = std::bit_cast<std::uint64_t>(e_raw);

    if (std::endian::native == std::endian::little) {
        bytes = std::byteswap(bytes);
        elapsed = std::byteswap(elapsed);
    }

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
        .elapsed = std::chrono::microseconds{static_cast<std::chrono::microseconds::rep>(elapsed)},
    };
}
