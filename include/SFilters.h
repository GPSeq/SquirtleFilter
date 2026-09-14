#ifndef SFILTERS_H
#define SFILTERS_H

#include "SquirtleFilter.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>
#include <string>
#include <iomanip>
#include <fstream>


/**
 * @brief Manages a collection of Bloom filter instances, enabling operations across multiple filters.
 *
 * The `SFilters` class (likely short for "Scalable Filters" or "Set of Filters")
 * provides a higher-level abstraction for working with multiple `BloomFilter` objects.
 * It allows for initialization of a specified number of filters, inserting items
 * into specific filters, serializing/deserializing the entire collection,
 * and querying the presence of items across all managed filters. This can be
 * useful for tiered Bloom filters, sharded filters, or scenarios requiring
 * multiple independent filters.
 */
class SFilters {
public:
    SFilters() = default;

    // Create N filters
    void initialize(size_t num_filters, size_t expected_items, double false_positive_rate, uint8_t hash_functions);

    // Insert into a specific filter
    void insert(size_t index, const std::string& key);
    void insert(size_t index, double value);

    // Write all filters to file
    void writeToFile(const std::string& output_path) const;

    // Load filters from file
    void loadFromFile(const std::string& input_path);

    // Search across all filters
    template <typename T>
    bool contains(const T& key) const;

    // Return the number of filters
    size_t getFilterCount() const;

    template <typename T>
    std::vector<size_t> matchFilters(const T& key) const;

    std::vector<int> matchBitVector(const std::string& key) const;
    std::vector<int> matchBitVector(double value) const;

    void printSummary() const;

private:
    struct SFiltersData {
        size_t num_filters;
        size_t bit_count;
        size_t capacity;
        uint8_t hash_functions;
        double false_positive_rate;
        std::vector<size_t> item_counts;
        std::vector<uint64_t> interleaved_bits;

        template <class Archive>
        void serialize(Archive& ar) {
            ar(num_filters, bit_count, capacity, hash_functions, false_positive_rate, item_counts, interleaved_bits);
        }
    };

    size_t num_filters = 0;
    size_t bit_count = 0;
    size_t capacity = 0;
    uint8_t k = 0;
    double false_positive_rate = 0.01;
    std::vector<size_t> item_counts;
    std::vector<uint64_t> interleaved_bits;

    size_t wordCountPerFilter() const;
    size_t interleavedOffset(size_t word_index, size_t filter_index) const;
    bool matchWord(size_t filter_index, size_t word_index, uint64_t bit_mask) const;
    void setWord(size_t filter_index, size_t word_index, uint64_t bit_mask);
    void validateIndex(size_t index) const;
};

#include "SFilters.tpp"  // for template impl

#endif // SFILTERS_H
