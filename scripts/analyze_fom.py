#!/usr/bin/env python3
# ==============================================================================
# LULESH Benchmark FOM (Figure of Merit) Statistical Analyzer.
#
# Description:
#   Parses LULESH execution logs from stdin or log files, extracts FOM values,
#   and calculates statistical metrics including Mean, Standard Deviation,
#   Standard Error, and 95% Confidence Interval (using scipy.stats Student's
#   t-distribution).
#
# Usage:
#   ./scripts/run_lulesh_repeats.sh 5 hbm | python3 ./scripts/analyze_fom.py
#   python3 ./scripts/analyze_fom.py <logfile>
# ==============================================================================

import sys
import re
import math
import statistics
import scipy.stats as stats

def parse_fom_values(text):
    pattern = re.compile(r"FOM\s*=\s*([0-9]+(?:\.[0-9]+)?)", re.IGNORECASE)
    matches = pattern.findall(text)
    return [float(val) for val in matches]

def analyze_fom(fom_list):
    n = len(fom_list)
    if n == 0:
        print("[ERROR] No valid FOM values found in input.")
        sys.exit(1)

    print("=================================================================")
    print(" LULESH Benchmark Results Analysis (95% Confidence Interval)")
    print("=================================================================")
    print(f" Total runs analyzed (n) : {n}")
    print(f" Raw FOM values (z/s)    : {', '.join(f'{x:.2f}' for x in fom_list)}")
    print("-----------------------------------------------------------------")

    mean = statistics.mean(fom_list)
    print(f" Mean FOM (z/s)          : {mean:.4f}")
    print(f" Min FOM (z/s)           : {min(fom_list):.4f}")
    print(f" Max FOM (z/s)           : {max(fom_list):.4f}")
    print(f" Median FOM (z/s)        : {statistics.median(fom_list):.4f}")

    if n > 1:
        stdev = statistics.stdev(fom_list)
        stderr = stdev / math.sqrt(n)
        df = n - 1
        t_crit = stats.t.ppf(0.975, df)
        margin_of_error = t_crit * stderr
        ci_lower = mean - margin_of_error
        ci_upper = mean + margin_of_error
        rel_error_pct = (margin_of_error / mean) * 100

        print(f" Std Deviation (s)       : {stdev:.4f}")
        print(f" Std Error (SE)          : {stderr:.4f}")
        print(f" Critical t (df={df})      : {t_crit:.4f}")
        print(f" 95% Confidence Interval : [{ci_lower:.4f}, {ci_upper:.4f}] (z/s)")
        print(f" Margin of Error         : ±{margin_of_error:.4f} (±{rel_error_pct:.2f}%)")
    else:
        print(" [INFO] Single run detected. Confidence interval requires n >= 2.")

    print("=================================================================")

def main():
    if len(sys.argv) > 1 and sys.argv[1] not in ("-h", "--help"):
        filepath = sys.argv[1]
        try:
            with open(filepath, 'r') as f:
                content = f.read()
        except IOError as e:
            print(f"[ERROR] Could not read file '{filepath}': {e}")
            sys.exit(1)
    else:
        if sys.stdin.isatty():
            print("Usage:")
            print("  ./scripts/run_lulesh_repeats.sh 5 hbm | python3 ./scripts/analyze_fom.py")
            print("  python3 ./scripts/analyze_fom.py <logfile>")
            sys.exit(0)
        content = sys.stdin.read()

    fom_values = parse_fom_values(content)
    analyze_fom(fom_values)

if __name__ == "__main__":
    main()
