# SquirtleFilter

[![Tests](https://github.com/GPSeq/SquirtleFilter/actions/workflows/tests.yml/badge.svg?branch=main)](https://github.com/GPSeq/SquirtleFilter/actions/workflows/tests.yml)
[![Docs](https://github.com/GPSeq/SquirtleFilter/actions/workflows/docs.yml/badge.svg?branch=main)](https://github.com/GPSeq/SquirtleFilter/actions/workflows/docs.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![Doxygen](https://img.shields.io/badge/docs-Doxygen-2C4AA8)](https://www.doxygen.nl/)

SquirtleFilter provides a thread-safe Bloom filter and a bit-sliced collection designed to match one key efficiently across many filters. Storage is calculated from both the requested false-positive rate and selected hash count.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSQUIRTLE_BUILD_PYTHON=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/SquirtleMain
```

Benchmarks are optional so normal builds do not download or compile Google Benchmark:

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DSQUIRTLE_BUILD_PYTHON=OFF \
  -DSQUIRTLE_BUILD_EXAMPLES=OFF -DSQUIRTLE_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel
./build-bench/SquirtleBenchmark
```

## C++ usage

```cpp
#include "SquirtleFilter.h"

BloomFilter filter(1'000'000, 0.01, 5);
filter.insert("squirtle");

if (filter.contains("squirtle")) {
    // Possibly present; Bloom filters can return false positives.
}
```

Search across many filters without constructing a result vector:

```cpp
#include "SFilters.h"

SFilters filters;
filters.initialize(1'000, 10'000, 0.01, 5);
filters.insert(42, "wartortle");

bool found_anywhere = filters.contains("wartortle");
auto matching_indices = filters.matchFilters("wartortle");
auto byte_vector = filters.matchBitVector("wartortle");
```

`SFilters` stores each logical Bloom bit for 64 filters in one machine word. Collection queries combine those words directly, reducing memory traffic compared with reading one full word per filter and probe.

## Python

Build and install the extension with:

```bash
python -m pip install .
```

Single operations and batch operations are available:

```python
import numpy as np
import squirtlefilter

bf = squirtlefilter.SquirtleFilter(100_000, 0.01, 5)
bf.insert_many(["squirtle", "wartortle", "blastoise"])
print(bf.contains_many(["squirtle", "ditto"]))  # [1, 0]

values = np.array([1.5, 2.5, 3.5])
bf.insert_many_double(values)
print(bf.contains_many_double(values))
```

Batch work releases Python's GIL while C++ hashes and probes the data.

## Persistence

`write`/`load` and `write_to_file`/`load_from_file` use validated, versioned, little-endian formats. Writes go through a temporary file before replacement to reduce the chance of leaving a partial destination.

The version 0.2 format is intentionally not compatible with the earlier unversioned Cereal files. The old files did not contain a safe format marker or validated dimensions.

## Threading semantics

Concurrent operations are data-race-free. A query running at the same time as an insertion can observe only part of that insertion because a Bloom insertion updates several independent bits. Synchronize insertions and queries externally when strict linearizable behavior is required.

## CMake options

- `BUILD_TESTING` — build unit tests; defaults to `ON` through CTest.
- `SQUIRTLE_BUILD_EXAMPLES` — build the small example; defaults to `ON`.
- `SQUIRTLE_BUILD_BENCHMARKS` — build benchmarks; defaults to `OFF`.
- `SQUIRTLE_BUILD_PYTHON` — build the Python module; defaults to `OFF`.
- `SQUIRTLE_BUILD_DOCS` — add the Doxygen target; defaults to `OFF`.
- `SQUIRTLE_ENABLE_WARNINGS` — enable strict project warnings; defaults to `ON`.

## License

SquirtleFilter is licensed under the GNU Affero General Public License v3. See [LICENSE](LICENSE).
