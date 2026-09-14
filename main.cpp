#include "SFilters.h"
#include "SquirtleFilter.h"

#include <iostream>
#include <string>

int main() {
    BloomFilter filter(1000, 0.01, 5);
    filter.insert("squirtle");

    std::cout << "squirtle: "
              << (filter.contains("squirtle") ? "possibly present" : "definitely absent") << '\n';
    std::cout << "ditto: "
              << (filter.contains("ditto") ? "possibly present" : "definitely absent") << '\n';

    SFilters collection;
    collection.initialize(3, 100, 0.01, 5);
    collection.insert(1, std::string("wartortle"));
    const auto matches = collection.matchFilters(std::string("wartortle"));
    std::cout << "wartortle matched " << matches.size() << " filter(s)";
    if (!matches.empty()) std::cout << ", first index: " << matches.front();
    std::cout << '\n';
    return 0;
}
