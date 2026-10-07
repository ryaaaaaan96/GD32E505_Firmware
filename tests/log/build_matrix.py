#!/usr/bin/env python3
"""验证日志禁用、独立后端和静态等级裁剪的 Release 固件。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
default_toolchain = (Path.home() / "Tools/toolchain/mcu_arm_toolchain"
                     / "arm-none-eabi-15.3")
toolchain = Path(os.environ.get("ARM_GCC_ROOT", str(default_toolchain)))
profiles = {
    "disabled": ["set(ALOG_ENABLE OFF)"],
    "independent": ["set(ASHELL_ENABLE OFF)", "set(ADATABASE_ENABLE OFF)",
                    "set(ADEV_FLASH25Q_ENABLE OFF)"],
    "warn_only": ["set(ALOG_OUTPUT_LEVEL 2)"],
    "large_line": ["set(ALOG_LINE_BUFFER_SIZE 512)"],
}

with tempfile.TemporaryDirectory(prefix="aclass-log-matrix-") as directory:
    base = Path(directory)
    for name, overrides in profiles.items():
        config = base / f"{name}.cmake"
        config.write_text(f'include("{root}/config/aclass_config.cmake")\n'
                          + "\n".join(overrides) + "\n")
        build = base / name
        for command in (
            ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", f"-DARM_GCC_ROOT={toolchain}",
             f"-DACLASS_CONFIG_FILE={config}"],
            ["cmake", "--build", str(build), "--parallel", "4"],
        ):
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                print(result.stdout, result.stderr)
                raise SystemExit(result.returncode)
        symbols = subprocess.check_output([
            str(toolchain / "bin/arm-none-eabi-nm"),
            str(build / "lib/libaLog.a")], text=True)
        assert (" T elog_output" in symbols) == (name != "disabled")
        assert "elog_async_output" not in symbols
        assert "elog_buf_output" not in symbols
        assert "pthread" not in symbols
        if name == "disabled":
            assert " U aOSMutex" not in symbols
        print(f"{name}: Release build and log archive checks passed")
