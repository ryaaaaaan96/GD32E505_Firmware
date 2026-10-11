#!/usr/bin/env python3
"""用标准向量和 zlib 验证 aLib CRC32，包含分段、初始 CRC 和未对齐输入。"""
from pathlib import Path
import subprocess
import tempfile
import zlib

root = Path(__file__).resolve().parents[2]
data = bytes((i * 37 + 11) & 255 for i in range(1025))
checks = []
for offset in (0, 1):
    for size in (0, 1, 2, 15, 16, 31, 32, 255, 256, 1024):
        for seed in (0, 0x12345678, 0xFFFFFFFF):
            expected = zlib.crc32(data[offset:offset + size], seed)
            checks.append(f"assert(aCrc32({seed}U, data + {offset}, {size}U)"
                          f" == {expected}U);")
source = r'''
#include "aCrc.h"
#include <assert.h>
int main(void)
{
    uint8_t data[1025];
    for (size_t i = 0; i < sizeof(data); ++i)
        data[i] = (uint8_t)(i * 37U + 11U);
    assert(aCrc32(0U, "123456789", 9U) == 0xCBF43926U);
    assert(aCrc32(0U, NULL, 0U) == 0U);
    assert(aCrc32(0x12345678U, NULL, 0U) == 0x12345678U);
''' + "\n".join(checks) + r'''
    for (size_t split = 0; split <= sizeof(data); ++split) {
        uint32_t crc = aCrc32(0U, data, split);
        crc = aCrc32(crc, data + split, sizeof(data) - split);
        assert(crc == aCrc32(0U, data, sizeof(data)));
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="aclass-crc-") as temp:
    folder = Path(temp)
    for compiler, standard, suffix in (("cc", "c11", "c"),
                                       ("c++", "c++11", "cpp")):
        path = folder / f"test.{suffix}"
        path.write_text(source)
        executable = folder / f"test-{suffix}"
        subprocess.run([
            compiler, f"-std={standard}", "-Wall", "-Wextra", "-Wpedantic",
            "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all",
            "-I", str(root / "platform/aLib/include"), str(path),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
print("aLib CRC32: C11/C++11 vectors, zlib, chaining and unaligned input passed")
