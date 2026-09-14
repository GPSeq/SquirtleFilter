#include "../include/SquirtleFilter.h"
#include <fstream>
#include <algorithm>
#include <vector>
#include <atomic>
#include <stdexcept>
#include <cassert>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <cereal/archives/binary.hpp>

/**
 * @brief Performs a 64-bit left bitwise rotation.
 *
 * This function rotates the bits of a 64-bit unsigned integer to the left
 * by a specified number of positions. Bits shifted off the left end reappear
 * on the right end.
 *
 * @param x The 64-bit unsigned integer to be rotated.
 * @param r The number of positions to rotate `x` to the left. This value
 * should be between 0 and 63, inclusive.
 * @return The result of rotating `x` left by `r` positions.
 */
static inline uint64_t rotl64(uint64_t x, int8_t r) {
    return (x << r) | (x >> (64 - r));
}

/**
 * @brief Finalizes a 64-bit hash value using a series of bitwise operations and multiplications.
 *
 * This function, often used in hash algorithms like MurmurHash3, applies a
 * sequence of XOR shifts and multiplications to further scramble the bits
 * of an intermediate hash key. This helps to improve the distribution and
 * avalanche effect of the hash.
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

// Implementation of 128-bit MurmurHash3 (x64 variant) to produce two 64-bit hash outputs.
// Check: https://github.com/judwhite/Grassfed.MurmurHash3/blob/master/Grassfed.MurmurHash3/MurmurHash3.cs
/**
 * @brief Computes a 128-bit MurmurHash3 hash for the given key.
 *
 * This function implements the MurmurHash3 algorithm for 128-bit hashes.
 * It processes the input `key` in 16-byte blocks, handles any remaining
 * tail bytes, and then finalizes the hash values to produce two 64-bit
 * output components. MurmurHash3 is a non-cryptographic hash function
 * known for its excellent performance and good distribution properties,
 * making it suitable for applications like hash tables and Bloom filters.
 *
 * @param key A pointer to the data buffer to be hashed.
 * @param len The length of the data buffer in bytes.
 * @param seed The seed value for the hash function, used to produce
 * different hash outputs for the same input data.
 * @param out1 A reference to a `uint64_t` that will store the first
 * 64 bits of the 128-bit hash result.
 * @param out2 A reference to a `uint64_t` that will store the second
 * 64 bits of the 128-bit hash result.
 */
void BloomFilter::hash128(const void* key, size_t len, uint64_t seed, uint64_t& out1, uint64_t& out2) {
    const uint8_t* data = static_cast<const uint8_t*>(key); // raw binary blob (convert ASCII into HEX)
    const int nblocks = len / 16;

    uint64_t h1 = seed;
    uint64_t h2 = seed;

    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    // Body - process 16-byte blocks
    for (int i = 0; i < nblocks; i++) {
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

    // Tail - process remaining bytes
    const uint8_t* tail = data + nblocks * 16;
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    switch (len & 15) {
    case 15: k2 ^= (uint64_t)tail[14] << 48;
    case 14: k2 ^= (uint64_t)tail[13] << 40;
    case 13: k2 ^= (uint64_t)tail[12] << 32;
    case 12: k2 ^= (uint64_t)tail[11] << 24;
    case 11: k2 ^= (uint64_t)tail[10] << 16;
    case 10: k2 ^= (uint64_t)tail[9]  << 8;
    case 9:  k2 ^= (uint64_t)tail[8]  << 0;
             k2 *= c2;
             k2 = rotl64(k2, 33);
             k2 *= c1;
             h2 ^= k2;
    case 8: k1 ^= (uint64_t)tail[7] << 56;
    case 7: k1 ^= (uint64_t)tail[6] << 48;
    case 6: k1 ^= (uint64_t)tail[5] << 40;
    case 5: k1 ^= (uint64_t)tail[4] << 32;
    case 4: k1 ^= (uint64_t)tail[3] << 24;
    case 3: k1 ^= (uint64_t)tail[2] << 16;
    case 2: k1 ^= (uint64_t)tail[1] << 8;
    case 1: k1 ^= (uint64_t)tail[0] << 0;
            k1 *= c1;
            k1 = rotl64(k1, 31);
            k1 *= c2;
            h1 ^= k1;
    }

    // Finalization
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

/**
 * @brief Constructs a BloomFilter object.
 *
 * Initializes a Bloom filter with a specified expected number of items,
 * target false positive rate, and number of hash functions. It calculates
 * the optimal number of bits required for the filter based on these
 * parameters to achieve the desired false positive rate.
 *
 * @param expected_items The approximate maximum number of items that will be
 * added to the Bloom filter. This value is used to calculate the
 * optimal size of the bit array.
 * @param false_positive_rate The desired maximum false positive rate (e.g., 0.01 for 1%).
 * This value influences the calculated size of the bit array.
 * @param hash_functions The number of hash functions to use. This value
 * is typically optimized based on the `expected_items` and
 * `false_positive_rate`, but can be explicitly provided. It is
 * clamped between 1 and 5 in this implementation.
 */
size_t BloomFilter::computeBitCount(size_t expected_items, double false_positive_rate) {
    if (expected_items == 0) expected_items = 1;
    if (false_positive_rate <= 0.0) false_positive_rate = 0.0001;
    if (false_positive_rate >= 1.0) false_positive_rate = 0.999;

    // m = - (n * ln(p)) / (ln(2)^2)
    // false positive rate is used to compute the number of bits
    double m_calc = -(double)expected_items * std::log(false_positive_rate) / (std::log(2) * std::log(2));
    return std::max<size_t>(64, static_cast<size_t>(std::ceil(m_calc)));
}

BloomFilter::BloomFilter(size_t expected_items, double false_positive_rate, uint8_t hash_functions)
    : bit_count(0),
      bits(nullptr),
      capacity(expected_items == 0 ? 1 : expected_items),
      item_count(0),
      k(hash_functions),
      target_false_positive(false_positive_rate) {
    
    if (k < 1) k = 1;
    if (k > 5) k = 5;
    if (target_false_positive <= 0.0) target_false_positive = 0.0001;
    if (target_false_positive >= 1.0) target_false_positive = 0.999;

    bit_count = computeBitCount(capacity, target_false_positive);
    size_t num_words = (bit_count + 63) / 64;
    bits = std::make_shared<std::vector<std::atomic<uint64_t>>>(num_words);
    for (auto& word : *bits) word.store(0, std::memory_order_relaxed);
}

/**
 * @brief Inserts an item into the Bloom filter.
 *
 * This function hashes the provided key using two 64-bit hash functions
 * and then sets the corresponding bits in the Bloom filter's bit array.
 * It uses a technique to generate 'k' distinct hash indices from two base
 * hash values. The bit setting is performed atomically to ensure thread safety.
 *
 * @param key A pointer to the data representing the item to be inserted.
 * @param len The length of the `key` data in bytes.
 */
void BloomFilter::insert(const void* key, size_t len) {
    std::shared_lock<std::shared_mutex> lock(mutex);

    // Generate two 64-bit hash values for the key
    uint64_t h1, h2;
    hash128(key, len, 0, h1, h2);
    uint64_t base_index = h1 % bit_count;
    uint64_t hash2_mod = h2 % bit_count;
    for (uint8_t i = 0; i < k; ++i) {
        uint64_t index = (base_index + i * hash2_mod) % bit_count;
    (*bits)[index / 64].fetch_or(1ULL << (index % 64), std::memory_order_relaxed); //  atomicity only, no synchronization with other thread
    }
    item_count.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief Move assignment operator for the BloomFilter class.
 *
 * Transfers ownership of the Bloom filter's internal state from `other`
 * to this object, leaving `other` in a valid but unspecified state.
 * This operation is thread-safe, acquiring locks on both objects to prevent
 * data races during the transfer. It ensures proper cleanup of the current
 * object's resources before taking over `other`'s resources.
 *
 * @param other The rvalue reference to the BloomFilter object to move from.
 * @return A reference to the current BloomFilter object (`*this`) after the move.
 */
BloomFilter& BloomFilter::operator=(BloomFilter&& other) noexcept {
    if (this != &other) {
        std::unique_lock<std::shared_mutex> lock_this(mutex);
        std::unique_lock<std::shared_mutex> lock_other(other.mutex);

        // Transfer filter configuration
        bit_count = other.bit_count;
        capacity = other.capacity;
        item_count.store(other.item_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
        k = other.k;
        target_false_positive = other.target_false_positive;

        // Transfer actual bit array
        bits = std::move(other.bits);
        other.item_count.store(0, std::memory_order_relaxed);
    }
    return *this;
}


/**
 * @brief Checks if an item might be present in the Bloom filter.
 *
 * This function determines whether the given key has potentially been
 * inserted into the Bloom filter. It calculates 'k' hash indices for the
 * key and checks if all corresponding bits in the filter's bit array are set.
 * Due to the nature of Bloom filters, a return value of `true` indicates
 * that the item *might* be present (with a chance of false positive),
 * while `false` definitively means the item is *not* in the filter.
 * The operation is thread-safe for reads.
 *
 * @param key A pointer to the data representing the item to be checked.
 * @param len The length of the `key` data in bytes.
 * @return `true` if all relevant bits are set (item might be present),
 * `false` if any relevant bit is not set (item is definitely not present).
 */
bool BloomFilter::contains(const void* key, size_t len) const {
    std::shared_lock<std::shared_mutex> lock(mutex);

    uint64_t h1, h2;
    hash128(key, len, 0, h1, h2);
    uint64_t base_index = h1 % bit_count;
    uint64_t hash2_mod = h2 % bit_count;
    for (uint8_t i = 0; i < k; ++i) {
        uint64_t index = (base_index + i * hash2_mod) % bit_count;
        if (((*bits)[index / 64].load(std::memory_order_relaxed) & (1ULL << (index % 64))) == 0)
            return false;
    }
    return true;
}

/**
 * @brief Returns a shared pointer to the underlying bit array (filter data).
 *
 * This function provides a way to access the raw bit data of the Bloom filter.
 * The `std::shared_ptr` ensures that the memory for the bit array remains
 * valid as long as there are any active references to it, even if the
 * original `BloomFilter` object goes out of scope. This is particularly
 * useful for scenarios where the filter's state needs to be shared or
 * observed by external components without exposing the full `BloomFilter` object.
 *
 * @return A `std::shared_ptr` to a `std::vector` of `std::atomic<uint64_t>`,
 * representing the bit array of the Bloom filter.
 */
std::shared_ptr<BloomFilter::SQFilter> BloomFilter::returnFilterReference() const{
    std::shared_lock<std::shared_mutex> lock(mutex);
    return bits; 
}

/**
 * @brief Returns a copy of the Bloom filter's underlying bit array.
 *
 * This function creates a deep copy of the current state of the Bloom filter's
 * bit array and returns it as a new `SQFilterRAW` object. This snapshot
 * represents the filter's state at the moment the function is called, and
 * subsequent modifications to the original Bloom filter will not affect
 * the returned copy. This is useful for scenarios requiring an immutable
 * view of the filter's data or for transferring the filter's state without
 * sharing ownership of the original data.
 *
 * @return A `SQFilterRAW` (which is a `std::vector<uint64_t>`) containing
 * a snapshot of all the 64-bit words that make up the Bloom filter's
 * bit array.
 */
BloomFilter::SQFilterRAW BloomFilter::returnFilter() const{
    std::shared_lock<std::shared_mutex> lock(mutex);
    SQFilterRAW snapshot;
    snapshot.reserve(bits->size());
    for (const auto& atomic_val : *bits) {
        snapshot.push_back(atomic_val.load(std::memory_order_relaxed));
    }
    return snapshot;
}

/**
 * @brief Sets the Bloom filter's internal bit array to a shared external one.
 *
 * This function allows the Bloom filter to take ownership of (or share)
 * an existing bit array, provided as a `std::shared_ptr`. After this call,
 * the Bloom filter will operate on the bits managed by the passed `shared_bits`
 * pointer. This is useful for initializing a Bloom filter from existing data,
 * or for having multiple BloomFilter objects share the same underlying bit array,
 * which can be beneficial for memory efficiency or specific synchronization needs.
 *
 * @param shared_bits A `std::shared_ptr` to a `std::vector` of `std::atomic<uint64_t>`.
 * This shared pointer will replace the current internal `bits` member,
 * causing the BloomFilter to use the provided bit array for all subsequent operations.
 */
void BloomFilter::passFilterReference(std::shared_ptr<SQFilter> shared_bits){
    if (!shared_bits) {
        throw std::invalid_argument("passFilterReference: shared filter reference must not be null");
    }

    std::unique_lock<std::shared_mutex> lock(mutex);
    if (bits && shared_bits->size() != bits->size()) {
        throw std::invalid_argument("passFilterReference: shared filter size does not match current filter layout");
    }
    bits = shared_bits;
}

/**
 * @brief Clears all items from the Bloom filter.
 *
 * Resets all bits in the Bloom filter's underlying bit array to zero,
 * effectively emptying the filter. This operation makes the filter ready
 * for new insertions while maintaining its configured size and number of hash functions.
 * The `item_count` is also reset to zero.
 */
void BloomFilter::clear(){
    std::unique_lock<std::shared_mutex> lock(mutex);
    for (auto& word : *bits) {
        word.store(0, std::memory_order_relaxed);
    }
    item_count.store(0, std::memory_order_relaxed);
}

/**
 * @brief Writes the current state of the Bloom filter to a binary file.
 *
 * Serializes the Bloom filter's configuration parameters (such as bit count,
 * capacity, item count, number of hash functions, and false positive rate)
 * along with its entire bit array to the specified output file in a binary format.
 * This function uses the Cereal library for serialization.
 *
 * @param output_path The file path where the Bloom filter data will be written.
 * @throws std::runtime_error if the output file cannot be opened for writing.
 */
void BloomFilter::writeSQFilter(const std::string& output_path) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    BloomFilterData data;
    data.bit_count = bit_count;
    data.capacity = capacity;
    data.item_count = item_count.load(std::memory_order_relaxed);
    data.hash_functions = k;
    data.false_positive_rate = target_false_positive;

    data.bits.reserve(bits->size());
    for (const auto& val : *bits) {
        data.bits.push_back(val.load(std::memory_order_relaxed));
    }

    std::ofstream ofs(output_path, std::ios::binary);
    if (!ofs) throw std::runtime_error("Failed to open file for writing: " + output_path);
    cereal::BinaryOutputArchive archive(ofs);
    archive(data);
}

/**
 * @brief Loads the state of a Bloom filter from a binary file.
 *
 * Deserializes the Bloom filter's configuration parameters and its bit array
 * from the specified input file, overwriting the current state of this object.
 * This function uses the Cereal library for deserialization. After a successful
 * load, the Bloom filter will have the exact state it had when it was saved.
 *
 * @param input_path The file path from which the Bloom filter data will be read.
 * @throws std::runtime_error if the input file cannot be opened for reading.
 */
void BloomFilter::loadSQFilter(const std::string& input_path) {
    std::ifstream ifs(input_path, std::ios::binary);
    if (!ifs) throw std::runtime_error("Failed to open file for reading: " + input_path);
    cereal::BinaryInputArchive archive(ifs);

    BloomFilterData data;
    archive(data);

    std::unique_lock<std::shared_mutex> lock(mutex);

    // Set internal config
    bit_count = data.bit_count;
    capacity = data.capacity;
    item_count.store(data.item_count, std::memory_order_relaxed);
    k = data.hash_functions;
    target_false_positive = data.false_positive_rate;

    // Reconstruct atomic bit vector, make_shared not best option, but worked sofar! 
    bits = std::make_shared<std::vector<std::atomic<uint64_t>>>(data.bits.size());
    for (size_t i = 0; i < data.bits.size(); ++i) {
        (*bits)[i].store(data.bits[i], std::memory_order_relaxed);
    }
}

/**
 * @brief Counts the number of set bits (1s) in a 64-bit unsigned integer (population count).
 *
 * This function implements a algorithm to count the set bits
 * in a `uint64_t`. It uses a series of bitwise operations to sum up the bits
 * in a parallel fashion, effectively calculating the Hamming weight of the input.
 * This method is a fast, branchless way to compute the population count,
 * often used when hardware `popcount` instructions are not available or
 * for maximum portability.
 *
 * @param x The 64-bit unsigned integer for which to count the set bits.
 * @return The number of set bits (1s) in the input `x`.
 */
inline uint64_t popcount(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    x = x + (x >> 8);
    x = x + (x >> 16);
    x = x + (x >> 32);
    return x & 0x7F;
}

/**
 * @brief Prints a summary of the Bloom filter's current state and configuration to the console.
 *
 * This function outputs various statistics about the Bloom filter, including
 * its calculated size (bit count), expected capacity, current number of items,
 * number of hash functions used, and target false positive rate. It also
 * calculates and displays the actual number of bits currently set in the
 * filter, providing insight into its current fullness and potential for
 * false positives.
 */
void BloomFilter::printSummary() const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    std::cout << "=== Bloom Filter Summary ===\n";
    std::cout << "Bit count             : " << bit_count << '\n';
    std::cout << "Capacity              : " << capacity << '\n';
    std::cout << "Item count            : " << item_count.load(std::memory_order_relaxed) << '\n';
    std::cout << "Hash functions (k)    : " << static_cast<int>(k) << '\n';
    std::cout << "False positive rate   : " << target_false_positive << '\n';
    std::cout << "Bit vector size       : " << bits->size() << " words (64-bit each)\n";

    size_t set_bits = 0;
    for (const auto& word : *bits) {
        set_bits += popcount(word.load(std::memory_order_relaxed));
    }
    std::cout << "Total bits set        : " << set_bits << '\n';
    std::cout << "============================\n";
}

/**
 * @brief Exports the current configuration and bit array state of the Bloom filter into a data structure.
 *
 * This function creates a snapshot of the Bloom filter's essential properties,
 * including its size, capacity, item count, hash function count, false positive rate,
 * and the entire contents of its bit array. The data is returned in a `BloomFilterData`
 * struct, which can then be used for serialization, analysis, or transfer.
 * This operation provides a consistent view of the filter's state at the time of export.
 *
 * @return A `BloomFilterData` object containing all the key parameters and
 * the full bit array content of the current Bloom filter instance.
 */
BloomFilter::BloomFilterData BloomFilter::exportData() const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    BloomFilterData data;
    data.bit_count = bit_count;
    data.capacity = capacity;
    data.item_count = item_count.load(std::memory_order_relaxed);
    data.hash_functions = k;
    data.false_positive_rate = target_false_positive;

    data.bits.reserve(bits->size());
    for (const auto& word : *bits) {
        data.bits.push_back(word.load(std::memory_order_relaxed));
    }
    return data;
}

/**
 * @brief Imports the state of a Bloom filter from a provided data structure.
 *
 * This function allows a Bloom filter object to be initialized or overwritten
 * with a previously exported `BloomFilterData` structure. It sets the filter's
 * configuration parameters and reconstructs its bit array based on the
 * data provided. This is useful for loading a filter from a snapshot or
 * for deep-copying filter states.
 *
 * @param data A constant reference to a `BloomFilterData` object containing
 * the configuration and bit array state to be imported into this Bloom filter.
 */
void BloomFilter::importData(const BloomFilterData& data) {
    std::unique_lock<std::shared_mutex> lock(mutex);
    bit_count = data.bit_count;
    capacity = data.capacity;
    item_count.store(data.item_count, std::memory_order_relaxed);
    k = data.hash_functions;
    target_false_positive = data.false_positive_rate;

    size_t word_count = (bit_count + 63) / 64;
    bits = std::make_shared<std::vector<std::atomic<uint64_t>>>(word_count);

    for (size_t i = 0; i < data.bits.size(); ++i) {
        (*bits)[i].store(data.bits[i], std::memory_order_relaxed);
    }
}

/**
 * @brief Move constructor for the BloomFilter class.
 *
 * Constructs a new BloomFilter object by efficiently transferring the
 * resources (like the bit array and internal state) from an existing
 * `other` BloomFilter object. After this operation, `other` is left in
 * a valid but unspecified state, typically empty or reset, as its resources
 * have been moved. This operation is designed to be efficient,
 * avoiding deep copies, and is marked `noexcept` to guarantee it won't throw
 * exceptions, which is crucial for types used in standard containers.
 *
 * @param other An rvalue reference to the BloomFilter object whose resources
 * will be moved to construct this new object.
 */
BloomFilter::BloomFilter(BloomFilter&& other) noexcept
    : bit_count(other.bit_count),
      bits(std::move(other.bits)),
      capacity(other.capacity),
      item_count(other.item_count.load(std::memory_order_relaxed)),
      k(other.k),
      target_false_positive(other.target_false_positive)
{
    std::unique_lock<std::shared_mutex> lock_other(other.mutex);
    other.item_count.store(0, std::memory_order_relaxed);
}
