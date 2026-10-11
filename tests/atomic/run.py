#!/usr/bin/env python3
"""验证 GCC 原子封装的语义、并发和相对原生 C11 的代码生成。"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
cc = os.environ.get("CC", "gcc")
flags = [cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
         "-Iplatform/aLib/include"]
with tempfile.TemporaryDirectory(prefix="aclass-atomic-") as folder:
    exe = str(Path(folder) / "atomic")
    for optimization in ("-O0", "-O2"):
        subprocess.run(flags + [optimization, "-pthread", "-fsanitize=undefined",
                       "-fno-sanitize-recover=all",
                       *(["-fsanitize=address", "-fno-omit-frame-pointer",
                          "-fno-pie", "-no-pie"] if os.environ.get("SANITIZE") else []),
                       "tests/atomic/test_atomic.c", "-o", exe], cwd=root, check=True)
        subprocess.run([exe], check=True, timeout=20)

    # 与工具链原生接口逐项对照；该测试故意直接引用 stdatomic.h。
    source = r'''
#include "aAtomic.h"
#include <stdatomic.h>
unsigned wrapped_load(const aAtomicUint_t *p) {
    return aAtomicLoadExplicit(p, A_ATOMIC_RELAXED);
}
unsigned native_load(const atomic_uint *p) {
    return atomic_load_explicit(p, memory_order_relaxed);
}
void wrapped_store(aAtomicUint_t *p, unsigned v) {
    aAtomicStoreExplicit(p, v, A_ATOMIC_RELAXED);
}
void native_store(atomic_uint *p, unsigned v) {
    atomic_store_explicit(p, v, memory_order_relaxed);
}
void wrapped_init(aAtomicUint_t *p, unsigned v) { aAtomicInit(p, v); }
void native_init(atomic_uint *p, unsigned v) { atomic_init(p, v); }
unsigned wrapped_add(aAtomicUint_t *p, unsigned v) {
    return aAtomicFetchAddExplicit(p, v, A_ATOMIC_RELAXED);
}
unsigned native_add(atomic_uint *p, unsigned v) {
    return atomic_fetch_add_explicit(p, v, memory_order_relaxed);
}
'''
    assembly = subprocess.run(flags + ["-O2", "-fno-ipa-icf", "-S", "-x", "c",
                                      "-o", "-", "-"], input=source, text=True,
                              capture_output=True, cwd=root, check=True).stdout

    def instructions(name):
        match = re.search(rf"^{name}:\n(.*?)^\s*\.size\s+{name}\b",
                          assembly, re.M | re.S)
        assert match, assembly
        return [line.strip() for line in match[1].splitlines()
                if line.strip() and not line.strip().startswith((".", "#"))]

    for operation in ("load", "store", "init", "add"):
        assert instructions("wrapped_" + operation) == instructions("native_" + operation)
    print("aAtomic GCC optimized instructions match native C11")

    for define, error in (("__STDC_NO_ATOMICS__=1", "does not support C11 atomics"),
                          ("__clang__=1", "only GCC is implemented")):
        result = subprocess.run(flags + ["-D" + define, "-fsyntax-only", "-x", "c", "-"],
                                input='#include "aAtomic.h"\n', text=True,
                                capture_output=True, cwd=root)
        assert result.returncode != 0 and error in result.stderr, result.stderr
    print("aAtomic unsupported compiler/atomic capability rejected")
