#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace squirtle::detail {

inline std::uint64_t rotateLeft(std::uint64_t value, unsigned shift) noexcept {
    return (value << shift) | (value >> (64U - shift));
}

inline std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return value;
}

inline void hash128(const void* key, std::size_t length, std::uint64_t seed,
                    std::uint64_t& out1, std::uint64_t& out2) {
    const auto* data = static_cast<const std::uint8_t*>(key);
    const std::size_t block_count = length / 16U;
    std::uint64_t h1 = seed;
    std::uint64_t h2 = seed;
    constexpr std::uint64_t c1 = 0x87c37b91114253d5ULL;
    constexpr std::uint64_t c2 = 0x4cf5ad432745937fULL;

    for (std::size_t i = 0; i < block_count; ++i) {
        std::uint64_t k1{};
        std::uint64_t k2{};
        std::memcpy(&k1, data + (2U * i) * sizeof(std::uint64_t), sizeof(k1));
        std::memcpy(&k2, data + (2U * i + 1U) * sizeof(std::uint64_t), sizeof(k2));

        k1 *= c1;
        k1 = rotateLeft(k1, 31U);
        k1 *= c2;
        h1 ^= k1;
        h1 = rotateLeft(h1, 27U);
        h1 = h1 + h2;
        h1 = h1 * 5U + 0x52dce729U;

        k2 *= c2;
        k2 = rotateLeft(k2, 33U);
        k2 *= c1;
        h2 ^= k2;
        h2 = rotateLeft(h2, 31U);
        h2 = h2 + h1;
        h2 = h2 * 5U + 0x38495ab5U;
    }

    const auto* tail = data == nullptr ? nullptr : data + block_count * 16U;
    std::uint64_t k1{};
    std::uint64_t k2{};
    switch (length & 15U) {
    case 15: k2 ^= static_cast<std::uint64_t>(tail[14]) << 48U; [[fallthrough]];
    case 14: k2 ^= static_cast<std::uint64_t>(tail[13]) << 40U; [[fallthrough]];
    case 13: k2 ^= static_cast<std::uint64_t>(tail[12]) << 32U; [[fallthrough]];
    case 12: k2 ^= static_cast<std::uint64_t>(tail[11]) << 24U; [[fallthrough]];
    case 11: k2 ^= static_cast<std::uint64_t>(tail[10]) << 16U; [[fallthrough]];
    case 10: k2 ^= static_cast<std::uint64_t>(tail[9]) << 8U; [[fallthrough]];
    case 9:
        k2 ^= static_cast<std::uint64_t>(tail[8]);
        k2 *= c2;
        k2 = rotateLeft(k2, 33U);
        k2 *= c1;
        h2 ^= k2;
        [[fallthrough]];
    case 8: k1 ^= static_cast<std::uint64_t>(tail[7]) << 56U; [[fallthrough]];
    case 7: k1 ^= static_cast<std::uint64_t>(tail[6]) << 48U; [[fallthrough]];
    case 6: k1 ^= static_cast<std::uint64_t>(tail[5]) << 40U; [[fallthrough]];
    case 5: k1 ^= static_cast<std::uint64_t>(tail[4]) << 32U; [[fallthrough]];
    case 4: k1 ^= static_cast<std::uint64_t>(tail[3]) << 24U; [[fallthrough]];
    case 3: k1 ^= static_cast<std::uint64_t>(tail[2]) << 16U; [[fallthrough]];
    case 2: k1 ^= static_cast<std::uint64_t>(tail[1]) << 8U; [[fallthrough]];
    case 1:
        k1 ^= static_cast<std::uint64_t>(tail[0]);
        k1 *= c1;
        k1 = rotateLeft(k1, 31U);
        k1 *= c2;
        h1 ^= k1;
        [[fallthrough]];
    case 0: break;
    }

    h1 ^= length;
    h2 ^= length;
    h1 += h2;
    h2 += h1;
    h1 = mix(h1);
    h2 = mix(h2);
    h1 += h2;
    h2 += h1;
    out1 = h1;
    out2 = h2;
}

} // namespace squirtle::detail
