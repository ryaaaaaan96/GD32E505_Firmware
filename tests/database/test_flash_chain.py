#!/usr/bin/env python3
"""验证实际数据库、设备封装和 SPI 移植层之间的调用链。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
database = root / "func/aDataBase"
flash = root / "device/aDev_Flash25q"
with tempfile.TemporaryDirectory(prefix="database-spi-") as directory:
    executable = Path(directory) / "test"
    sources = [root / "tests/database/test_flash_chain.c",
               database / "aDataBase.c", database / "port/fal_storage_port.c",
               database / "backend/flash25q/aDataBase_flash25q.c"]
    sources += [database / "FlashDB/src" / name for name in
                ("fdb.c", "fdb_kvdb.c", "fdb_tsdb.c", "fdb_utils.c")]
    sources += [database / "FlashDB/port/fal/src" / name for name in
                ("fal.c", "fal_flash.c", "fal_partition.c")]
    sources += [flash / name for name in
                ("aDev_flash25q.c", "port/aDev_flash25q_spi_bus.c",
                 "port/aDev_flash25q_sfud_port.c", "SFUD/sfud/src/sfud.c",
                 "SFUD/sfud/src/sfud_sfdp.c")]
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
               "-fsanitize=undefined", "-fno-sanitize-recover=all"]
    for path in (database, database / "config", database / "FlashDB/inc",
                 database / "FlashDB/port/fal/inc",
                 database / "backend/flash25q", flash, flash / "config",
                 flash / "SFUD/sfud/inc", root / "config",
                 root / "platform/aLib/include",
                 root / "platform/aOS/public", root / "platform/aDrv/include"):
        command += ["-I", str(path)]
    objects = []
    for i, source in enumerate(sources):
        obj = Path(directory) / f"{i}.o"
        flags = ["-Wno-unused-parameter"] if "FlashDB" in source.parts else []
        subprocess.run(command + flags + ["-c", str(source), "-o", str(obj)],
                       check=True)
        objects.append(str(obj))
    subprocess.run(command + objects + ["-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=20)
