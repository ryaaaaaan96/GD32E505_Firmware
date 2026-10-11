#!/usr/bin/env python3
"""验证点表构建检查能发现跨文件重复，且允许不同设备使用同一 Key。"""
import importlib.util
from pathlib import Path
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    'check_sig', root / 'scripts/check_sig.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

with tempfile.TemporaryDirectory() as folder:
    table = Path(folder) / 'table.inc'
    child = Path(folder) / 'child.inc'
    table.write_text('ABUS_SIG(A, 1U, .size = 4)\n#include "child.inc"\n')
    child.write_text('ABUS_SIG(B, 0x2UL, .size = 4)\n')
    assert checker.check(table) == 2
    other = Path(folder) / 'other.inc'
    other.write_text('ABUS_SIG(A, 1U, .size = 4)\n')
    assert checker.check(other) == 1
    for invalid in ('ABUS_SIG(B, 1U, .size = 4)',
                    'ABUS_BOUND_SIG(B, 1U, uint32_t, .flags = 0)',
                    'ABUS_SIG(A, 2U, .size = 4)',
                    'ABUS_SIG(B, 65536U, .size = 4)',
                    'ABUS_SIG(B, -1, .size = 4)',
                    'ABUS_SIG(B, SOME_KEY, .size = 4)',
                    'ABUS_SIG(B, 010U, .size = 4)',
                    '#include "table.inc"'):
        child.write_text(invalid)
        try:
            checker.check(table)
        except ValueError:
            pass
        else:
            raise AssertionError(invalid)
print('SIG keys: nested duplicates, device isolation and literals passed')
