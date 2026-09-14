/**
 * @brief Checks the presence of a key across all Bloom filters in the collection.
 *
 * This template function iterates through each Bloom filter managed by `SFilters`
 * and checks if the given `key` might be present in *any* of the filters.
 * It returns `true` as soon as the key is found in at least one filter,
 * optimizing for early exit. If the key is not found in any filter, it returns `false`.
 * This is useful for a "union" or "any match" query across the set of filters.
 *
 * @tparam T The type of the key (e.g., `std::string`, `double`, or a raw data pointer).
 * This type must be supported by the `BloomFilter::contains` method.
 * @param key The key to check for presence.
 * @return `true` if the key might be present in any of the filters, `false` otherwise.
 */
template <typename T>
bool SFilters::contains(const T& key) const {
    const auto presence = matchBitVector(key);
    for (int match : presence) {
        if (match == 1) return true;
    }
    return false;
}

/**
 * @brief Finds all filters that potentially contain a given key.
 *
 * This template function iterates through each Bloom filter in the collection
 * and identifies all filters that report the presence of the `key`. It returns
 * a vector containing the *indices* of these matching filters. This is useful
 * when you need to know which specific filters (if any) might contain an item,
 * or for implementing more complex matching logic.
 *
 * @tparam T The type of the key (e.g., `std::string`, `double`, or a raw data pointer).
 * This type must be supported by the `BloomFilter::contains` method.
 * @param key The key to match against the filters.
 * @return A `std::vector<size_t>` containing the zero-based indices of the filters
 * that potentially contain the key. An empty vector means no filters matched.
 */
template <typename T>
std::vector<size_t> SFilters::matchFilters(const T& key) const {
    const auto presence = matchBitVector(key);
    std::vector<size_t> matched;
    for (size_t i = 0; i < presence.size(); ++i) {
        if (presence[i] == 1) {
            matched.push_back(i);
        }
    }
    return matched;
}

