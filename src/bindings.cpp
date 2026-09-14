#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "../include/SFilters.h"
#include "../include/SquirtleFilter.h"

namespace py = pybind11;

PYBIND11_MODULE(squirtlefilter, m) {
    py::class_<BloomFilter>(m, "SquirtleFilter")
        .def(py::init<size_t, double, uint8_t>(), "Constructor")
        .def("insert", static_cast<void (BloomFilter::*)(const std::string&)>(&BloomFilter::insert), "Insert string key")
        .def("insert_double", static_cast<void (BloomFilter::*)(double)>(&BloomFilter::insert), "Insert double value")
        .def("contains", static_cast<bool (BloomFilter::*)(const std::string&) const>(&BloomFilter::contains), "Check presence for string")
        .def("contains_double", static_cast<bool (BloomFilter::*)(double) const>(&BloomFilter::contains), "Check presence for double")
        .def("clear", &BloomFilter::clear, "Clear the filter")
        .def("write", &BloomFilter::writeSQFilter, "Write to file")
        .def("load", &BloomFilter::loadSQFilter, "Load from file")
        .def("print_summary_single", &BloomFilter::printSummary, "Print summary"); // this case is handling one filter only!

    py::class_<SFilters>(m, "SFilters")
        .def(py::init<>(), "Default constructor")
        .def("initialize", &SFilters::initialize, "Initialize filters",
             py::arg("num_filters"), py::arg("expected_items"), py::arg("false_positive_rate"), py::arg("hash_functions"))
        .def("insert_string", static_cast<void (SFilters::*)(size_t, const std::string&)>(&SFilters::insert), "Insert string at index")
        .def("insert_double", static_cast<void (SFilters::*)(size_t, double)>(&SFilters::insert), "Insert double at index")
        .def("write_to_file", &SFilters::writeToFile, "Write all filters to file")
        .def("load_from_file", &SFilters::loadFromFile, "Load filters from file")
        .def("print_summary_collection", &SFilters::printSummary, "Print summary of the collection")
        .def("contains_string", [](const SFilters& self, const std::string& key) {
            return self.contains(key);
        }, "Check presence across all filters for string input")
        .def("contains_double", [](const SFilters& self, double value) {
            return self.contains(value);
        }, "Check presence across all filters for double input")

        .def("match_bit_vector_string", [](const SFilters& self, const std::string& key) {
            std::vector<int> result = self.matchBitVector(key);
            return py::array_t<int>(result.size(), result.data());
        }, "Return bit vector for string input")

        .def("match_bit_vector_double", [](const SFilters& self, double value) {
            std::vector<int> result = self.matchBitVector(value);
            return py::array_t<int>(result.size(), result.data());
        }, "Return bit vector for double input")

        .def("get_filter_count", [](const SFilters& self) {
            return self.getFilterCount();
        }, "Return number of filters");
}
