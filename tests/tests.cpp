#include <gtest/gtest.h>
#include "SquirtleFilter.h"
#include "SFilters.h"
#include <string>
#include <cstdio>

// ==== BLOOM FILTER TESTS ====

TEST(BloomFilterTest, InsertAndContainsString) {
    BloomFilter bf(100, 0.01, 3);
    std::string key = "hello";
    bf.insert(key.data(), key.size());
    EXPECT_TRUE(bf.contains(key.data(), key.size()));
}

TEST(BloomFilterTest, ContainsReturnsFalse) {
    BloomFilter bf(100, 0.01, 3);
    std::string key = "absent";
    EXPECT_FALSE(bf.contains(key.data(), key.size()));
}

TEST(BloomFilterTest, ReturnFilterReferenceIsNotNull) {
    BloomFilter bf(100, 0.01, 3);
    auto ref = bf.returnFilterReference();
    EXPECT_NE(ref, nullptr);
}

TEST(BloomFilterTest, ReturnFilterIsCopy) {
    BloomFilter bf(100, 0.01, 3);
    auto original = bf.returnFilter();
    bf.insert("x", 1);
    auto after = bf.returnFilter();
    EXPECT_NE(original, after);
}

TEST(BloomFilterTest, PassFilterReferenceShared) {
    BloomFilter bf1(100, 0.01, 3);
    bf1.insert("x", 1);
    auto ref = bf1.returnFilterReference();

    BloomFilter bf2(100, 0.01, 3);
    bf2.passFilterReference(ref);

    EXPECT_TRUE(bf2.contains("x", 1));
}

TEST(BloomFilterTest, PassFilterReferenceRejectsMismatchedLayout) {
    BloomFilter bf1(100, 0.01, 3);
    BloomFilter bf2(1000, 0.01, 3);
    auto ref = bf2.returnFilterReference();

    EXPECT_THROW(bf1.passFilterReference(ref), std::invalid_argument);
}

TEST(BloomFilterTest, ClearResetsBits) {
    BloomFilter bf(100, 0.01, 3);
    bf.insert("a", 1);
    bf.clear();
    EXPECT_FALSE(bf.contains("a", 1));
}

TEST(BloomFilterTest, ExportImportRoundTrip) {
    BloomFilter bf1(100, 0.01, 3);
    bf1.insert("z", 1);
    auto data = bf1.exportData();

    BloomFilter bf2(100, 0.01, 3);
    bf2.importData(data);
    EXPECT_TRUE(bf2.contains("z", 1));
}

TEST(BloomFilterTest, WriteAndLoadSQFilter) {
    BloomFilter bf1(100, 0.01, 3);
    bf1.insert("a", 1);
    bf1.writeSQFilter("test.sf");

    BloomFilter bf2(1, 0.5, 1);
    bf2.loadSQFilter("test.sf");

    EXPECT_TRUE(bf2.contains("a", 1));
    std::remove("test.sf");
}

TEST(BloomFilterTest, MoveConstructorTransfersOwnership) {
    BloomFilter bf1(100, 0.01, 3);
    bf1.insert("x", 1);
    BloomFilter bf2(std::move(bf1));
    EXPECT_TRUE(bf2.contains("x", 1));
}

TEST(BloomFilterTest, MoveAssignmentTransfersOwnership) {
    BloomFilter bf1(100, 0.01, 3);
    bf1.insert("y", 1);
    BloomFilter bf2(1, 0.5, 1);
    bf2 = std::move(bf1); // just move data from one to the next filter, memory-better. 
    EXPECT_TRUE(bf2.contains("y", 1));
}

// ==== SFILTERS TESTS ====

TEST(SFiltersTest, InitializeCreatesCorrectCount) {
    SFilters sf;
    sf.initialize(5, 100, 0.01, 3);
    EXPECT_EQ(sf.getFilterCount(), 5);
}

TEST(SFiltersTest, InsertAndContainsString) {
    SFilters sf;
    sf.initialize(1, 100, 0.01, 3);
    sf.insert(0, std::string("A"));
    EXPECT_TRUE(sf.contains(std::string("A")));
}

TEST(SFiltersTest, InsertAndContainsDouble) {
    SFilters sf;
    sf.initialize(1, 100, 0.01, 3);
    sf.insert(0, 3.1415);
    EXPECT_TRUE(sf.contains(3.1415));
}

TEST(SFiltersTest, MatchFiltersReturnsCorrectIndex) {
    SFilters sf;
    sf.initialize(3, 100, 0.01, 3);
    sf.insert(2, std::string("KEY"));
    auto result = sf.matchFilters(std::string("KEY"));
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 2);
}

TEST(SFiltersTest, InterleavedMatchVectorCanMatchMultipleFilters) {
    SFilters sf;
    sf.initialize(3, 100, 0.01, 3);
    sf.insert(0, std::string("shared"));
    sf.insert(2, std::string("shared"));

    auto vec = sf.matchBitVector("shared");
    ASSERT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 0);
    EXPECT_EQ(vec[2], 1);
}

TEST(SFiltersTest, MatchBitVectorString) {
    SFilters sf;
    sf.initialize(2, 100, 0.01, 3);
    sf.insert(1, "abc");
    auto vec = sf.matchBitVector("abc");
    EXPECT_EQ(vec.size(), 2);
    EXPECT_EQ(vec[0], 0);
    EXPECT_EQ(vec[1], 1);
}

TEST(SFiltersTest, MatchBitVectorDouble) {
    SFilters sf;
    sf.initialize(2, 100, 0.01, 3);
    sf.insert(0, 2.71828);
    auto vec = sf.matchBitVector(2.71828);
    EXPECT_EQ(vec.size(), 2);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 0);
}

TEST(SFiltersTest, InsertOutOfRangeThrows) {
    SFilters sf;
    sf.initialize(1, 100, 0.01, 3);
    EXPECT_THROW(sf.insert(10, "x"), std::out_of_range);
    EXPECT_THROW(sf.insert(5, 42.0), std::out_of_range);
}

TEST(SFiltersTest, WriteAndLoadRoundTrip) {
    SFilters sf1;
    sf1.initialize(1, 100, 0.01, 3);
    sf1.insert(0, "persist");
    sf1.writeToFile("test.sfs");

    SFilters sf2;
    sf2.loadFromFile("test.sfs");

    EXPECT_TRUE(sf2.contains("persist"));
    std::remove("test.sfs");
}
