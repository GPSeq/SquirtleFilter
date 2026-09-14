#ifndef SQUIRTLEFILTER_H
#define SQUIRTLEFILTER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

/** Thread-safe Bloom filter for byte strings and double values. */
class BloomFilter {
public:
    static constexpr std::uint8_t max_hash_functions = 32;

    BloomFilter(std::size_t expected_items = 1000, double false_positive_rate = 0.01,
                std::uint8_t hash_functions = 3);
    ~BloomFilter() = default;
    BloomFilter(const BloomFilter&) = delete;
    BloomFilter& operator=(const BloomFilter&) = delete;
    BloomFilter(BloomFilter&& other) noexcept;
    BloomFilter& operator=(BloomFilter&& other) noexcept;

    void insert(const void* key, std::size_t length);
    void insert(const std::string& key) { insert(key.data(), key.size()); }
    void insert(double value) { insert(&value, sizeof(value)); }
    void insertMany(const std::vector<std::string>& keys);
    void insertMany(const std::vector<double>& values);

    [[nodiscard]] bool contains(const void* key, std::size_t length) const;
    [[nodiscard]] bool contains(const std::string& key) const { return contains(key.data(), key.size()); }
    [[nodiscard]] bool contains(double value) const { return contains(&value, sizeof(value)); }
    [[nodiscard]] std::vector<std::uint8_t> containsMany(const std::vector<std::string>& keys) const;
    [[nodiscard]] std::vector<std::uint8_t> containsMany(const std::vector<double>& values) const;

    void clear();

    using SQFilter = std::vector<std::atomic<std::uint64_t>>;
    using SQFilterRAW = std::vector<std::uint64_t>;
    [[nodiscard]] SQFilterRAW returnFilter() const;

    void writeSQFilter(const std::string& output_path) const;
    void loadSQFilter(const std::string& input_path);
    void printSummary() const;

    [[nodiscard]] static std::size_t computeBitCount(std::size_t expected_items,
                                                     double false_positive_rate,
                                                     std::uint8_t hash_functions = 3);

    struct BloomFilterData {
        std::size_t bit_count{};
        std::size_t capacity{};
        std::size_t item_count{};
        std::uint8_t hash_functions{};
        double false_positive_rate{};
        std::vector<std::uint64_t> bits;
    };

    [[nodiscard]] BloomFilterData exportData() const;
    void importData(const BloomFilterData& data);

private:
    static double validateFalsePositiveRate(double rate);
    static std::uint8_t validateHashCount(std::uint8_t count);
    static void validateKey(const void* key, std::size_t length);
    void insertUnlocked(const void* key, std::size_t length);
    [[nodiscard]] bool containsUnlocked(const void* key, std::size_t length) const;

    std::size_t bit_count{};
    std::shared_ptr<SQFilter> bits;
    std::size_t capacity{};
    std::atomic<std::size_t> item_count{};
    std::uint8_t k{};
    double target_false_positive{};
    mutable std::shared_mutex mutex;
};

#endif
