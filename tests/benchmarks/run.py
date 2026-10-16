#!/usr/bin/env python3
"""基准正确性故障注入，以及历史性能报告的比较条件。"""
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from benchmark import compare_results


class BenchmarkTest(unittest.TestCase):
    def test_faults_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="aclass-bench-") as folder:
            executable = str(Path(folder) / "test")
            command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra",
                       "-Wpedantic", "-Werror", "-pthread",
                       "-fsanitize=undefined", "-fno-sanitize-recover=all",
                       "-DABUS_EVENT_ENABLE=0", "-DABUS_LOCK_MODE=1",
                       "-DABUS_STATIC_ENABLE=0", "-DABUS_DYNAMIC_ENABLE=1",
                       "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld"]
            for include in ("benchmarks", "func/aBus/include",
                            "platform/aLib/include", "platform/aOS/public"):
                command += ["-I" + include]
            for name in ("SetByIndex", "GetByIndex", "SetByKey", "GetByKey"):
                command += ["-Wl,--wrap=aBus" + name]
            command += ["tests/benchmarks/test_bus.c", "benchmarks/bench_bus.c",
                        "benchmarks/host_port.c", "func/aBus/src/aBus.c",
                        "-o", executable]
            subprocess.run(command, cwd=ROOT, check=True)
            for fault in range(10):
                with self.subTest(fault=fault):
                    subprocess.run([executable, str(fault)],
                                   check=True, timeout=20)

    def test_comparison_conditions(self):
        report = {
            "format_version": 2, "workload_version": 2,
            "platform": "Linux", "machine": "x86_64",
            "environment": {"host": "bench", "cpu": "test", "logical_cpus": 4},
            "compiler": "gcc", "compiler_command": ["cc"],
            "build_flags": ["-O2"], "points_sha256": "points",
            "quick": False, "clock": "ns", "statistic": "batch mean",
            "scope": "single thread", "results": [{
                "kind": "bus", "lock_mode": 1, "operation": "get_index",
                "points": 16, "bytes": 4, "samples": 256, "batch": 32,
                "median_ns": 10}]
        }
        old = copy.deepcopy(report)
        old["results"][0]["median_ns"] = 20
        self.assertEqual(compare_results(report, old)["ratios"][0]
                         ["median_ratio"], 0.5)
        for name, value in (("quick", True), ("workload_version", 1),
                            ("compiler", "clang"), ("build_flags", ["-O0"]),
                            ("environment", {"host": "another"}),
                            ("points_sha256", "other points")):
            previous = {**old, name: value}
            comparison = compare_results(report, previous)
            self.assertEqual(comparison["status"], "incompatible")
            self.assertEqual(comparison["ratios"], [])
            self.assertIn(name, comparison["mismatches"])
        for name in ("samples", "batch"):
            previous = copy.deepcopy(old)
            previous["results"][0][name] //= 2
            self.assertEqual(compare_results(report, previous)["ratios"], [])
        old.pop("workload_version")
        self.assertEqual(compare_results(report, old)["status"], "incompatible")


if __name__ == "__main__":
    unittest.main(verbosity=2)
