#ifndef SFILTERS_H
#define SFILTERS_H

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <vector>

/** A bit-sliced collection optimized for matching a key across many filters. */
class SFilters {
public:
    SFilters() = default;
    SFilters(const SFilters&) = delete;
    SFilters& operator=(const SFilters&) = delete;

    void initialize(std::size_t num_filters, std::size_t expected_items,
                    double false_positive_rate, std::uint8_t hash_functions);
    void insert(std::size_t index, const std::string& key);
    void insert(std::size_t index, double value);
    void insertMany(std::size_t index, const std::vector<std::string>& keys);
    void insertMany(std::size_t index, const std::vector<double>& values);

    [[nodiscard]] bool contains(const std::string& key) const;
    [[nodiscard]] bool contains(double value) const;
    [[nodiscard]] std::vector<std::size_t> matchFilters(const std::string& key) const;
    [[nodiscard]] std::vector<std::size_t> matchFilters(double value) const;
    [[nodiscard]] std::vector<std::uint8_t> matchBitVector(const std::string& key) const;
    [[nodiscard]] std::vector<std::uint8_t> matchBitVector(double value) const;

    void writeToFile(const std::string& output_path) const;
    void loadFromFile(const std::string& input_path);
    [[nodiscard]] std::size_t getFilterCount() const;
    void printSummary() const;

private:
    [[nodiscard]] std::size_t filterWordCount() const noexcept;
    [[nodiscard]] std::size_t bitSliceOffset(std::size_t bit_index,
                                             std::size_t filter_word) const noexcept;
    void validateIndex(std::size_t index) const;
    void insertBytesUnlocked(std::size_t index, const void* key, std::size_t length);
    [[nodiscard]] bool containsBytesUnlocked(const void* key, std::size_t length) const;
    [[nodiscard]] std::vector<std::size_t> matchFiltersBytesUnlocked(const void* key,
                                                                     std::size_t length) const;
    [[nodiscard]] std::vector<std::uint8_t> matchBitVectorBytesUnlocked(const void* key,
                                                                        std::size_t length) const;

    std::size_t num_filters{};
    std::size_t bit_count{};
    std::size_t capacity{};
    std::uint8_t k{};
    double false_positive_rate{0.01};
    std::vector<std::uint64_t> item_counts;
    std::vector<std::uint64_t> bit_slices;
    mutable std::shared_mutex mutex;
};

#endif
