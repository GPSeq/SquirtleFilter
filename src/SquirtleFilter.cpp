#include "SquirtleFilter.h"

#include "BinaryIO.hpp"
#include "Hash.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace {

constexpr std::array<char, 8> bloom_magic{'S', 'Q', 'B', 'F', 'I', 'L', 'T', '\0'};
constexpr std::uint32_t bloom_format_version = 1;

std::size_t nextProbe(std::size_t current, std::size_t step, std::size_t modulus) noexcept {
    return current >= modulus - step ? current - (modulus - step) : current + step;
}

std::shared_ptr<BloomFilter::SQFilter> makeAtomicBits(const std::vector<std::uint64_t>& raw) {
    auto result = std::make_shared<BloomFilter::SQFilter>(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        (*result)[i].store(raw[i], std::memory_order_relaxed);
    }
    return result;
}

} // namespace

double BloomFilter::validateFalsePositiveRate(double rate) {
    if (!std::isfinite(rate) || rate <= 0.0 || rate >= 1.0) {
        throw std::invalid_argument("false_positive_rate must be finite and between 0 and 1");
    }
    return rate;
}

std::uint8_t BloomFilter::validateHashCount(std::uint8_t count) {
    if (count == 0 || count > max_hash_functions) {
        throw std::invalid_argument("hash_functions must be between 1 and 32");
    }
    return count;
}

void BloomFilter::validateKey(const void* key, std::size_t length) {
    if (key == nullptr && length != 0U) {
        throw std::invalid_argument("key must not be null when length is non-zero");
    }
}

std::size_t BloomFilter::computeBitCount(std::size_t expected_items, double false_positive_rate,
                                         std::uint8_t hash_functions) {
    if (expected_items == 0U) throw std::invalid_argument("expected_items must be greater than zero");
    const double rate = validateFalsePositiveRate(false_positive_rate);
    const std::uint8_t hashes = validateHashCount(hash_functions);

    const long double k_value = hashes;
    const long double probability = rate;
    const long double denominator = std::log1p(-std::pow(probability, 1.0L / k_value));
    const long double calculated = -k_value * static_cast<long double>(expected_items) / denominator;
    if (!std::isfinite(calculated) || calculated > static_cast<long double>(std::numeric_limits<std::size_t>::max() - 63U)) {
        throw std::length_error("requested Bloom filter is too large");
    }
    return std::max<std::size_t>(64U, static_cast<std::size_t>(std::ceil(calculated)));
}

BloomFilter::BloomFilter(std::size_t expected_items, double false_positive_rate,
                         std::uint8_t hash_functions)
    : bit_count(computeBitCount(expected_items, false_positive_rate, hash_functions)),
      capacity(expected_items),
      item_count(0),
      k(validateHashCount(hash_functions)),
      target_false_positive(validateFalsePositiveRate(false_positive_rate)) {
    const std::size_t word_count = squirtle::detail::wordsForBits(bit_count);
    bits = std::make_shared<SQFilter>(word_count);
    for (auto& word : *bits) word.store(0, std::memory_order_relaxed);
}

void BloomFilter::insertUnlocked(const void* key, std::size_t length) {
    std::uint64_t h1{};
    std::uint64_t h2{};
    squirtle::detail::hash128(key, length, 0, h1, h2);
    std::size_t probe = static_cast<std::size_t>(h1 % bit_count);
    const std::size_t step = static_cast<std::size_t>(h2 % (bit_count - 1U)) + 1U;
    for (std::uint8_t i = 0; i < k; ++i) {
        (*bits)[probe / 64U].fetch_or(1ULL << (probe % 64U), std::memory_order_relaxed);
        probe = nextProbe(probe, step, bit_count);
    }
    item_count.fetch_add(1, std::memory_order_relaxed);
}

void BloomFilter::insert(const void* key, std::size_t length) {
    validateKey(key, length);
    std::shared_lock lock(mutex);
    insertUnlocked(key, length);
}

void BloomFilter::insertMany(const std::vector<std::string>& keys) {
    std::shared_lock lock(mutex);
    for (const auto& key : keys) insertUnlocked(key.data(), key.size());
}

void BloomFilter::insertMany(const std::vector<double>& values) {
    std::shared_lock lock(mutex);
    for (const double value : values) insertUnlocked(&value, sizeof(value));
}

bool BloomFilter::containsUnlocked(const void* key, std::size_t length) const {
    std::uint64_t h1{};
    std::uint64_t h2{};
    squirtle::detail::hash128(key, length, 0, h1, h2);
    std::size_t probe = static_cast<std::size_t>(h1 % bit_count);
    const std::size_t step = static_cast<std::size_t>(h2 % (bit_count - 1U)) + 1U;
    for (std::uint8_t i = 0; i < k; ++i) {
        const auto word = (*bits)[probe / 64U].load(std::memory_order_relaxed);
        if ((word & (1ULL << (probe % 64U))) == 0U) return false;
        probe = nextProbe(probe, step, bit_count);
    }
    return true;
}

bool BloomFilter::contains(const void* key, std::size_t length) const {
    validateKey(key, length);
    std::shared_lock lock(mutex);
    return containsUnlocked(key, length);
}

std::vector<std::uint8_t> BloomFilter::containsMany(const std::vector<std::string>& keys) const {
    std::shared_lock lock(mutex);
    std::vector<std::uint8_t> result(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        result[i] = static_cast<std::uint8_t>(containsUnlocked(keys[i].data(), keys[i].size()));
    }
    return result;
}

std::vector<std::uint8_t> BloomFilter::containsMany(const std::vector<double>& values) const {
    std::shared_lock lock(mutex);
    std::vector<std::uint8_t> result(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        result[i] = static_cast<std::uint8_t>(containsUnlocked(&values[i], sizeof(values[i])));
    }
    return result;
}

BloomFilter& BloomFilter::operator=(BloomFilter&& other) noexcept {
    if (this == &other) return *this;
    std::scoped_lock lock(mutex, other.mutex);
    bit_count = other.bit_count;
    bits = std::move(other.bits);
    capacity = other.capacity;
    item_count.store(other.item_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    k = other.k;
    target_false_positive = other.target_false_positive;
    other.bit_count = 0;
    other.capacity = 0;
    other.k = 0;
    other.item_count.store(0, std::memory_order_relaxed);
    return *this;
}

BloomFilter::BloomFilter(BloomFilter&& other) noexcept {
    std::unique_lock lock(other.mutex);
    bit_count = other.bit_count;
    bits = std::move(other.bits);
    capacity = other.capacity;
    item_count.store(other.item_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    k = other.k;
    target_false_positive = other.target_false_positive;
    other.bit_count = 0;
    other.capacity = 0;
    other.k = 0;
    other.item_count.store(0, std::memory_order_relaxed);
}

BloomFilter::SQFilterRAW BloomFilter::returnFilter() const {
    std::shared_lock lock(mutex);
    SQFilterRAW snapshot(bits->size());
    for (std::size_t i = 0; i < bits->size(); ++i) {
        snapshot[i] = (*bits)[i].load(std::memory_order_relaxed);
    }
    return snapshot;
}

void BloomFilter::clear() {
    std::unique_lock lock(mutex);
    for (auto& word : *bits) word.store(0, std::memory_order_relaxed);
    item_count.store(0, std::memory_order_relaxed);
}

BloomFilter::BloomFilterData BloomFilter::exportData() const {
    std::shared_lock lock(mutex);
    BloomFilterData data{bit_count, capacity, item_count.load(std::memory_order_relaxed),
                         k, target_false_positive, {}};
    data.bits.resize(bits->size());
    for (std::size_t i = 0; i < bits->size(); ++i) {
        data.bits[i] = (*bits)[i].load(std::memory_order_relaxed);
    }
    return data;
}

void BloomFilter::importData(const BloomFilterData& data) {
    validateFalsePositiveRate(data.false_positive_rate);
    validateHashCount(data.hash_functions);
    if (data.capacity == 0U || data.bit_count < 64U) {
        throw std::invalid_argument("invalid Bloom filter metadata");
    }
    const std::size_t expected_words = squirtle::detail::wordsForBits(data.bit_count);
    if (data.bits.size() != expected_words) {
        throw std::invalid_argument("bit-vector size does not match bit_count");
    }
    auto new_bits = makeAtomicBits(data.bits);

    std::unique_lock lock(mutex);
    bit_count = data.bit_count;
    capacity = data.capacity;
    item_count.store(data.item_count, std::memory_order_relaxed);
    k = data.hash_functions;
    target_false_positive = data.false_positive_rate;
    bits = std::move(new_bits);
}

void BloomFilter::writeSQFilter(const std::string& output_path) const {
    const BloomFilterData data = exportData();
    squirtle::detail::writeAtomically(output_path, [&](std::ostream& output) {
        squirtle::detail::writeMagic(output, bloom_magic);
        squirtle::detail::writeUnsigned(output, bloom_format_version);
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(data.bit_count));
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(data.capacity));
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(data.item_count));
        squirtle::detail::writeUnsigned(output, data.hash_functions);
        squirtle::detail::writeDouble(output, data.false_positive_rate);
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(data.bits.size()));
        for (const auto word : data.bits) squirtle::detail::writeUnsigned(output, word);
    });
}

void BloomFilter::loadSQFilter(const std::string& input_path) {
    std::ifstream input(input_path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open filter file: " + input_path);
    squirtle::detail::requireMagic(input, bloom_magic);
    if (squirtle::detail::readUnsigned<std::uint32_t>(input) != bloom_format_version) {
        throw std::runtime_error("Unsupported Bloom filter format version");
    }
    BloomFilterData data;
    data.bit_count = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "bit_count");
    data.capacity = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "capacity");
    data.item_count = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "item_count");
    data.hash_functions = squirtle::detail::readUnsigned<std::uint8_t>(input);
    data.false_positive_rate = squirtle::detail::readDouble(input);
    const auto words = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "word_count");
    const std::size_t expected_words = squirtle::detail::wordsForBits(data.bit_count);
    if (words == 0U || words != expected_words) throw std::runtime_error("Invalid Bloom filter dimensions");
    const std::size_t byte_count = squirtle::detail::checkedMultiply(words, sizeof(std::uint64_t), "Bloom filter payload");
    const auto payload_start = input.tellg();
    input.seekg(0, std::ios::end);
    const auto payload_end = input.tellg();
    if (payload_start < 0 || payload_end < payload_start ||
        static_cast<std::uintmax_t>(payload_end - payload_start) != byte_count) {
        throw std::runtime_error("Truncated or oversized Bloom filter payload");
    }
    input.seekg(payload_start);
    data.bits.resize(words);
    for (auto& word : data.bits) word = squirtle::detail::readUnsigned<std::uint64_t>(input);
    importData(data);
}

void BloomFilter::printSummary() const {
    const BloomFilterData data = exportData();
    std::size_t set_bits{};
    for (const auto word : data.bits) set_bits += std::popcount(word);
    std::cout << "=== Bloom Filter Summary ===\n"
              << "Bit count             : " << data.bit_count << '\n'
              << "Capacity              : " << data.capacity << '\n'
              << "Item count            : " << data.item_count << '\n'
              << "Hash functions (k)    : " << static_cast<unsigned>(data.hash_functions) << '\n'
              << "False positive rate   : " << data.false_positive_rate << '\n'
              << "Bit vector size       : " << data.bits.size() << " words\n"
              << "Total bits set        : " << set_bits << '\n'
              << "============================\n";
}
