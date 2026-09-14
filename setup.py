from pathlib import Path

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup


setup(
    name="squirtlefilter",
    version="0.2.0",
    author="Rappsilber-Laboratory",
    description="Fast Bloom filters and bit-sliced multi-filter matching",
    long_description=Path("README.md").read_text(encoding="utf-8"),
    long_description_content_type="text/markdown",
    ext_modules=[
        Pybind11Extension(
            "squirtlefilter",
            ["src/bindings.cpp", "src/SquirtleFilter.cpp", "src/SFilters.cpp"],
            include_dirs=["include", "src"],
            cxx_std=20,
        )
    ],
    cmdclass={"build_ext": build_ext},
    install_requires=["numpy>=1.23"],
    python_requires=">=3.9",
    zip_safe=False,
)
