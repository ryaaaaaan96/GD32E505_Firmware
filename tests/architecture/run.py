#!/usr/bin/env python3
"""Small boundary checks: no business task ownership in func; public aOS isolation."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
for path in (root / "func").rglob("*.c"):
    text = path.read_text(encoding="utf-8", errors="replace")
    assert not re.search(r"\b(aOSCreateTask|aOSDeleteTask|xTaskCreate|pthread_create)\s*\(", text), path
for folder in ("app", "device", "func"):
    for path in (root / folder).rglob("*.c"):
        text = path.read_text(encoding="utf-8", errors="replace")
        assert not re.search(r'#\s*include\s*[<"](?:FreeRTOS|task|semphr)\.h[>"]', text), path
subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-fsyntax-only",
                "-Iplatform/aOS/public", "-Iplatform/aLib/include", "-x", "c", "-"],
               input='#include "aOS.h"\n', text=True, cwd=root, check=True)
print("Task ownership/public OS header boundary checks passed")
