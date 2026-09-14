#include "SFilters.h"
#include <cereal/archives/binary.hpp>
#include <cstring>
#include <stdexcept>

namespace {

/**
 * @brief Performs a 64-bit left bitwise rotation.
 *
 * This helper mirrors the Bloom filter hashing path so that `SFilters`
 * can compute probe locations once and reuse them across all interleaved
 * filters in the collection.
 *
 * @param x The 64-bit unsigned integer to be rotated.
 * @param r The number of positions to rotate `x` to the left.
 * @return The result of rotating `x` left by `r` positions.
 */
static inline uint64_t rotl64(uint64_t x, int8_t r) {
    return (x << r) | (x >> (64 - r));
}

/**
 * @brief Finalizes a 64-bit hash value using a series of bitwise operations and multiplications.
 *
 * This helper uses the same MurmurHash3 finalization routine as the
 * single-filter implementation, ensuring probe compatibility between
 * `BloomFilter` and `SFilters`.
 *
 * @param k The 64-bit unsigned integer hash key to be finalized.
 * @return The 64-bit finalized hash value.
 */
static inline uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

/**
 * @brief Computes a 128-bit MurmurHash3 hash for the given key.
 *
 * This local helper allows the interleaved `SFilters` structure to
 * derive the same logical probe positions used by `BloomFilter`.
 *
 * @param key A pointer to the data buffer to be hashed.
 * @param len The length of the data buffer in bytes.
 * @param seed The seed value for the hash function.
 * @param out1 Stores the first 64 bits of the 128-bit hash result.
 * @param out2 Stores the second 64 bits of the 128-bit hash result.
 */
void hash128(const void* key, size_t len, uint64_t seed, uint64_t& out1, uint64_t& out2) {
    const uint8_t* data = static_cast<const uint8_t*>(key);
    const int nblocks = len / 16;

    uint64_t h1 = seed;
    uint64_t h2 = seed;

    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    for (int i = 0; i < nblocks; ++i) {
        uint64_t k1;
        uint64_t k2;
        std::memcpy(&k1, data + (2 * i) * sizeof(uint64_t), sizeof(uint64_t));
        std::memcpy(&k2, data + (2 * i + 1) * sizeof(uint64_t), sizeof(uint64_t));

        k1 *= c1;
        k1 = rotl64(k1, 31);
        k1 *= c2;
        h1 ^= k1;

        h1 = rotl64(h1, 27);
        h1 += h2;
        h1 = h1 * 5 + 0x52dce729;

        k2 *= c2;
        k2 = rotl64(k2, 33);
        k2 *= c1;
        h2 ^= k2;

        h2 = rotl64(h2, 31);
        h2 += h1;
        h2 = h2 * 5 + 0x38495ab5;
    }

    const uint8_t* tail = data + nblocks * 16;
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    switch (len & 15) {
    case 15: k2 ^= (uint64_t)tail[14] << 48;
    case 14: k2 ^= (uint64_t)tail[13] << 40;
    case 13: k2 ^= (uint64_t)tail[12] << 32;
    case 12: k2 ^= (uint64_t)tail[11] << 24;
    case 11: k2 ^= (uint64_t)tail[10] << 16;
    case 10: k2 ^= (uint64_t)tail[9] << 8;
    case 9:  k2 ^= (uint64_t)tail[8] << 0;
             k2 *= c2;
             k2 = rotl64(k2, 33);
             k2 *= c1;
             h2 ^= k2;
    case 8:  k1 ^= (uint64_t)tail[7] << 56;
    case 7:  k1 ^= (uint64_t)tail[6] << 48;
    case 6:  k1 ^= (uint64_t)tail[5] << 40;
    case 5:  k1 ^= (uint64_t)tail[4] << 32;
    case 4:  k1 ^= (uint64_t)tail[3] << 24;
    case 3:  k1 ^= (uint64_t)tail[2] << 16;
    case 2:  k1 ^= (uint64_t)tail[1] << 8;
    case 1:  k1 ^= (uint64_t)tail[0] << 0;
             k1 *= c1;
             k1 = rotl64(k1, 31);
             k1 *= c2;
             h1 ^= k1;
    }

    h1 ^= len;
    h2 ^= len;
    h1 += h2;
    h2 += h1;
    h1 = fmix64(h1);
    h2 = fmix64(h2);
    h1 += h2;
    h2 += h1;
    out1 = h1;
    out2 = h2;
}

} // namespace

/**
 * @brief Returns the number of 64-bit words required for each filter.
 *
 * The interleaved representation stores the same logical Bloom-filter
 * bit layout for every filter, but stripes words by filter index for
 * improved locality during multi-filter queries.
 *
 * @return The number of 64-bit words required for one filter.
 */
size_t SFilters::wordCountPerFilter() const {
    return (bit_count + 63) / 64;
}

/**
 * @brief Returns the physical offset for an interleaved word.
 *
 * Storage is arranged as [word0 filter0..N][word1 filter0..N]... so
 * that the same logical probe across all filters stays tightly packed.
 *
 * @param word_index The logical word index within a single filter.
 * @param filter_index The filter index inside the collection.
 * @return The physical offset in the interleaved word buffer.
 */
size_t SFilters::interleavedOffset(size_t word_index, size_t filter_index) const {
    return word_index * num_filters + filter_index;
}

/**
 * @brief Checks whether a specific bit mask is set for a given filter word.
 *
 * This helper keeps the interleaved indexing logic in one place.
 *
 * @param filter_index The target filter index.
 * @param word_index The target word index.
 * @param bit_mask The bit mask to test.
 * @return `true` if the bit is set, `false` otherwise.
 */
bool SFilters::matchWord(size_t filter_index, size_t word_index, uint64_t bit_mask) const {
    return (interleaved_bits[interleavedOffset(word_index, filter_index)] & bit_mask) != 0;
}

/**
 * @brief Sets a bit mask for a given filter word in the interleaved layout.
 *
 * @param filter_index The target filter index.
 * @param word_index The logical word index within the filter.
 * @param bit_mask The bit mask to set.
 */
void SFilters::setWord(size_t filter_index, size_t word_index, uint64_t bit_mask) {
    interleaved_bits[interleavedOffset(word_index, filter_index)] |= bit_mask;
}

/**
 * @brief Validates that a requested filter index is inside the collection bounds.
 *
 * @param index The filter index to validate.
 * @throws std::out_of_range If the index falls outside the collection.
 */
void SFilters::validateIndex(size_t index) const {
    if (index >= num_filters) throw std::out_of_range("insert: index out of bounds");
}

/**
 * @brief Initializes a collection of interleaved Bloom filters.
 *
 * This method allocates a single interleaved bit store where each logical
 * Bloom-filter word is packed across all filters before moving to the next
 * word. That layout improves locality for collection-wide queries.
 *
 * @param number_of_filters The total number of Bloom filter instances to create and manage.
 * @param expected_items The approximate maximum number of items each individual Bloom filter is expected to hold.
 * @param target_false_positive_rate The desired maximum false positive rate for each Bloom filter.
 * @param hash_functions The number of hash functions to be used by each Bloom filter.
 */
void SFilters::initialize(size_t number_of_filters, size_t expected_items, double target_false_positive_rate, uint8_t hash_functions) {
    num_filters = number_of_filters;
    capacity = expected_items == 0 ? 1 : expected_items;
    false_positive_rate = target_false_positive_rate;
    k = std::clamp<uint8_t>(hash_functions, 1, 5);
    bit_count = BloomFilter::computeBitCount(capacity, false_positive_rate);

    item_counts.assign(num_filters, 0);
    interleaved_bits.assign(wordCountPerFilter() * num_filters, 0);
}

/**
 * @brief Inserts a key into a specific interleaved Bloom filter.
 *
 * This method hashes the key once, derives all probe locations, and then
 * applies the resulting bit masks to the selected filter inside the shared
 * interleaved bit layout.
 *
 * @param index The zero-based index of the Bloom filter in the collection where the key should be inserted.
 * @param key The string key to be inserted into the specified Bloom filter.
 * @throws std::out_of_range If the provided `index` is greater than or equal to the number of filters.
 */
void SFilters::insert(size_t index, const std::string& key) {
    validateIndex(index);

    uint64_t h1, h2;
    hash128(key.data(), key.size(), 0, h1, h2);
    const uint64_t base_index = h1 % bit_count;
    const uint64_t hash2_mod = h2 % bit_count;
    for (uint8_t i = 0; i < k; ++i) {
        const uint64_t bit_index = (base_index + i * hash2_mod) % bit_count;
        const size_t word_index = bit_index / 64;
        const uint64_t bit_mask = 1ULL << (bit_index % 64);
        setWord(index, word_index, bit_mask);
    }
    ++item_counts[index];
}

/**
 * @brief Inserts a double-precision floating-point value into a specific interleaved Bloom filter.
 *
 * This overload stores the raw binary representation of the double in the
 * same way as the single-filter implementation.
 *
 * @param index The zero-based index of the Bloom filter in the collection where the double value should be inserted.
 * @param value The double-precision floating-point value to be inserted.
 * @throws std::out_of_range If the provided `index` is greater than or equal to the number of filters.
 */
void SFilters::insert(size_t index, double value) {
    validateIndex(index);

    uint64_t h1, h2;
    hash128(&value, sizeof(double), 0, h1, h2);
    const uint64_t base_index = h1 % bit_count;
    const uint64_t hash2_mod = h2 % bit_count;
    for (uint8_t i = 0; i < k; ++i) {
        const uint64_t bit_index = (base_index + i * hash2_mod) % bit_count;
        const size_t word_index = bit_index / 64;
        const uint64_t bit_mask = 1ULL << (bit_index % 64);
        setWord(index, word_index, bit_mask);
    }
    ++item_counts[index];
}

/**
 * @brief Writes the entire interleaved collection of Bloom filters to a single binary file.
 *
 * This method serializes the collection metadata and the full interleaved
 * bit store so that the layout can be reconstructed exactly during load.
 *
 * @param output_path The file path where the serialized collection of Bloom filters will be written.
 * @throws std::runtime_error If the output file cannot be opened for writing.
 */
void SFilters::writeToFile(const std::string& output_path) const {
    std::ofstream ofs(output_path, std::ios::binary);
    if (!ofs) throw std::runtime_error("Cannot open output file: " + output_path);

    cereal::BinaryOutputArchive archive(ofs);
    SFiltersData data{num_filters, bit_count, capacity, k, false_positive_rate, item_counts, interleaved_bits};
    archive(data);
}

/**
 * @brief Loads an interleaved collection of Bloom filters from a binary file.
 *
 * This method restores the exact interleaved storage layout, item counts,
 * and filter parameters written by `writeToFile`.
 *
 * @param input_path The file path from which the serialized collection of Bloom filters will be read.
 * @throws std::runtime_error If the input file cannot be opened for reading.
 */
void SFilters::loadFromFile(const std::string& input_path) {
    std::ifstream ifs(input_path, std::ios::binary);
    if (!ifs) throw std::runtime_error("Cannot open input file: " + input_path);

    cereal::BinaryInputArchive archive(ifs);
    SFiltersData data;
    archive(data);

    num_filters = data.num_filters;
    bit_count = data.bit_count;
    capacity = data.capacity;
    k = data.hash_functions;
    false_positive_rate = data.false_positive_rate;
    item_counts = std::move(data.item_counts);
    interleaved_bits = std::move(data.interleaved_bits);
}

/**
 * @brief Returns the number of filters stored in the collection.
 *
 * @return The total number of filters in the interleaved collection.
 */
size_t SFilters::getFilterCount() const {
    return num_filters;
}

/**
 * @brief Checks the presence of a key across all Bloom filters in the collection.
 *
 * This method delegates to `matchBitVector` so that the shared probe
 * computation happens only once before scanning the collection-wide
 * match result.
 *
 * @param key The string key to check for presence across the filters.
 * @return `true` if the key might be present in at least one filter, `false` otherwise.
 */
std::vector<int> SFilters::matchBitVector(const std::string& key) const {
    std::vector<int> presence(num_filters, 1);
    if (num_filters == 0) return presence;

    uint64_t h1, h2;
    hash128(key.data(), key.size(), 0, h1, h2);
    const uint64_t base_index = h1 % bit_count;
    const uint64_t hash2_mod = h2 % bit_count;

    for (uint8_t i = 0; i < k; ++i) {
        const uint64_t bit_index = (base_index + i * hash2_mod) % bit_count;
        const size_t word_index = bit_index / 64;
        const uint64_t bit_mask = 1ULL << (bit_index % 64);

        for (size_t filter_index = 0; filter_index < num_filters; ++filter_index) {
            if (presence[filter_index] == 1 && !matchWord(filter_index, word_index, bit_mask)) {
                presence[filter_index] = 0;
            }
        }
    }

    return presence;
}

/**
 * @brief Checks the presence of a double value across all Bloom filters in the collection.
 *
 * This overload performs the same shared-probe scan as the string overload,
 * but hashes the raw binary representation of the floating-point input.
 *
 * @param value The double-precision floating-point value to check for presence across the filters.
 * @return A `std::vector<int>` where each element is 1 if the `value` might be present in the corresponding filter, and 0 otherwise.
 */
std::vector<int> SFilters::matchBitVector(double value) const {
    std::vector<int> presence(num_filters, 1);
    if (num_filters == 0) return presence;

    uint64_t h1, h2;
    hash128(&value, sizeof(double), 0, h1, h2);
    const uint64_t base_index = h1 % bit_count;
    const uint64_t hash2_mod = h2 % bit_count;

    for (uint8_t i = 0; i < k; ++i) {
        const uint64_t bit_index = (base_index + i * hash2_mod) % bit_count;
        const size_t word_index = bit_index / 64;
        const uint64_t bit_mask = 1ULL << (bit_index % 64);

        for (size_t filter_index = 0; filter_index < num_filters; ++filter_index) {
            if (presence[filter_index] == 1 && !matchWord(filter_index, word_index, bit_mask)) {
                presence[filter_index] = 0;
            }
        }
    }

    return presence;
}

/**
 * @brief Prints a summary of the interleaved filter collection.
 *
 * This summary reports collection-level totals and simple per-filter
 * statistics derived from the interleaved storage metadata.
 */
void SFilters::printSummary() const {
    std::cout << "=== SFilters Summary ===\n";
    std::cout << "Number of filters     : " << num_filters << '\n';

    if (num_filters == 0) {
        std::cout << "Collection is empty.\n";
        std::cout << "========================\n";
        return;
    }

    size_t total_items = 0;
    size_t min_items = std::numeric_limits<size_t>::max();
    size_t max_items = 0;

    for (size_t count : item_counts) {
        total_items += count;
        min_items = std::min(min_items, count);
        max_items = std::max(max_items, count);
    }

    const size_t total_bits = bit_count * num_filters;
    std::cout << "Bit count per filter  : " << bit_count << '\n';
    std::cout << "Capacity per filter   : " << capacity << '\n';
    std::cout << "Hash functions (k)    : " << static_cast<int>(k) << '\n';
    std::cout << "False positive rate   : " << false_positive_rate << '\n';
    std::cout << "Total items           : " << total_items << '\n';
    std::cout << "Total bit count       : " << total_bits << " bits (~"
              << interleaved_bits.size() << " words)\n";
    std::cout << "Per-filter items      : min " << min_items
              << " / max " << max_items << '\n';
    std::cout << "========================\n";
}
