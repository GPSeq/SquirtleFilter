cmake_minimum_required(VERSION 3.15)
project(SquirtleFilter CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

find_package(Threads REQUIRED)

include_directories(include)

# === GoogleTest ===
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# === Google Benchmark ===
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG main
)
FetchContent_MakeAvailable(benchmark)

# === Pybind11 ===
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.11.1
)
FetchContent_MakeAvailable(pybind11)


FetchContent_Declare(
  cereal
  GIT_REPOSITORY https://github.com/USCiLab/cereal.git
  GIT_TAG        master
)
set(JUST_INSTALL_CEREAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cereal)

add_library(SquirtleFilter STATIC src/SquirtleFilter.cpp src/SFilters.cpp)
target_include_directories(SquirtleFilter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${cereal_SOURCE_DIR}/include)
set_target_properties(SquirtleFilter PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(SquirtleMain main.cpp)
target_link_libraries(SquirtleMain PRIVATE SquirtleFilter Threads::Threads)

# === Tests ===
enable_testing()
add_executable(SquirtleTests tests/tests.cpp)
target_link_libraries(SquirtleTests PRIVATE SquirtleFilter gtest gtest_main Threads::Threads)
add_test(NAME SquirtleUnitTests COMMAND SquirtleTests)

# === Benchmark ===
add_executable(SquirtleBenchmark benchmarks/benchmark.cpp)
target_link_libraries(SquirtleBenchmark PRIVATE SquirtleFilter benchmark::benchmark Threads::Threads)

# === Python Module ===
add_library(squirtlefilter_module SHARED src/bindings.cpp)
target_link_libraries(squirtlefilter_module PRIVATE SquirtleFilter pybind11::module)
target_compile_definitions(squirtlefilter_module PRIVATE -DPYBIND11_EXPORTS)
set_target_properties(squirtlefilter_module PROPERTIES OUTPUT_NAME "squirtlefilter" PREFIX "")
