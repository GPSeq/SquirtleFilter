#include <gtest/gtest.h>

#include "SFilters.h"
#include "SquirtleFilter.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::filesystem::path temporaryFile(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("squirtlefilter-" + name);
}

} // namespace

TEST(BloomFilterTest, InsertContainsAndClear) {
    BloomFilter filter(100, 0.01, 3);
    EXPECT_FALSE(filter.contains("hello"));
    filter.insert("hello");
    EXPECT_TRUE(filter.contains("hello"));
    filter.clear();
    EXPECT_FALSE(filter.contains("hello"));
}

TEST(BloomFilterTest, SupportsEmptyAndDoubleKeys) {
    BloomFilter filter(100, 0.01, 3);
    filter.insert(std::string{});
    filter.insert(3.1415);
    EXPECT_TRUE(filter.contains(std::string{}));
    EXPECT_TRUE(filter.contains(3.1415));
}

TEST(BloomFilterTest, RejectsInvalidParametersAndPointers) {
    EXPECT_THROW((BloomFilter{0, 0.01, 3}), std::invalid_argument);
    EXPECT_THROW((BloomFilter{10, 0.0, 3}), std::invalid_argument);
    EXPECT_THROW((BloomFilter{10, std::nan(""), 3}), std::invalid_argument);
    EXPECT_THROW((BloomFilter{10, 0.01, 0}), std::invalid_argument);
    BloomFilter filter;
    EXPECT_THROW(filter.insert(nullptr, 1), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(filter.contains(nullptr, 1)), std::invalid_argument);
}

TEST(BloomFilterTest, SizingHonorsSelectedHashCount) {
    constexpr std::size_t items = 100'000;
    constexpr double requested_rate = 0.01;
    for (const std::uint8_t hashes : {std::uint8_t{3}, std::uint8_t{5}}) {
        BloomFilter filter(items, requested_rate, hashes);
        for (std::size_t i = 0; i < items; ++i) filter.insert("in-" + std::to_string(i));
        std::size_t false_positives{};
        for (std::size_t i = 0; i < items; ++i) {
            false_positives += filter.contains("out-" + std::to_string(i));
        }
        EXPECT_LT(static_cast<double>(false_positives) / items, 0.0125) << "hashes=" << unsigned(hashes);
    }
}

TEST(BloomFilterTest, BatchOperations) {
    BloomFilter filter(100, 0.01, 5);
    const std::vector<std::string> strings{"a", "b", "c"};
    const std::vector<double> doubles{1.0, 2.0, 3.0};
    filter.insertMany(strings);
    filter.insertMany(doubles);
    EXPECT_EQ(filter.containsMany(strings), (std::vector<std::uint8_t>{1, 1, 1}));
    EXPECT_EQ(filter.containsMany(doubles), (std::vector<std::uint8_t>{1, 1, 1}));
}

TEST(BloomFilterTest, ImportRejectsBadDimensionsWithoutChangingState) {
    BloomFilter filter(100, 0.01, 3);
    filter.insert("still-here");
    auto bad = filter.exportData();
    bad.bits.push_back(0);
    EXPECT_THROW(filter.importData(bad), std::invalid_argument);
    EXPECT_TRUE(filter.contains("still-here"));
}

TEST(BloomFilterTest, VersionedPersistenceRoundTrip) {
    const auto path = temporaryFile("single.sqbf");
    BloomFilter original(100, 0.01, 5);
    original.insert("persist");
    original.writeSQFilter(path.string());
    BloomFilter loaded;
    loaded.loadSQFilter(path.string());
    EXPECT_TRUE(loaded.contains("persist"));
    std::filesystem::remove(path);
}

TEST(BloomFilterTest, RejectsCorruptPersistence) {
    const auto path = temporaryFile("corrupt.sqbf");
    { std::ofstream output(path, std::ios::binary); output << "not-a-filter"; }
    BloomFilter filter;
    EXPECT_THROW(filter.loadSQFilter(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST(BloomFilterTest, MoveOperationsTransferState) {
    BloomFilter first(100, 0.01, 3);
    first.insert("x");
    BloomFilter second(std::move(first));
    EXPECT_TRUE(second.contains("x"));
    BloomFilter third;
    third = std::move(second);
    EXPECT_TRUE(third.contains("x"));
}

TEST(BloomFilterTest, ConcurrentInsertionsRemainVisibleAfterJoin) {
    BloomFilter filter(4000, 0.01, 5);
    std::vector<std::thread> threads;
    for (std::size_t thread = 0; thread < 4; ++thread) {
        threads.emplace_back([&, thread] {
            for (std::size_t i = 0; i < 1000; ++i) {
                filter.insert("key-" + std::to_string(thread) + '-' + std::to_string(i));
            }
        });
    }
    for (auto& thread : threads) thread.join();
    for (std::size_t thread = 0; thread < 4; ++thread) {
        for (std::size_t i = 0; i < 1000; ++i) {
            EXPECT_TRUE(filter.contains("key-" + std::to_string(thread) + '-' + std::to_string(i)));
        }
    }
}

TEST(SFiltersTest, InitializeInsertAndMatch) {
    SFilters filters;
    filters.initialize(130, 100, 0.01, 5);
    filters.insert(0, "shared");
    filters.insert(64, "shared");
    filters.insert(129, "shared");
    EXPECT_TRUE(filters.contains("shared"));
    EXPECT_EQ(filters.matchFilters("shared"), (std::vector<std::size_t>{0, 64, 129}));
    const auto bits = filters.matchBitVector("shared");
    ASSERT_EQ(bits.size(), 130);
    EXPECT_EQ(bits[0], 1);
    EXPECT_EQ(bits[64], 1);
    EXPECT_EQ(bits[129], 1);
}

TEST(SFiltersTest, EmptyCollectionDoesNotMatch) {
    SFilters filters;
    filters.initialize(0, 100, 0.01, 3);
    EXPECT_FALSE(filters.contains("x"));
    EXPECT_TRUE(filters.matchFilters("x").empty());
}

TEST(SFiltersTest, SupportsDoubleAndBatchOperations) {
    SFilters filters;
    filters.initialize(2, 100, 0.01, 5);
    filters.insertMany(1, std::vector<std::string>{"a", "b"});
    filters.insertMany(0, std::vector<double>{2.5, 3.5});
    EXPECT_EQ(filters.matchFilters("a"), (std::vector<std::size_t>{1}));
    EXPECT_EQ(filters.matchFilters(3.5), (std::vector<std::size_t>{0}));
}

TEST(SFiltersTest, RejectsInvalidIndex) {
    SFilters filters;
    filters.initialize(1, 100, 0.01, 3);
    EXPECT_THROW(filters.insert(1, "x"), std::out_of_range);
    EXPECT_THROW(filters.insertMany(2, std::vector<double>{1.0}), std::out_of_range);
}

TEST(SFiltersTest, VersionedPersistenceRoundTrip) {
    const auto path = temporaryFile("collection.sqsf");
    SFilters original;
    original.initialize(70, 100, 0.01, 5);
    original.insert(69, "persist");
    original.writeToFile(path.string());
    SFilters loaded;
    loaded.loadFromFile(path.string());
    EXPECT_EQ(loaded.getFilterCount(), 70);
    EXPECT_EQ(loaded.matchFilters("persist"), (std::vector<std::size_t>{69}));
    std::filesystem::remove(path);
}

TEST(SFiltersTest, RejectsCorruptPersistence) {
    const auto path = temporaryFile("corrupt.sqsf");
    { std::ofstream output(path, std::ios::binary); output << "bad"; }
    SFilters filters;
    EXPECT_THROW(filters.loadFromFile(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST(SFiltersTest, ConcurrentInsertionsAreDataRaceFree) {
    SFilters filters;
    filters.initialize(64, 1000, 0.01, 5);
    std::vector<std::thread> threads;
    for (std::size_t thread = 0; thread < 4; ++thread) {
        threads.emplace_back([&, thread] {
            for (std::size_t i = 0; i < 500; ++i) {
                filters.insert(thread, "key-" + std::to_string(thread) + '-' + std::to_string(i));
            }
        });
    }
    for (auto& thread : threads) thread.join();
    for (std::size_t thread = 0; thread < 4; ++thread) {
        EXPECT_TRUE(filters.contains("key-" + std::to_string(thread) + "-499"));
    }
}
