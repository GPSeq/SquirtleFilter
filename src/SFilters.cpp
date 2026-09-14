#include "SFilters.h"

#include "BinaryIO.hpp"
#include "Hash.hpp"
#include "SquirtleFilter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace {

constexpr std::array<char, 8> collection_magic{'S', 'Q', 'S', 'F', 'I', 'L', 'T', '\0'};
constexpr std::uint32_t collection_format_version = 2;

std::size_t nextProbe(std::size_t current, std::size_t step, std::size_t modulus) noexcept {
    return current >= modulus - step ? current - (modulus - step) : current + step;
}

std::array<std::size_t, BloomFilter::max_hash_functions>
makeProbes(const void* key, std::size_t length, std::size_t bit_count, std::uint8_t hash_count) {
    std::uint64_t h1{};
    std::uint64_t h2{};
    squirtle::detail::hash128(key, length, 0, h1, h2);
    std::array<std::size_t, BloomFilter::max_hash_functions> probes{};
    std::size_t probe = static_cast<std::size_t>(h1 % bit_count);
    const std::size_t step = static_cast<std::size_t>(h2 % (bit_count - 1U)) + 1U;
    for (std::uint8_t i = 0; i < hash_count; ++i) {
        probes[i] = probe;
        probe = nextProbe(probe, step, bit_count);
    }
    return probes;
}

std::uint64_t loadAtomic(const std::uint64_t& value) {
    return std::atomic_ref<const std::uint64_t>(value).load(std::memory_order_relaxed);
}

} // namespace

std::size_t SFilters::filterWordCount() const noexcept {
    return squirtle::detail::wordsForBits(num_filters);
}

std::size_t SFilters::bitSliceOffset(std::size_t bit_index, std::size_t filter_word) const noexcept {
    return bit_index * filterWordCount() + filter_word;
}

void SFilters::validateIndex(std::size_t index) const {
    if (index >= num_filters) throw std::out_of_range("filter index is out of bounds");
}

void SFilters::initialize(std::size_t number_of_filters, std::size_t expected_items,
                          double target_rate, std::uint8_t hash_functions) {
    const std::size_t new_bit_count = BloomFilter::computeBitCount(expected_items, target_rate, hash_functions);
    const std::size_t word_count = squirtle::detail::wordsForBits(number_of_filters);
    const std::size_t storage_size = squirtle::detail::checkedMultiply(new_bit_count, word_count, "SFilters storage");
    std::vector<std::uint64_t> new_counts(number_of_filters, 0U);
    std::vector<std::uint64_t> new_slices(storage_size, 0U);

    std::unique_lock lock(mutex);
    num_filters = number_of_filters;
    bit_count = new_bit_count;
    capacity = expected_items;
    k = hash_functions;
    false_positive_rate = target_rate;
    item_counts = std::move(new_counts);
    bit_slices = std::move(new_slices);
}

void SFilters::insertBytesUnlocked(std::size_t index, const void* key, std::size_t length) {
    validateIndex(index);
    if (key == nullptr && length != 0U) throw std::invalid_argument("key must not be null");
    const auto probes = makeProbes(key, length, bit_count, k);
    const std::size_t filter_word = index / 64U;
    const std::uint64_t filter_mask = 1ULL << (index % 64U);
    for (std::uint8_t i = 0; i < k; ++i) {
        std::atomic_ref<std::uint64_t>(bit_slices[bitSliceOffset(probes[i], filter_word)])
            .fetch_or(filter_mask, std::memory_order_relaxed);
    }
    std::atomic_ref<std::uint64_t>(item_counts[index]).fetch_add(1U, std::memory_order_relaxed);
}

void SFilters::insert(std::size_t index, const std::string& key) {
    std::shared_lock lock(mutex);
    insertBytesUnlocked(index, key.data(), key.size());
}

void SFilters::insert(std::size_t index, double value) {
    std::shared_lock lock(mutex);
    insertBytesUnlocked(index, &value, sizeof(value));
}

void SFilters::insertMany(std::size_t index, const std::vector<std::string>& keys) {
    std::shared_lock lock(mutex);
    validateIndex(index);
    for (const auto& key : keys) insertBytesUnlocked(index, key.data(), key.size());
}

void SFilters::insertMany(std::size_t index, const std::vector<double>& values) {
    std::shared_lock lock(mutex);
    validateIndex(index);
    for (const double value : values) insertBytesUnlocked(index, &value, sizeof(value));
}

bool SFilters::containsBytesUnlocked(const void* key, std::size_t length) const {
    if (num_filters == 0U) return false;
    const auto probes = makeProbes(key, length, bit_count, k);
    const std::size_t words = filterWordCount();
    for (std::size_t filter_word = 0; filter_word < words; ++filter_word) {
        std::uint64_t candidates = std::numeric_limits<std::uint64_t>::max();
        for (std::uint8_t i = 0; i < k && candidates != 0U; ++i) {
            candidates &= loadAtomic(bit_slices[bitSliceOffset(probes[i], filter_word)]);
        }
        if (filter_word + 1U == words && num_filters % 64U != 0U) {
            candidates &= (1ULL << (num_filters % 64U)) - 1U;
        }
        if (candidates != 0U) return true;
    }
    return false;
}

bool SFilters::contains(const std::string& key) const {
    std::shared_lock lock(mutex);
    return containsBytesUnlocked(key.data(), key.size());
}

bool SFilters::contains(double value) const {
    std::shared_lock lock(mutex);
    return containsBytesUnlocked(&value, sizeof(value));
}

std::vector<std::size_t> SFilters::matchFiltersBytesUnlocked(const void* key, std::size_t length) const {
    std::vector<std::size_t> matches;
    if (num_filters == 0U) return matches;
    matches.reserve(std::min<std::size_t>(num_filters, 64U));
    const auto probes = makeProbes(key, length, bit_count, k);
    const std::size_t words = filterWordCount();
    for (std::size_t filter_word = 0; filter_word < words; ++filter_word) {
        std::uint64_t candidates = std::numeric_limits<std::uint64_t>::max();
        for (std::uint8_t i = 0; i < k && candidates != 0U; ++i) {
            candidates &= loadAtomic(bit_slices[bitSliceOffset(probes[i], filter_word)]);
        }
        while (candidates != 0U) {
            const unsigned bit = std::countr_zero(candidates);
            const std::size_t index = filter_word * 64U + bit;
            if (index < num_filters) matches.push_back(index);
            candidates &= candidates - 1U;
        }
    }
    return matches;
}

std::vector<std::size_t> SFilters::matchFilters(const std::string& key) const {
    std::shared_lock lock(mutex);
    return matchFiltersBytesUnlocked(key.data(), key.size());
}

std::vector<std::size_t> SFilters::matchFilters(double value) const {
    std::shared_lock lock(mutex);
    return matchFiltersBytesUnlocked(&value, sizeof(value));
}

std::vector<std::uint8_t> SFilters::matchBitVectorBytesUnlocked(const void* key, std::size_t length) const {
    std::vector<std::uint8_t> result(num_filters, 0U);
    if (num_filters == 0U) return result;
    const auto probes = makeProbes(key, length, bit_count, k);
    const std::size_t words = filterWordCount();
    for (std::size_t filter_word = 0; filter_word < words; ++filter_word) {
        std::uint64_t candidates = std::numeric_limits<std::uint64_t>::max();
        for (std::uint8_t i = 0; i < k && candidates != 0U; ++i) {
            candidates &= loadAtomic(bit_slices[bitSliceOffset(probes[i], filter_word)]);
        }
        while (candidates != 0U) {
            const unsigned bit = std::countr_zero(candidates);
            const std::size_t index = filter_word * 64U + bit;
            if (index < num_filters) result[index] = 1U;
            candidates &= candidates - 1U;
        }
    }
    return result;
}

std::vector<std::uint8_t> SFilters::matchBitVector(const std::string& key) const {
    std::shared_lock lock(mutex);
    return matchBitVectorBytesUnlocked(key.data(), key.size());
}

std::vector<std::uint8_t> SFilters::matchBitVector(double value) const {
    std::shared_lock lock(mutex);
    return matchBitVectorBytesUnlocked(&value, sizeof(value));
}

void SFilters::writeToFile(const std::string& output_path) const {
    std::size_t stored_num_filters{};
    std::size_t stored_bit_count{};
    std::size_t stored_capacity{};
    std::uint8_t stored_k{};
    double stored_rate{};
    std::vector<std::uint64_t> counts;
    std::vector<std::uint64_t> slices;
    {
        std::shared_lock lock(mutex);
        if (bit_count == 0U) throw std::logic_error("SFilters must be initialized before writing");
        stored_num_filters = num_filters;
        stored_bit_count = bit_count;
        stored_capacity = capacity;
        stored_k = k;
        stored_rate = false_positive_rate;
        counts.resize(item_counts.size());
        slices.resize(bit_slices.size());
        for (std::size_t i = 0; i < counts.size(); ++i) counts[i] = loadAtomic(item_counts[i]);
        for (std::size_t i = 0; i < slices.size(); ++i) slices[i] = loadAtomic(bit_slices[i]);
    }

    squirtle::detail::writeAtomically(output_path, [&](std::ostream& output) {
        squirtle::detail::writeMagic(output, collection_magic);
        squirtle::detail::writeUnsigned(output, collection_format_version);
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(stored_num_filters));
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(stored_bit_count));
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(stored_capacity));
        squirtle::detail::writeUnsigned(output, stored_k);
        squirtle::detail::writeDouble(output, stored_rate);
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(counts.size()));
        squirtle::detail::writeUnsigned(output, static_cast<std::uint64_t>(slices.size()));
        for (const auto count : counts) squirtle::detail::writeUnsigned(output, count);
        for (const auto word : slices) squirtle::detail::writeUnsigned(output, word);
    });
}

void SFilters::loadFromFile(const std::string& input_path) {
    std::ifstream input(input_path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open filter collection: " + input_path);
    squirtle::detail::requireMagic(input, collection_magic);
    if (squirtle::detail::readUnsigned<std::uint32_t>(input) != collection_format_version) {
        throw std::runtime_error("Unsupported SFilters format version");
    }
    const std::size_t loaded_filters = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "num_filters");
    const std::size_t loaded_bits = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "bit_count");
    const std::size_t loaded_capacity = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "capacity");
    const std::uint8_t loaded_k = squirtle::detail::readUnsigned<std::uint8_t>(input);
    const double loaded_rate = squirtle::detail::readDouble(input);
    const std::size_t count_size = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "item_count length");
    const std::size_t slice_size = squirtle::detail::checkedSize(squirtle::detail::readUnsigned<std::uint64_t>(input), "bit-slice length");

    const std::size_t calculated_bits = BloomFilter::computeBitCount(loaded_capacity, loaded_rate, loaded_k);
    const std::size_t filter_words = squirtle::detail::wordsForBits(loaded_filters);
    const std::size_t expected_slices = squirtle::detail::checkedMultiply(loaded_bits, filter_words, "loaded SFilters storage");
    if (loaded_bits != calculated_bits || count_size != loaded_filters || slice_size != expected_slices) {
        throw std::runtime_error("Invalid SFilters dimensions");
    }
    const std::size_t total_words = count_size > std::numeric_limits<std::size_t>::max() - slice_size
                                        ? throw std::length_error("loaded SFilters data is too large")
                                        : count_size + slice_size;
    const std::size_t byte_count = squirtle::detail::checkedMultiply(total_words, sizeof(std::uint64_t), "loaded SFilters payload");
    const auto payload_start = input.tellg();
    input.seekg(0, std::ios::end);
    const auto payload_end = input.tellg();
    if (payload_start < 0 || payload_end < payload_start ||
        static_cast<std::uintmax_t>(payload_end - payload_start) != byte_count) {
        throw std::runtime_error("Truncated or oversized SFilters payload");
    }
    input.seekg(payload_start);

    std::vector<std::uint64_t> counts(count_size);
    std::vector<std::uint64_t> slices(slice_size);
    for (auto& count : counts) count = squirtle::detail::readUnsigned<std::uint64_t>(input);
    for (auto& word : slices) word = squirtle::detail::readUnsigned<std::uint64_t>(input);

    std::unique_lock lock(mutex);
    num_filters = loaded_filters;
    bit_count = loaded_bits;
    capacity = loaded_capacity;
    k = loaded_k;
    false_positive_rate = loaded_rate;
    item_counts = std::move(counts);
    bit_slices = std::move(slices);
}

std::size_t SFilters::getFilterCount() const {
    std::shared_lock lock(mutex);
    return num_filters;
}

void SFilters::printSummary() const {
    std::shared_lock lock(mutex);
    std::uint64_t total_items{};
    std::uint64_t min_items = item_counts.empty() ? 0U : std::numeric_limits<std::uint64_t>::max();
    std::uint64_t max_items{};
    for (const auto& count : item_counts) {
        const auto value = loadAtomic(count);
        total_items += value;
        min_items = std::min(min_items, value);
        max_items = std::max(max_items, value);
    }
    std::cout << "=== SFilters Summary ===\n"
              << "Number of filters     : " << num_filters << '\n'
              << "Bit count per filter  : " << bit_count << '\n'
              << "Capacity per filter   : " << capacity << '\n'
              << "Hash functions (k)    : " << static_cast<unsigned>(k) << '\n'
              << "False positive rate   : " << false_positive_rate << '\n'
              << "Total items           : " << total_items << '\n'
              << "Storage words         : " << bit_slices.size() << '\n'
              << "Per-filter items      : min " << min_items << " / max " << max_items << '\n'
              << "========================\n";
}
