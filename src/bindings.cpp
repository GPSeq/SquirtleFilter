#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "SFilters.h"
#include "SquirtleFilter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace py = pybind11;

namespace {

py::array_t<std::uint8_t> asArray(const std::vector<std::uint8_t>& values) {
    py::array_t<std::uint8_t> output(values.size());
    if (!values.empty()) {
        std::memcpy(output.mutable_data(), values.data(), values.size() * sizeof(std::uint8_t));
    }
    return output;
}

std::vector<double> copyDoubles(const py::array_t<double, py::array::c_style | py::array::forcecast>& input) {
    if (input.size() == 0) return {};
    return {input.data(), input.data() + input.size()};
}

} // namespace

PYBIND11_MODULE(squirtlefilter, module) {
    module.doc() = "Fast Bloom filters and bit-sliced multi-filter matching";
    module.attr("__version__") = "0.2.0";

    py::class_<BloomFilter>(module, "SquirtleFilter")
        .def(py::init<std::size_t, double, std::uint8_t>(),
             py::arg("expected_items") = 1000, py::arg("false_positive_rate") = 0.01,
             py::arg("hash_functions") = 3)
        .def("insert", static_cast<void (BloomFilter::*)(const std::string&)>(&BloomFilter::insert))
        .def("insert_double", static_cast<void (BloomFilter::*)(double)>(&BloomFilter::insert))
        .def("insert_many", [](BloomFilter& self, const std::vector<std::string>& keys) {
            py::gil_scoped_release release;
            self.insertMany(keys);
        })
        .def("insert_many_double", [](BloomFilter& self,
                                      const py::array_t<double, py::array::c_style | py::array::forcecast>& values) {
            auto copied = copyDoubles(values);
            py::gil_scoped_release release;
            self.insertMany(copied);
        })
        .def("contains", static_cast<bool (BloomFilter::*)(const std::string&) const>(&BloomFilter::contains))
        .def("contains_double", static_cast<bool (BloomFilter::*)(double) const>(&BloomFilter::contains))
        .def("contains_many", [](const BloomFilter& self, const std::vector<std::string>& keys) {
            std::vector<std::uint8_t> result;
            {
                py::gil_scoped_release release;
                result = self.containsMany(keys);
            }
            return asArray(result);
        })
        .def("contains_many_double", [](const BloomFilter& self,
                                        const py::array_t<double, py::array::c_style | py::array::forcecast>& values) {
            auto copied = copyDoubles(values);
            std::vector<std::uint8_t> result;
            {
                py::gil_scoped_release release;
                result = self.containsMany(copied);
            }
            return asArray(result);
        })
        .def("clear", &BloomFilter::clear)
        .def("write", &BloomFilter::writeSQFilter, py::call_guard<py::gil_scoped_release>())
        .def("load", &BloomFilter::loadSQFilter, py::call_guard<py::gil_scoped_release>())
        .def("print_summary_single", &BloomFilter::printSummary);

    py::class_<SFilters>(module, "SFilters")
        .def(py::init<>())
        .def("initialize", &SFilters::initialize,
             py::arg("num_filters"), py::arg("expected_items"),
             py::arg("false_positive_rate"), py::arg("hash_functions"),
             py::call_guard<py::gil_scoped_release>())
        .def("insert_string", static_cast<void (SFilters::*)(std::size_t, const std::string&)>(&SFilters::insert))
        .def("insert_double", static_cast<void (SFilters::*)(std::size_t, double)>(&SFilters::insert))
        .def("insert_many_string", [](SFilters& self, std::size_t index,
                                      const std::vector<std::string>& keys) {
            py::gil_scoped_release release;
            self.insertMany(index, keys);
        })
        .def("insert_many_double", [](SFilters& self, std::size_t index,
                                      const py::array_t<double, py::array::c_style | py::array::forcecast>& values) {
            auto copied = copyDoubles(values);
            py::gil_scoped_release release;
            self.insertMany(index, copied);
        })
        .def("write_to_file", &SFilters::writeToFile, py::call_guard<py::gil_scoped_release>())
        .def("load_from_file", &SFilters::loadFromFile, py::call_guard<py::gil_scoped_release>())
        .def("print_summary_collection", &SFilters::printSummary)
        .def("contains_string", static_cast<bool (SFilters::*)(const std::string&) const>(&SFilters::contains))
        .def("contains_double", static_cast<bool (SFilters::*)(double) const>(&SFilters::contains))
        .def("match_filters_string", static_cast<std::vector<std::size_t> (SFilters::*)(const std::string&) const>(&SFilters::matchFilters))
        .def("match_filters_double", static_cast<std::vector<std::size_t> (SFilters::*)(double) const>(&SFilters::matchFilters))
        .def("match_bit_vector_string", [](const SFilters& self, const std::string& key) {
            std::vector<std::uint8_t> result;
            {
                py::gil_scoped_release release;
                result = self.matchBitVector(key);
            }
            return asArray(result);
        })
        .def("match_bit_vector_double", [](const SFilters& self, double value) {
            std::vector<std::uint8_t> result;
            {
                py::gil_scoped_release release;
                result = self.matchBitVector(value);
            }
            return asArray(result);
        })
        .def("get_filter_count", &SFilters::getFilterCount);
}
