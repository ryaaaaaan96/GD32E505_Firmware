#!/usr/bin/env python3
"""检查可选布局断言和 SIG 长度求和，包括 Release 与预期编译失败。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
prefix = '''
#include "aLayout.h"
#include <stdint.h>
typedef struct {
    uint16_t mode;
    uint16_t reserved;
    uint32_t speed;
} Example_t;
'''
cases = (
    ("valid", '''
ALIB_STATIC_ASSERT(sizeof(uint16_t) == 2U, "uint16_t size");
ALIB_ASSERT_TYPE_SIZE(Example_t,
    sizeof(((Example_t *)0)->mode) +
    sizeof(((Example_t *)0)->reserved) +
    sizeof(((Example_t *)0)->speed));
ALIB_ASSERT_MEMBER_OFFSET(Example_t, speed, 4U);
''', None),
    ("wrong_size", 'ALIB_ASSERT_TYPE_SIZE(Example_t, 7U);',
     "Example_t: unexpected type size"),
    ("wrong_offset", 'ALIB_ASSERT_MEMBER_OFFSET(Example_t, speed, 2U);',
     "Example_t.speed: unexpected member offset"),
    ("omitted_member", '''
ALIB_ASSERT_TYPE_SIZE(Example_t,
    sizeof(((Example_t *)0)->mode) +
    sizeof(((Example_t *)0)->speed));
''', "Example_t: unexpected type size"),
    ("padding", '''
typedef struct { uint32_t value; uint8_t state; } Padded_t;
ALIB_ASSERT_TYPE_SIZE(Padded_t,
    sizeof(((Padded_t *)0)->value) + sizeof(((Padded_t *)0)->state));
''', "Padded_t: unexpected type size"),
    ("false_condition", 'ALIB_STATIC_ASSERT(0, "layout test failure");',
     "layout test failure"),
)

with tempfile.TemporaryDirectory(prefix="aclass-layout-") as directory:
    for compiler, standard, language in (
            ("cc", "c11", "c"), ("c++", "c++11", "c++")):
        for release in (False, True):
            flags = [compiler, f"-std={standard}", "-Wall", "-Wextra",
                     "-Wpedantic", "-Werror", "-Iplatform/aLib/include"]
            if release:
                flags += ["-DNDEBUG", "-O2"]
            for name, body, diagnostic in cases:
                result = subprocess.run(
                    flags + ["-fsyntax-only", "-x", language, "-"],
                    cwd=root, input=prefix + body, text=True,
                    capture_output=True)
                if diagnostic is None:
                    assert result.returncode == 0, result.stderr
                else:
                    assert result.returncode != 0, name
                    assert diagnostic in result.stderr, result.stderr
            print(f"Layout assertions {standard} NDEBUG={release} passed")

    for release in (False, True):
        executable = str(Path(directory) / f"param-size-{release}")
        flags = ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
                 "-Werror", "-fsanitize=undefined",
                 "-fno-sanitize-recover=all", "-Iplatform/aLib/include",
                 "-Ifunc/aBus/include"]
        if release:
            # 工具仅借用定义头，关闭运行库/定义检查不影响显式调用。
            flags += ["-DNDEBUG", "-O2", "-DABUS_DEF_CHECK_ENABLE=0",
                      "-DABUS_STATIC_ENABLE=0", "-DABUS_DYNAMIC_ENABLE=0",
                      "-DABUS_LOCK_MODE=99"]
        subprocess.run(flags + ["tests/layout/test_param_size.c",
                               "-o", executable], cwd=root, check=True)
        subprocess.run([executable], check=True)
        print(f"SIG parameter size check NDEBUG={release} passed")

with tempfile.TemporaryDirectory(prefix="aclass-range-") as directory:
    executable = str(Path(directory) / "range")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
                    "-Werror", "-fsanitize=undefined",
                    "-fno-sanitize-recover=all", "-Iplatform/aLib/include",
                    "tests/layout/test_range.c", "-o", executable],
                   cwd=root, check=True)
    subprocess.run([executable], check=True)
