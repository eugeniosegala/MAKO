/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "content_hash.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <span>
#include <string>
#include <vector>

namespace {
    constexpr std::array<uint32_t, 64> roundConstants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    [[nodiscard]] uint32_t readBigEndian(
            const std::span<const uint8_t> data, const size_t offset) {
        return (static_cast<uint32_t>(data[offset]) << 24U) |
            (static_cast<uint32_t>(data[offset + 1]) << 16U) |
            (static_cast<uint32_t>(data[offset + 2]) << 8U) |
            static_cast<uint32_t>(data[offset + 3]);
    }
}

std::string mako::backend::detail::sha256Hex(
        const std::span<const uint8_t> data) {
    std::vector<uint8_t> padded(data.begin(), data.end());
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U)
        padded.push_back(0U);
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8U;
    for (int shift = 56; shift >= 0; shift -= 8)
        padded.push_back(static_cast<uint8_t>(bitLength >> shift));

    std::array<uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<uint32_t, 64> schedule{};
    for (size_t block = 0; block < padded.size(); block += 64U) {
        const std::span<const uint8_t> bytes{padded.data() + block, 64U};
        for (size_t i = 0; i < 16U; ++i)
            schedule[i] = readBigEndian(bytes, i * 4U);
        for (size_t i = 16U; i < schedule.size(); ++i) {
            const uint32_t s0 = std::rotr(schedule[i - 15U], 7) ^
                std::rotr(schedule[i - 15U], 18) ^
                (schedule[i - 15U] >> 3U);
            const uint32_t s1 = std::rotr(schedule[i - 2U], 17) ^
                std::rotr(schedule[i - 2U], 19) ^
                (schedule[i - 2U] >> 10U);
            schedule[i] = schedule[i - 16U] + s0 +
                schedule[i - 7U] + s1;
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];
        for (size_t i = 0; i < schedule.size(); ++i) {
            const uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^
                std::rotr(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t temporary1 = h + s1 + choose +
                roundConstants[i] + schedule[i];
            const uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^
                std::rotr(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temporary2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const uint32_t word : state)
        output << std::setw(8) << word;
    return output.str();
}
