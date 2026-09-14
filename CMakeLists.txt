cmake_minimum_required(VERSION 3.20)
project(SquirtleFilter VERSION 0.2.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SQUIRTLE_BUILD_EXAMPLES "Build the lightweight C++ example" ON)
option(SQUIRTLE_BUILD_BENCHMARKS "Build Google Benchmark targets" OFF)
option(SQUIRTLE_BUILD_PYTHON "Build the Python extension" OFF)
option(SQUIRTLE_BUILD_DOCS "Build Doxygen documentation" OFF)
option(SQUIRTLE_ENABLE_WARNINGS "Enable strict warnings for SquirtleFilter targets" ON)

find_package(Threads REQUIRED)

add_library(SquirtleFilter STATIC src/SquirtleFilter.cpp src/SFilters.cpp)
add_library(SquirtleFilter::SquirtleFilter ALIAS SquirtleFilter)
target_compile_features(SquirtleFilter PUBLIC cxx_std_20)
target_include_directories(SquirtleFilter
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(SquirtleFilter PUBLIC Threads::Threads)
set_target_properties(SquirtleFilter PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(squirtle_enable_warnings target)
    if(NOT SQUIRTLE_ENABLE_WARNINGS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2
            -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough=5)
    endif()
endfunction()
squirtle_enable_warnings(SquirtleFilter)

if(SQUIRTLE_BUILD_EXAMPLES)
    add_executable(SquirtleMain main.cpp)
    target_link_libraries(SquirtleMain PRIVATE SquirtleFilter::SquirtleFilter)
    squirtle_enable_warnings(SquirtleMain)
endif()

if(BUILD_TESTING)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    add_executable(SquirtleTests tests/tests.cpp)
    target_link_libraries(SquirtleTests PRIVATE SquirtleFilter::SquirtleFilter GTest::gtest_main)
    squirtle_enable_warnings(SquirtleTests)
    add_test(NAME SquirtleUnitTests COMMAND SquirtleTests)
endif()

if(SQUIRTLE_BUILD_BENCHMARKS)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG c58e6d0710581e3a08d65c349664128a8d9a2461)
    FetchContent_MakeAvailable(benchmark)
    add_executable(SquirtleBenchmark benchmarks/benchmark.cpp)
    target_link_libraries(SquirtleBenchmark PRIVATE SquirtleFilter::SquirtleFilter benchmark::benchmark)
    squirtle_enable_warnings(SquirtleBenchmark)
endif()

if(SQUIRTLE_BUILD_PYTHON)
    set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG a2e59f0e7065404b44dfe92a28aca47ba1378dc4)
    FetchContent_MakeAvailable(pybind11)
    pybind11_add_module(squirtlefilter src/bindings.cpp)
    target_link_libraries(squirtlefilter PRIVATE SquirtleFilter::SquirtleFilter)
    squirtle_enable_warnings(squirtlefilter)
endif()

if(SQUIRTLE_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    doxygen_add_docs(docs include README.md COMMENT "Generate API documentation")
endif()

install(TARGETS SquirtleFilter EXPORT SquirtleFilterTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(
    cmake/SquirtleFilterConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SquirtleFilterConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SquirtleFilter)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SquirtleFilterConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT SquirtleFilterTargets NAMESPACE SquirtleFilter::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SquirtleFilter)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SquirtleFilterConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SquirtleFilterConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SquirtleFilter)
