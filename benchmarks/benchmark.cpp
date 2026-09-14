#include <benchmark/benchmark.h>

#include "SFilters.h"
#include "SquirtleFilter.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

std::vector<std::string> makeKeys(std::size_t count, const std::string& prefix) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) keys.push_back(prefix + std::to_string(i));
    return keys;
}

void BloomInsert(benchmark::State& state) {
    const auto keys = makeKeys(65'536, "insert-");
    BloomFilter filter(1'000'000, 0.01, 5);
    std::size_t index{};
    for (auto _ : state) {
        filter.insert(keys[index]);
        index = (index + 1U) % keys.size();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BloomInsert);

void BloomContains(benchmark::State& state) {
    const auto present = makeKeys(65'536, "present-");
    const auto absent = makeKeys(65'536, "absent-");
    BloomFilter filter(1'000'000, 0.01, 5);
    filter.insertMany(present);
    const bool query_present = state.range(0) != 0;
    const auto& keys = query_present ? present : absent;
    std::size_t index{};
    for (auto _ : state) {
        benchmark::DoNotOptimize(filter.contains(keys[index]));
        index = (index + 1U) % keys.size();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BloomContains)->ArgName("present")->Arg(0)->Arg(1);

void CollectionContains(benchmark::State& state) {
    const auto filter_count = static_cast<std::size_t>(state.range(0));
    const auto present = makeKeys(4096, "present-");
    const auto absent = makeKeys(4096, "absent-");
    SFilters filters;
    filters.initialize(filter_count, 1000, 0.01, 5);
    for (std::size_t i = 0; i < present.size(); ++i) filters.insert(i % filter_count, present[i]);
    const bool query_present = state.range(1) != 0;
    const auto& keys = query_present ? present : absent;
    std::size_t index{};
    for (auto _ : state) {
        benchmark::DoNotOptimize(filters.contains(keys[index]));
        index = (index + 1U) % keys.size();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(CollectionContains)
    ->ArgsProduct({{64, 1024, 16'384}, {0, 1}})
    ->ArgNames({"filters", "present"});

} // namespace

BENCHMARK_MAIN();
