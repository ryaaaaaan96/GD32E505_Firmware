#!/usr/bin/env python3
"""编译真实模块并测量主机基准；结果为 JSON/CSV，不代表 MCU 时间。"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def capture(command):
    return subprocess.check_output(command, cwd=ROOT, text=True,
                                   timeout=120).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quick", action="store_true",
                        help="缩短统计样本供 CI 使用")
    parser.add_argument("--output", type=Path, default=ROOT / "build/benchmarks")
    parser.add_argument("--baseline", type=Path, help="对比旧 results.json，中位耗时比值")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "scripts/generate_points.py", "--check"],
                   cwd=ROOT, check=True)
    flags = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Wpedantic",
             "-Werror", "-pthread", "-ffunction-sections", "-fdata-sections",
             "-Wl,--gc-sections", "-Wl,--fatal-warnings",
             "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld",
             "-DABUS_STATIC_ENABLE=0", "-DABUS_DYNAMIC_ENABLE=1",
             "-DABUS_EVENT_ENABLE=1", "-DABUS_DEF_CHECK_ENABLE=1",
             "-DAMODBUS_CLIENT_ENABLE=1", "-DAMODBUS_SERVER_ENABLE=0",
             "-DNMBS_SERVER_DISABLED", "-DAMODBUS_STATIC_ENABLE=0",
             "-DAMODBUS_DYNAMIC_ENABLE=1", "-DACOMM_STATIC_ENABLE=0",
             "-DACOMM_DYNAMIC_ENABLE=1"]
    for folder in ("benchmarks", "platform/aLib/include", "platform/aOS/public",
                   "func/aBus/include", "func/aComm/include", "func/aComm/src",
                   "func/aModbus/include", "func/aModbus/src",
                   "func/aModbus/nanoMODBUS", "app/protocol"):
        flags += ["-I" + folder]
    sources = ["benchmarks/bench_bus.c", "benchmarks/bench_plan.c",
               "benchmarks/host_port.c", "benchmarks/host_main.c",
               "func/aBus/src/aBus.c", "func/aBus/src/aBus_event.c",
               "func/aBus/src/aBus_endpoint.c",
               "func/aComm/src/aComm.c", "func/aComm/src/aComm_table.c",
               "func/aComm/src/aComm_channel.c",
               "func/aComm/src/aComm_endpoint.c",
               "func/aComm/src/aComm_binding.c",
               *[str(p.relative_to(ROOT)) for p in sorted(
                   (ROOT / "func/aModbus/src").glob("*.c"))],
               "func/aModbus/nanoMODBUS/nanomodbus.c"]
    compiler = shlex.split(args.cc)
    rows = []
    commands = []
    binaries = {}
    for lock in (0, 1, 2):
        executable = output / f"benchmark-lock-{lock}"
        command = compiler + flags + [f"-DABUS_LOCK_MODE={lock}"] + sources
        command += ["-o", str(executable)]
        commands.append(command)
        subprocess.run(command, cwd=ROOT, check=True)
        binaries[executable.name] = hashlib.sha256(
            executable.read_bytes()).hexdigest()
        raw = capture([str(executable)] + (["--quick"] if args.quick else []))
        (output / f"lock-{lock}.jsonl").write_text(raw + "\n")
        for line in raw.splitlines():
            rows.append({"lock_mode": lock, **json.loads(line)})
        print(f"lock={lock}: bus + generated Modbus plan passed", flush=True)
    report = {
        "format_version": 1,
        "measured_at_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(), "machine": platform.machine(),
        "compiler": capture(compiler + ["--version"]).splitlines()[0],
        "binaries_sha256": binaries,
        "revision": capture(["git", "rev-parse", "HEAD"]),
        "dirty": bool(capture(["git", "status", "--porcelain"])),
        "points_sha256": hashlib.sha256((ROOT /
            "app/protocol/config/points.json").read_bytes()).hexdigest(),
        "quick": args.quick, "commands": commands,
        "clock": "CLOCK_MONOTONIC, ns; paired timing overhead not subtracted",
        "statistic": "bus: per-batch mean ns/op; percentiles across batches",
        "scope": "single thread; real uncontended pthread mutex; no I/O",
        "results": rows,
    }
    if args.baseline:
        old = json.loads(args.baseline.read_text())
        def key(row):
            return (row["lock_mode"], row.get("operation"), row.get("points"),
                    row.get("bytes"))
        previous = {key(r): r for r in old["results"] if r["kind"] == "bus"}
        report["comparison"] = [{"lock_mode": r["lock_mode"],
            "operation": r["operation"], "points": r["points"],
            "bytes": r["bytes"], "median_ratio": round(
                r["median_ns"] / previous[key(r)]["median_ns"], 3)}
            for r in rows if r["kind"] == "bus" and key(r) in previous
            and previous[key(r)]["median_ns"] > 0]
        report["comparison_note"] = (
            "仅同一机器、编译配置及相近负载可比较；比例不是性能门槛")
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    fields = list(dict.fromkeys(field for row in rows for field in row))
    with (output / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"Report: {output / 'results.json'}")


if __name__ == "__main__":
    main()
