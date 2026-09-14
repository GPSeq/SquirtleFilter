import numpy as np

import squirtlefilter


def main():
    keys = [f"protein-{index}" for index in range(10_000)]
    filter_ = squirtlefilter.SquirtleFilter(len(keys), 0.01, 5)
    filter_.insert_many(keys)
    print(filter_.contains_many(["protein-42", "missing"]))

    values = np.arange(10_000, dtype=np.float64)
    filter_.insert_many_double(values)
    print(filter_.contains_many_double(np.array([42.0, -1.0])))

    collection = squirtlefilter.SFilters()
    collection.initialize(128, 100, 0.01, 5)
    collection.insert_many_string(7, ["squirtle", "wartortle"])
    print(collection.match_filters_string("wartortle"))


if __name__ == "__main__":
    main()
