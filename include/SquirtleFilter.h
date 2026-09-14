#ifndef SQUIRTLEFILTER_H
#define SQUIRTLEFILTER_H

#include <vector>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <memory>
#include <shared_mutex>
#include <string>
#include <cstring>
#include <cereal/types/vector.hpp>
#include <cereal/archives/binary.hpp>

/**
 * @brief Implements a probabilistic data structure for checking set membership,
 * optimized for space efficiency and high performance.
 *
 * A Bloom filter allows for fast checking of whether an element is a member of a set.
 * It uses multiple hash functions to map elements to bits in a bit array.
 * While it can have false positives (reporting an element as present when it's not),
 * it never has false negatives (reporting an element as not present when it is).
 * This implementation uses `std::atomic<uint64_t>` for thread-safe bit manipulation,
 * making it suitable for concurrent environments. It also provides serialization
 * capabilities for persistence.
 */
class BloomFilter {
public:

    BloomFilter(size_t expected_items = 1000, double false_positive_rate = 0.01, uint8_t hash_functions = 3);
    ~BloomFilter() = default;
    BloomFilter(const BloomFilter&) = delete;
    BloomFilter& operator=(const BloomFilter&) = delete;

    BloomFilter(BloomFilter&& other) noexcept;
    BloomFilter& operator=(BloomFilter&& other) noexcept;

    void insert(const void* key, size_t len);

    void insert(const std::string& key) {
        insert(key.data(), key.size());
    }

    void insert(double value) {
        insert(&value, sizeof(double));
    }

    bool contains(const void* key, size_t len) const;

    bool contains(const std::string& key) const {
        return contains(key.data(), key.size());
    }

    bool contains(double value) const {
    return contains(&value, sizeof(double));
    }

    void clear(); 

    typedef std::vector<std::atomic<uint64_t>> SQFilter;
    typedef std::vector<uint64_t> SQFilterRAW;

    std::shared_ptr<SQFilter> returnFilterReference() const; 

    SQFilterRAW returnFilter() const;

    void passFilterReference(std::shared_ptr<SQFilter> shared_bits);

    void writeSQFilter(const std::string& output_path) const;

    void loadSQFilter(const std::string& input_path);

    void printSummary() const;

    /**
     * @brief Computes the number of bits required for a Bloom filter.
     *
     * This helper centralizes the sizing formula so that both `BloomFilter`
     * and higher-level structures such as `SFilters` can derive identical
     * layouts for the same capacity and false-positive target.
     *
     * @param expected_items The approximate number of elements the filter is expected to hold.
     * @param false_positive_rate The target false-positive rate.
     * @return The total number of bits required for the filter.
     */
    static size_t computeBitCount(size_t expected_items, double false_positive_rate);


    struct BloomFilterData {
        size_t bit_count;
        size_t capacity;
        size_t item_count;
        uint8_t hash_functions;
        double false_positive_rate;
        std::vector<uint64_t> bits;

        template <class Archive>
        void serialize(Archive& ar) {
            ar(bit_count, capacity, item_count, hash_functions, false_positive_rate, bits);
        }
    };

    BloomFilterData exportData() const;

    void importData(const BloomFilterData& data);

private:
    size_t bit_count;
    std::shared_ptr<std::vector<std::atomic<uint64_t>>> bits;
    size_t capacity;
    std::atomic<size_t> item_count;

    // Hash a key to a 128-bit (two 64-bit) result using a high-performance hash (MurmurHash3).
    // We use two 64-bit hashes to generate multiple indices via double hashing.
    static void hash128(const void* key, size_t len, uint64_t seed, uint64_t& out1, uint64_t& out2);

    uint8_t k;                     // number of hash functions
    double target_false_positive;  // desired maximum false positive rate for the Bloom filter
    mutable std::shared_mutex mutex; // mutex for thread-safe access (read for queries/inserts, write for state swaps)
};

#endif
