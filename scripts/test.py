#!/usr/bin/env python3
"""统一回归入口；详细输出保存在 build/tests，不上传或烧录设备。"""
from __future__ import annotations

import argparse
import json
import os
import signal
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
HOST = [
    "points/run.py",
    "architecture/run.py", "layout/run.py", "crc/run.py", "atomic/run.py",
    "aos/run.py", "fault/run.py", "led/run.py", "spi/run.py", "dma/run.py",
    "usart/run.py",
    "bus/run.py", "bus_events/run.py", "bus/test_sig_check.py",
    "bus/test_app_sig.py", "bus/test_shell_types.py", "memory/run.py",
    "flash25q/run.py", "flash25q/test_command.py", "database/run.py",
    "database/test_flash_chain.py", "comm/run.py", "modbus/run.py",
    "modbus_demo/run.py", "protocol_tasks/run.py", "shell/run.py",
    "log/run.py", "app_devices/run.py", "app_startup/run.py",
    "scripts/test_debug.py",
]
MATRIX = [
    "config/test_platform_build.py", "config/build_matrix.py",
    "flash25q/build_matrix.py", "database/build_matrix.py",
    "modbus/build_matrix.py", "log/build_matrix.py",
]


def execute(command: list[str], stream, env: dict, timeout: int) -> int:
    # 每组独立进程组；超时/中断时同时结束它创建的编译器和测试进程。
    with subprocess.Popen(command, cwd=ROOT, env=env, stdout=stream,
                          stderr=subprocess.STDOUT,
                          start_new_session=True) as process:
        try:
            return process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise


def commands(suite: str) -> dict[str, list[str]]:
    result = {}
    if suite in ("host", "all"):
        result["points/generated"] = [
            sys.executable, "scripts/generate_points.py", "--check"]
        result["performance/smoke"] = [
            sys.executable, "scripts/benchmark.py", "--quick",
            "--output", "build/benchmarks/smoke"]
        result["config/resolver"] = [
            "cmake", "-P", "tests/config/test_resolver.cmake"]
        for path in HOST:
            result[path.removesuffix(".py")] = [sys.executable, "tests/" + path]
    if suite in ("matrix", "all"):
        for path in MATRIX:
            result[path.removesuffix(".py")] = [sys.executable, "tests/" + path]
    if suite in ("firmware", "all"):
        for mode in ("build", "release"):
            result["firmware/" + mode] = [
                sys.executable, "scripts/build.py", mode, "-j", "4"]
        result["firmware/product"] = [sys.executable, "scripts/build.py",
            "release", "--profile", "product", "-j", "4"]
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("host", "firmware", "matrix", "all"),
                        default="host")
    parser.add_argument("--only", nargs="+", help="仅执行指定测试名，见 --list")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--timeout", type=int, default=1800,
                        help="每组测试的超时秒数，默认 1800")
    args = parser.parse_args()
    selected = commands(args.suite)
    if args.only:
        unknown = set(args.only) - selected.keys()
        if unknown:
            parser.error("未知测试：" + ", ".join(sorted(unknown)))
        selected = {name: selected[name] for name in args.only}
    if args.timeout <= 0:
        parser.error("--timeout 必须大于零")
    if args.list:
        print("\n".join(selected))
        return 0

    output = ROOT / "build/tests" / args.suite
    output.mkdir(parents=True, exist_ok=True)
    report = output / "results.json"
    report.unlink(missing_ok=True)
    env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}
    results = []
    for name, command in selected.items():
        log = output / (name.replace("/", "_") + ".log")
        started = time.monotonic()
        print(f"RUN  {name}", flush=True)
        try:
            with log.open("w", encoding="utf-8") as stream:
                try:
                    code = execute(command, stream, env, args.timeout)
                except (OSError, subprocess.TimeoutExpired) as error:
                    stream.write(f"\n{error}\n")
                    code = 1
        except KeyboardInterrupt:
            print("测试已中断；当前测试不计为通过。", file=sys.stderr)
            return 130
        result = {"name": name, "passed": code == 0, "exit_code": code,
                  "seconds": round(time.monotonic() - started, 2),
                  "log": str(log)}
        results.append(result)
        report.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        print(f"{'PASS' if code == 0 else 'FAIL'} {name} "
              f"({result['seconds']} s)", flush=True)
        if code != 0:
            print(f"  {log}", flush=True)
    failed = sum(not item["passed"] for item in results)
    print(f"{len(results) - failed}/{len(results)} passed; report: {report}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
