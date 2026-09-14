#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace squirtle::detail {

template <typename UInt>
void writeUnsigned(std::ostream& output, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    std::array<char, sizeof(UInt)> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>((value >> (i * 8U)) & static_cast<UInt>(0xffU));
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Failed while writing filter data");
}

template <typename UInt>
UInt readUnsigned(std::istream& input) {
    static_assert(std::is_unsigned_v<UInt>);
    std::array<unsigned char, sizeof(UInt)> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Truncated filter file");
    std::uintmax_t value{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uintmax_t>(bytes[i]) << (i * 8U);
    }
    return static_cast<UInt>(value);
}

inline void writeDouble(std::ostream& output, double value) {
    writeUnsigned(output, std::bit_cast<std::uint64_t>(value));
}

inline double readDouble(std::istream& input) {
    return std::bit_cast<double>(readUnsigned<std::uint64_t>(input));
}

template <std::size_t N>
void writeMagic(std::ostream& output, const std::array<char, N>& magic) {
    output.write(magic.data(), static_cast<std::streamsize>(N));
    if (!output) throw std::runtime_error("Failed while writing filter header");
}

template <std::size_t N>
void requireMagic(std::istream& input, const std::array<char, N>& expected) {
    std::array<char, N> actual{};
    input.read(actual.data(), static_cast<std::streamsize>(N));
    if (!input || actual != expected) {
        throw std::runtime_error("Unsupported or corrupt filter file header");
    }
}

inline std::size_t checkedSize(std::uint64_t value, const char* field) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error(std::string(field) + " does not fit this platform");
    }
    return static_cast<std::size_t>(value);
}

inline std::size_t checkedMultiply(std::size_t left, std::size_t right, const char* field) {
    if (right != 0U && left > std::numeric_limits<std::size_t>::max() / right) {
        throw std::length_error(std::string(field) + " is too large");
    }
    return left * right;
}

inline std::size_t wordsForBits(std::size_t bits) noexcept {
    return bits / 64U + static_cast<std::size_t>(bits % 64U != 0U);
}

template <typename Writer>
void writeAtomically(const std::string& output_path, Writer writer) {
    const std::filesystem::path destination(output_path);
    const std::filesystem::path temporary = destination.string() + ".tmp";
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot open temporary output file: " + temporary.string());
        writer(output);
        output.flush();
        if (!output) throw std::runtime_error("Failed to flush filter file: " + temporary.string());
    } catch (...) {
        std::filesystem::remove(temporary);
        throw;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot replace filter file (Windows error " +
                                 std::to_string(error) + ")");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot replace filter file: " + error.message());
    }
#endif
}

} // namespace squirtle::detail
