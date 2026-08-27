#!/usr/bin/env python3
"""从 WSL 连接 Windows 宿主机或远程 GDB Server 调试 GD32E505VET7。

直接运行时可交互选择 Windows/WSL 宿主机或其他远程主机：

    python3 scripts/debug.py

也可以通过参数跳过交互：

    python3 scripts/debug.py --mode wsl-host
    python3 scripts/debug.py --mode remote --host 192.168.1.100 --port 2331
    python3 scripts/debug.py --mode wsl-host --attach
    python3 scripts/debug.py --mode wsl-host --dry-run

默认会复位目标、下载 Debug ELF、复位后运行至 main。--attach 不复位、不下载，
只连接并停止目标。
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = PROJECT_ROOT / "build"
FIRMWARE_NAME = "gd32e505vet7_debug"
DEFAULT_GDB_SERVER = "jlink"
JLINK_DEVICE = "GD32E505VET6"
DEFAULT_ARM_GCC_ROOT = (
    Path.home() / "Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3"
)
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 2331


def find_elf(build_type: str) -> Path:
    elf = BUILD_ROOT / build_type / "bin" / f"{FIRMWARE_NAME}.elf"
    if not elf.is_file():
        raise RuntimeError(
            f"ELF not found: {elf}\n"
            f"Run 'python3 scripts/build.py {build_command(build_type)}' first."
        )
    return elf.resolve()


def build_command(build_type: str) -> str:
    return "release" if build_type == "Release" else "build"


def find_gdb() -> Path:
    configured = os.environ.get("ARM_GCC_ROOT")
    roots = []
    if configured:
        roots.append(Path(configured).expanduser())
    roots.append(DEFAULT_ARM_GCC_ROOT)

    for root in roots:
        gdb = root / "bin" / "arm-none-eabi-gdb"
        if gdb.is_file() and os.access(gdb, os.X_OK):
            return gdb.resolve()

    for name in ("arm-none-eabi-gdb", "gdb-multiarch"):
        executable = shutil.which(name)
        if executable:
            return Path(executable).resolve()

    raise RuntimeError(
        "GDB not found; set ARM_GCC_ROOT or install arm-none-eabi-gdb."
    )


def gdb_quote(path: Path) -> str:
    value = str(path).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{value}"'


def endpoint(host: str, port: int) -> str:
    if ":" in host and not host.startswith("["):
        return f"[{host}]:{port}"
    return f"{host}:{port}"


def write_gdb_script(
    elf: Path,
    host: str,
    port: int,
    server: str,
    attach: bool,
    continue_to_main: bool,
) -> Path:
    script_dir = BUILD_ROOT / ".debug"
    script_dir.mkdir(parents=True, exist_ok=True)
    script = script_dir / "gd32e505vet7.gdb"

    commands = [
        "set pagination off",
        "set confirm off",
        f"file {gdb_quote(elf)}",
        f"target remote {endpoint(host, port)}",
    ]

    if server == "jlink":
        commands.append(f"monitor device {JLINK_DEVICE}")

    if attach:
        commands.append("monitor halt")
    else:
        commands.extend(
            [
                "monitor reset",
                "load",
            ]
        )
        if continue_to_main:
            if server == "jlink":
                commands.extend(
                    [
                        "set $aclass_main = ((unsigned long)&main) & ~1",
                        'eval "monitor setbp 0x%lx 0x1", $aclass_main',
                        "continue",
                        "monitor clrbp",
                    ]
                )
            else:
                commands.extend(["hbreak main", "continue"])

    script.write_text("\n".join(commands) + "\n", encoding="utf-8")
    return script


def run_build(build_type: str) -> None:
    command = [
        sys.executable,
        str(PROJECT_ROOT / "scripts" / "build.py"),
        build_command(build_type),
    ]
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)


def detect_wsl_host() -> tuple[str, str]:
    wslinfo = shutil.which("wslinfo")
    if wslinfo:
        result = subprocess.run(
            [wslinfo, "--networking-mode"],
            capture_output=True,
            check=False,
            text=True,
        )
        if result.stdout.strip().lower() == "mirrored":
            return DEFAULT_HOST, "WSL mirrored networking"

    ip = shutil.which("ip")
    if ip:
        result = subprocess.run(
            [ip, "route", "show", "default"],
            capture_output=True,
            check=False,
            text=True,
        )
        fields = result.stdout.split()
        if "via" in fields:
            index = fields.index("via") + 1
            if index < len(fields):
                return fields[index], "WSL NAT default gateway"

    return DEFAULT_HOST, "localhost fallback"


def read_connection_mode() -> str:
    print("选择调试服务器位置：")
    print("  1. Windows 宿主机（脚本运行在 WSL）")
    print("  2. 其他远程主机")

    while True:
        choice = input("请选择 [1/2]: ").strip().lower()
        if choice in ("1", "wsl", "wsl-host", "host"):
            return "wsl-host"
        if choice in ("2", "remote", "r"):
            return "remote"
        print("输入无效，请输入 1 或 2。")


def select_endpoint(args: argparse.Namespace) -> tuple[str, int, str]:
    mode = args.mode or read_connection_mode()
    port = args.port

    if mode == "wsl-host":
        detected_host, reason = detect_wsl_host()
        host = args.host or detected_host
        print(f"WSL host: {host} ({reason})")
        return (
            host,
            port if port is not None else DEFAULT_PORT,
            mode,
        )

    host = args.host
    if not host:
        host = input("Remote host/IP: ").strip()
    if not host:
        raise ValueError("remote host cannot be empty")

    if port is None:
        value = input(f"GDB Server port [{DEFAULT_PORT}]: ").strip()
        port = int(value) if value else DEFAULT_PORT
    return host, port, mode


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--mode",
        choices=("wsl-host", "remote"),
        help="connection mode; omit to select interactively",
    )
    parser.add_argument("--host", help="GDB Server address")
    parser.add_argument(
        "--port", type=int, help=f"GDB Server port (default: {DEFAULT_PORT})"
    )
    parser.add_argument(
        "--server",
        choices=("jlink", "openocd"),
        default=DEFAULT_GDB_SERVER,
        help=f"GDB Server type (default: {DEFAULT_GDB_SERVER})",
    )
    parser.add_argument(
        "--build-type",
        choices=("Debug", "Release"),
        default="Debug",
        help="ELF configuration to debug",
    )
    parser.add_argument(
        "--build", action="store_true", help="build firmware before connecting"
    )
    parser.add_argument(
        "--attach",
        action="store_true",
        help="attach and halt without resetting or downloading",
    )
    parser.add_argument(
        "--no-continue",
        action="store_true",
        help="download and halt without continuing to main",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="generate the GDB script without connecting",
    )
    parser.add_argument(
        "gdb_args",
        nargs=argparse.REMAINDER,
        help="extra arguments passed to GDB after '--'",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        host, port, mode = select_endpoint(args)
        if not 1 <= port <= 65535:
            raise ValueError("--port must be between 1 and 65535")
        if args.build:
            run_build(args.build_type)

        elf = find_elf(args.build_type)
        gdb = find_gdb()
        script = write_gdb_script(
            elf,
            host,
            port,
            args.server,
            args.attach,
            not args.no_continue,
        )
        gdb_args = args.gdb_args
        if gdb_args[:1] == ["--"]:
            gdb_args = gdb_args[1:]
        command = [str(gdb), "-x", str(script), *gdb_args]

        print(f"Mode:   {mode}")
        print(f"Server: {args.server}")
        print(f"Target: {endpoint(host, port)}")
        print(f"ELF:    {elf}")
        print(f"GDB:    {gdb}")
        print(f"Script: {script}")
        print("+", " ".join(command), flush=True)

        if args.dry_run:
            return 0

        process = subprocess.Popen(command, cwd=PROJECT_ROOT)
        previous_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
        try:
            return process.wait()
        finally:
            signal.signal(signal.SIGINT, previous_handler)
    except (
        EOFError,
        KeyboardInterrupt,
        OSError,
        RuntimeError,
        ValueError,
        subprocess.CalledProcessError,
    ) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
