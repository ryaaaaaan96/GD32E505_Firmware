#!/usr/bin/env python3
"""GD32E505 固件构建入口。

用法：
    python3 scripts/build.py
    python3 scripts/build.py release
    python3 scripts/build.py clean
    python3 scripts/build.py rebuild
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = PROJECT_ROOT / "build"
LEGACY_BUILD_DIRS = (
    PROJECT_ROOT / "build-app-main",
    PROJECT_ROOT / "build-layers",
    PROJECT_ROOT / "build-no-board",
    PROJECT_ROOT / "build-release",
    PROJECT_ROOT / "build-verify",
)
DEFAULT_ARM_GCC_ROOT = Path.home() / "Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3"


def require_program(name: str) -> None:
    if shutil.which(name) is None:
        raise RuntimeError(f"required program not found: {name}")


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)


def toolchain_root() -> Path | None:
    configured = os.environ.get("ARM_GCC_ROOT")
    root = (Path(configured).expanduser() if configured else DEFAULT_ARM_GCC_ROOT)
    if not root.exists() and configured is None:
        return None
    root = root.resolve()
    for name in ("gcc", "objcopy", "size"):
        executable = root / "bin" / f"arm-none-eabi-{name}"
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise RuntimeError(f"invalid ARM_GCC_ROOT, missing: {executable}")
    return root


def build(build_type: str, jobs: int | None) -> None:
    require_program("cmake")
    require_program("ninja")
    gcc_root = toolchain_root()
    if gcc_root is None:
        require_program("arm-none-eabi-gcc")

    build_dir = BUILD_ROOT / build_type
    configure = [
        "cmake",
        "-S",
        str(PROJECT_ROOT),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
    ]
    if gcc_root is not None:
        configure.append(f"-DARM_GCC_ROOT={gcc_root}")
    run(configure)

    compile_command = ["cmake", "--build", str(build_dir)]
    if jobs is not None:
        compile_command.extend(["--parallel", str(jobs)])
    run(compile_command)

    artifact_dir = build_dir / "bin"
    print(f"Artifacts: {artifact_dir}")


def clean() -> None:
    removed = False
    for build_dir in (BUILD_ROOT, *LEGACY_BUILD_DIRS):
        if not build_dir.exists():
            continue
        print(f"Removing {build_dir}")
        shutil.rmtree(build_dir)
        removed = True
    if not removed:
        print("Build directory is already clean.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "command",
        nargs="?",
        default="build",
        choices=("build", "release", "clean", "rebuild"),
    )
    parser.add_argument("-j", "--jobs", type=int, help="parallel build jobs")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.jobs is not None and args.jobs < 1:
            raise ValueError("--jobs must be greater than zero")
        if args.command == "clean":
            clean()
        elif args.command == "release":
            build("Release", args.jobs)
        elif args.command == "rebuild":
            clean()
            build("Debug", args.jobs)
        else:
            build("Debug", args.jobs)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
