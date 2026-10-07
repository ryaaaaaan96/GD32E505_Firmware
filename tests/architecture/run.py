#!/usr/bin/env python3
"""检查自有代码层次和公共头自包含性，不把上游示例当作产品代码。"""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
# 精确排除上游树；模块的适配源码和配置仍参与检查。
vendors = tuple(root / path for path in (
    "func/aDataBase/FlashDB", "func/aLog/EasyLogger",
    "func/aModbus/nanoMODBUS", "func/aShell/nr_micro_shell",
    "device/aDev_Flash25q/SFUD",
))
for folder in ("app", "device", "func", "platform/aLib"):
    for path in (root / folder).rglob("*"):
        if path.suffix not in (".c", ".h") or any(
                path.is_relative_to(vendor) for vendor in vendors):
            continue
        text = path.read_text(encoding="utf-8")
        if folder == "func":
            assert not re.search(
                r"\b(aOSCreateTask|aOSDeleteTask|xTaskCreate|pthread_create)"
                r"\s*\(", text), path
        assert not re.search(
            r'#\s*include\s*[<"](?:FreeRTOS|task|semphr|gd32[^/]*|'
            r'core_cm\w*)\.h[>"]', text), path
        if folder != "app":
            assert not re.search(
                r'#\s*include\s*[<"](?:app/|app_)', text), path
        if folder == "platform/aLib":
            assert not re.search(
                r'#\s*include\s*[<"](?:aOS|aDrv|aDev|aBus|aMemory|'
                r'aDataBase|aShell|aLog|aModbus)', text), path

# 业务公共头不需要厂商头、OS 内核头或实例私有布局。
includes = ["platform/aLib/include", "platform/aOS/public",
            "platform/aDrv/include", "device/aDev_usart/include",
            "device/aDev_Flash25q", "func/aBus/include", "func/aMemory",
            "func/aDataBase", "func/aShell/include", "func/aLog",
            "func/aModbus"]
headers = ("aOS.h", "aDrv.h", "aDev_usart.h", "aDev_flash25q.h",
           "aBus.h", "aMemory.h", "aDataBase.h", "aShell.h", "aLog.h",
           "aModbus.h")
command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-fsyntax-only", *["-I" + path for path in includes],
           "-x", "c", "-"]
for header in headers:
    subprocess.run(command, input=f'#include "{header}"\n',
                   text=True, cwd=root, check=True)
# 只读定义与数据库不消费 aBus 的分配/锁配置。
subprocess.run(command + ["-DABUS_STATIC_ENABLE=0", "-DABUS_DYNAMIC_ENABLE=0",
                          "-DABUS_LOCK_MODE=99"],
               input='#include "aBus_table.h"\n#include "aDataBase.h"\n',
               text=True, cwd=root, check=True)
print("Architecture/public header/metadata isolation checks passed")
