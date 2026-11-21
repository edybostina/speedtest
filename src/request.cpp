#include "request.hpp"
#include "receiver.hpp"
#include "sender.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <expected>
#include <format>

std::expected<void, std::string> send_request(Socket &sock, const TestRequest &request)
{
    std::array<char, REQUEST_SIZE> req = {'S', 'P', 'T', '1'};
    req[4] = static_cast<char>(request.direction);
    req[5] = static_cast<char>(request.streams);

    std::uint16_t elapsed = static_cast<std::uint16_t>(request.duration.count());

    if (std::endian::native == std::endian::little) {
        elapsed = std::byteswap(elapsed);
    }
    auto e_arr = std::bit_cast<std::array<char, 2>>(elapsed);
    std::ranges::copy(e_arr, req.begin() + 6);

    return send_all(sock, req);
}

std::expected<TestRequest, std::string> read_request(Socket &sock)
{
    std::array<char, REQUEST_SIZE> req{};
    auto rec = recv_all(sock, req);
    if (!rec) {
        return std::unexpected(rec.error());
    }

    if (req[0] != 'S' || req[1] != 'P' || req[2] != 'T' || req[3] != '1') {
        return std::unexpected("not a speedtest client");
    }

    auto dir = parse_direction(req[4]);
    if (!dir) {
        return std::unexpected(dir.error());
    }

    unsigned char streams = static_cast<unsigned char>(req[5]);

    if (streams < 1 || streams > 16) {
        return std::unexpected(std::format("bad stream count: {}", streams));
    }

    if (dir == Direction::latency && streams != 1) {
        return std::unexpected("latency test uses only 1 stream");
    }

    std::array<char, 2> d_raw{};
    std::ranges::copy(req.begin() + 6, req.end(), d_raw.begin());

    std::uint16_t duration = std::bit_cast<std::uint16_t>(d_raw);

    if (std::endian::native == std::endian::little) {
        duration = std::byteswap(duration);
    }

    if (duration < 1 || duration > 3600) {
        return std::unexpected(std::format("bad duration: {} s", duration));
    }

    return TestRequest{.direction = dir.value(),
                       .streams = streams,
                       .duration =
                           std::chrono::seconds{static_cast<std::chrono::seconds::rep>(duration)}};
}
