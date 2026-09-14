from setuptools import setup, Extension
from pybind11.setup_helpers import Pybind11Extension, build_ext
import sys

#  python setup.py bdist_wheel
# pip install dist/squirtlefilter-0.1-cp310-cp310-linux_x86_64.whl
ext_modules = [
    Pybind11Extension(
        "squirtlefilter",
        ["src/bindings.cpp", "src/SquirtleFilter.cpp", "src/SFilters.cpp"],
        include_dirs=["include", "src"], 
        language="c++",
        extra_compile_args=["-O3", "-std=c++17"],
    ),
]

setup(
    name="squirtlefilter",
    version="0.1",
    author="Rappsilber-Laboratory",
    description="High-performance Bloom filter",
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
)
