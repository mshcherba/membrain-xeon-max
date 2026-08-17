#!/usr/bin/env python3
"""
Unit tests for the MemBrain Benchmark Suite statistical computation and runner.
"""

import unittest
import math
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MEMBRAIN_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
sys.path.insert(0, os.path.join(MEMBRAIN_ROOT, "scripts"))

from run_benchmark_suite import compute_statistics, BenchmarkSuiteRunner


class TestBenchmarkSuiteStats(unittest.TestCase):

    def test_single_value(self):
        stats = compute_statistics([100.0])
        self.assertEqual(stats["n"], 1)
        self.assertEqual(stats["mean"], 100.0)
        self.assertEqual(stats["stdev"], 0.0)
        self.assertEqual(stats["stderr"], 0.0)
        self.assertEqual(stats["ci_margin"], 0.0)

    def test_multiple_values(self):
        values = [100.0, 102.0, 98.0, 101.0, 99.0]
        stats = compute_statistics(values)
        self.assertEqual(stats["n"], 5)
        self.assertAlmostEqual(stats["mean"], 100.0, places=4)
        self.assertAlmostEqual(stats["min"], 98.0, places=4)
        self.assertAlmostEqual(stats["max"], 102.0, places=4)
        self.assertAlmostEqual(stats["median"], 100.0, places=4)
        # Expected sample variance = ((0)^2 + (2)^2 + (-2)^2 + (1)^2 + (-1)^2) / 4 = 10 / 4 = 2.5
        # Expected stdev = sqrt(2.5) ~= 1.58113883
        expected_stdev = math.sqrt(2.5)
        expected_stderr = expected_stdev / math.sqrt(5)
        self.assertAlmostEqual(stats["stdev"], expected_stdev, places=4)
        self.assertAlmostEqual(stats["stderr"], expected_stderr, places=4)
        self.assertTrue(stats["ci_margin"] > 0)
        self.assertTrue(stats["ci_margin_pct"] > 0)

    def test_lulesh_config_loading(self):
        runner = BenchmarkSuiteRunner(
            benchmark_name="lulesh",
            repeats=2,
            dry_run=True,
            skip_rss=True
        )
        self.assertEqual(runner.repeats, 2)
        self.assertEqual(runner.fom_unit, "z/s")
        exps = runner.build_experiments(peak_rss_kb=50000000)
        # 2 unconstrained + 3 capacities * 4 strategies = 14
        self.assertEqual(len(exps), 14)

        # Baseline check
        hbm_baseline = [e for e in exps if e["id"] == "unconstrained_hbm"][0]
        self.assertTrue(hbm_baseline["is_baseline"])
        self.assertEqual(hbm_baseline["numa_mode"], "membind_2")

        # DDR check
        ddr_cfg = [e for e in exps if e["id"] == "unconstrained_ddr"][0]
        self.assertEqual(ddr_cfg["numa_mode"], "membind_0")

        # First-touch check
        ft_cfg = [e for e in exps if e["id"] == "12.5pct_first_touch"][0]
        self.assertEqual(ft_cfg["strategy"], "first-touch")
        self.assertEqual(ft_cfg["numa_mode"], "preferred_2")


if __name__ == "__main__":
    unittest.main()
