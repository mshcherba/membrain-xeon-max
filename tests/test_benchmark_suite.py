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
            repeats=2
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
        self.assertFalse(hbm_baseline["use_runtime"])
        self.assertIsNone(hbm_baseline["guidance_file"])

        # DDR check
        ddr_cfg = [e for e in exps if e["id"] == "unconstrained_ddr"][0]
        self.assertEqual(ddr_cfg["numa_mode"], "membind_0")
        self.assertFalse(ddr_cfg["use_runtime"])
        self.assertIsNone(ddr_cfg["guidance_file"])

        # First-touch check
        ft_cfg = [e for e in exps if e["id"] == "12.5pct_first_touch"][0]
        self.assertEqual(ft_cfg["strategy"], "first-touch")
        self.assertEqual(ft_cfg["numa_mode"], "preferred_2")
        self.assertTrue(ft_cfg["use_runtime"])
        self.assertIsNotNone(ft_cfg["guidance_file"])

    def test_bfs_config_loading(self):
        runner = BenchmarkSuiteRunner(
            benchmark_name="bfs",
            repeats=3
        )
        self.assertEqual(runner.repeats, 3)
        self.assertEqual(runner.fom_unit, "TEPS")
        self.assertEqual(runner.cli_args, ["25", "-A", "-C", "-n", "64"])
        # Verify common environment variables inherited
        self.assertEqual(runner.config.get("environment", {}).get("OMP_NUM_THREADS"), "32")
        self.assertEqual(runner.config.get("environment", {}).get("KMP_AFFINITY"), "granularity=fine,compact,1,0")

        exps = runner.build_experiments(peak_rss_kb=30000000)
        self.assertEqual(len(exps), 14)

    def test_bfs_metric_parsing(self):
        runner = BenchmarkSuiteRunner(
            benchmark_name="bfs"
        )
        sample_output = """
        construction_time:              4.123456 s
        Running BFS 0
        Time for BFS 0 is 0.051234
        harmonic_mean_TEPS:             1.23456789e+08
        Maximum resident set size (kbytes): 1234567
        """
        metrics = runner.parse_metrics(sample_output)
        self.assertAlmostEqual(metrics["fom"], 1.23456789e+08)
        self.assertAlmostEqual(metrics["construction_time"], 4.123456)
        self.assertEqual(metrics["max_rss_kb"], 1234567)

    def test_qmcpack_config_loading(self):
        runner = BenchmarkSuiteRunner(
            benchmark_name="qmcpack",
            repeats=3
        )
        self.assertEqual(runner.repeats, 3)
        self.assertEqual(runner.fom_unit, "1/s")
        self.assertEqual(runner.cli_args, ["NiO-fcc-S64.xml"])
        expected_work_dir = os.path.join(MEMBRAIN_ROOT, "benchmarks", "qmcpack")
        self.assertEqual(runner.working_dir, expected_work_dir)

        # Verify Intel MKL is included in execution environment LD_LIBRARY_PATH
        env = runner._build_execution_env()
        self.assertIn("mkl", env.get("LD_LIBRARY_PATH", ""))

        exps = runner.build_experiments(peak_rss_kb=40000000)
        self.assertEqual(len(exps), 14)

    def test_qmcpack_metric_parsing(self):
        runner = BenchmarkSuiteRunner(
            benchmark_name="qmcpack"
        )
        sample_output = """
          Initialization Execution time = 10.50 secs
          VMC Execution time = 5.25 secs
          DMC Execution time = 25.00 secs
          Total Execution time = 40.75 secs
        Maximum resident set size (kbytes): 41943040
        """
        metrics = runner.parse_metrics(sample_output)
        self.assertAlmostEqual(metrics["dmc_sec"], 25.0)
        self.assertAlmostEqual(metrics["elapsed_sec"], 25.0)
        self.assertAlmostEqual(metrics["total_sec"], 40.75)
        self.assertAlmostEqual(metrics["init_sec"], 10.50)
        # FOM should be computed from fom_formula: 1.0 / dmc_sec = 0.04
        self.assertAlmostEqual(metrics["fom"], 0.04)
        self.assertEqual(metrics["max_rss_kb"], 41943040)
        self.assertAlmostEqual(metrics["max_rss_mb"], 40960.0)


if __name__ == "__main__":
    unittest.main()
