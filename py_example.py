import sys
import resource
import gc
sys.path.append("./build/")
import squirtlefilter
print(dir(squirtlefilter.SquirtleFilter))
import time
import psutil
import os


def get_peak_rss_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024  # in MB


def print_stats(label, start_time, tp, fp, tn, fn):
    end_time = time.time()
    real_time = end_time - start_time
    peak_rss = get_peak_rss_mb()

    total = tp + fp + tn + fn
    fpr = fp / (fp + tn) if (fp + tn) else 0.0
    fnr = fn / (tp + fn) if (tp + fn) else 0.0

    print(f"\n************** {label} **************")
    print(f"Total: {total}, TP: {tp}, FP: {fp}, TN: {tn}, FN: {fn}")
    print(f"False Positive Rate: {fpr:.6f}")
    print(f"False Negative Rate: {fnr:.6f}")
    print(f"Real Time: {real_time:.2f} sec")
    print(f"Peak RSS: {peak_rss:.2f} MB")
    print("**************************************\n")


def single_string_test():
    print("[TEST] Single SquirtleFilter (String)")
    n = 200000000
    f = squirtlefilter.SquirtleFilter(n, 0.001, 5)
    for i in range(n):
        f.insert(f"RQTGRPHGFLRKFGL{i}")
    f.write("singleSFStringsInsertionPY.sf")

    f2 = squirtlefilter.SquirtleFilter(100, 0.001, 5)
    f2.print_summary_single()
    f2.load("singleSFStringsInsertionPY.sf")
    f2.print_summary_single()

    start = time.time()
    TP = sum(f2.contains(f"RQTGRPHGFLRKFGL{i}") for i in range(n))
    FN = n - TP
    FP = sum(f2.contains(f"RQTGRPHFLRKFGL{i}") for i in range(n))
    TN = n - FP
    print_stats("Single String Lookup", start, TP, FP, TN, FN)


def single_double_test():
    print("[TEST] Single SquirtleFilter (Double)")
    n = 200000000
    f = squirtlefilter.SquirtleFilter(n, 0.001, 5)
    for i in range(n):
        f.insert_double(8565948.2514 + i * 0.253458)
    f.write("singleSFDoubleInsertionPY.sf")

    f2 = squirtlefilter.SquirtleFilter(200000000, 0.001, 5)
    f2.load("singleSFDoubleInsertionPY.sf")

    start = time.time()
    TP = sum(f2.contains_double(8565948.2514 + i * 0.253458) for i in range(n))
    FN = n - TP
    FP = sum(f2.contains_double(8565948.2514 + i * 0.353458) for i in range(n))
    TN = n - FP
    print_stats("Single Double Lookup", start, TP, FP, TN, FN)


def multi_string_test():
    print("[TEST] Multi SFilters (String)")
    n = 200000000
    filters = 10
    per_filter = n // filters
    f = squirtlefilter.SFilters()
    f.initialize(filters, per_filter, 0.001, 5)

    for j in range(filters):
        for i in range(per_filter):
            f.insert_string(j, f"RQTGRPHGFLRKFGL{i}")
    f.write_to_file("singleSFStringsInsertionPY.sfs")

    f2 = squirtlefilter.SFilters()
    f2.load_from_file("singleSFStringsInsertionPY.sfs")

    start = time.time()
    TP = sum(any(f2.match_bit_vector_string(f"RQTGRPHGFLRKFGL{i}")) for i in range(per_filter))
    FN = per_filter - TP
    FP = sum(any(f2.match_bit_vector_string(f"RQTGRPHFLRKFGL{i}")) for i in range(per_filter))
    TN = per_filter - FP
    print_stats("Multi String Lookup", start, TP, FP, TN, FN)


def multi_double_test():
    print("[TEST] Multi SFilters (Double)")
    n = 100_000
    filters = 10
    per_filter = n // filters
    f = squirtlefilter.SFilters()
    f.initialize(filters, per_filter, 0.001, 5)

    for j in range(filters):
        for i in range(per_filter):
            f.insert_double(j, 8565948.2514 + i * 0.253458)
    f.write_to_file("multiSFDoubleInsertionPY.sfs")

    f2 = squirtlefilter.SFilters()
    f2.load_from_file("multiSFDoubleInsertionPY.sfs")

    start = time.time()
    TP = sum(any(f2.match_bit_vector_double(8565948.2514 + i * 0.253458)) for i in range(per_filter))
    FN = per_filter - TP
    FP = sum(any(f2.match_bit_vector_double(8565948.2514 + i * 0.353458)) for i in range(per_filter))
    TN = per_filter - FP
    print_stats("Multi Double Lookup", start, TP, FP, TN, FN)

def multi_double_test_real():
    print("[TEST] Multi SFilters (Double)")
    n = 150
    filters = 2000_000
    per_filter = n
    f = squirtlefilter.SFilters()
    f.initialize(filters, per_filter, 0.001, 5)

    for j in range(filters):
        for i in range(per_filter):
            f.insert_double(j, 8565948.2514 + i * 0.253458)
    f.write_to_file("multiSFDoubleInsertionPY.sfs")
    f = None
    gc.collect()
    f2 = squirtlefilter.SFilters()
    f2.load_from_file("multiSFDoubleInsertionPY.sfs")

    start = time.time()
    TP = sum(any(f2.match_bit_vector_double(8565948.2514 + i * 0.253458)) for i in range(per_filter))
    FN = per_filter - TP
    FP = sum(any(f2.match_bit_vector_double(8565948.2514 + i * 0.353458)) for i in range(per_filter))
    TN = per_filter - FP
    print_stats("Multi Double Lookup", start, TP, FP, TN, FN)

if __name__ == "__main__":
    # single_string_test()
    # single_double_test()
    # multi_string_test()
    # multi_double_test()
    multi_double_test_real()
