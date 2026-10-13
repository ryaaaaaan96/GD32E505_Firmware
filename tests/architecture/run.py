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
# 应用头按实际文件识别，去掉 app_ 文件名前缀后仍禁止底层反向依赖。
app_headers = {path.name for path in (root / "app").rglob("*.h")}
for folder in ("app", "device", "func", "platform/aLib"):
    for path in (root / folder).rglob("*"):
        if path.suffix not in (".c", ".h", ".inc") or any(
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
            for header in re.findall(r'#\s*include\s*[<"]([^>"]+)[>"]',
                                     text):
                assert Path(header).name not in app_headers, (path, header)
        if path.is_relative_to(root / "func/aComm"):
            assert not re.search(
                r'#\s*include\s*[<"](?:aBus|aModbus|aDrv|aDev)', text), path
            assert not re.search(r'\b(?:AMODBUS_\w*|aModbus\w*|'
                                 r'ABUS_\w*|aBus\w*)\b', text), path
        if path.is_relative_to(root / "device/aDev_usart"):
            assert not re.search(r'\b(?:interrupt_priority|irq_priority|'
                                 r'aOSValidateIsrPriority)\b', text), path
        if path.is_relative_to(root / "func/aModbus"):
            assert not re.search(r'#\s*include\s*[<"]aBus', text), path
            if path.name not in ("aModbus_endpoint.c", "aModbus_endpoint.h"):
                assert not re.search(r'#\s*include\s*[<"]aComm', text), path
            assert not re.search(r'#\s*include\s*[<"](?:aDrv|aDev)',
                                 text), path
        if path.is_relative_to(root / "app/devices/rs485"):
            assert not re.search(r'#\s*include\s*[<"](?:aModbus|modbus|'
                                 r'IDU_modbus|FAN_modbus|protocol)',
                                 text), path
        if path.name in ("FAN_modbus_master.c", "IDU_modbus_slave.c"):
            assert not re.search(r'\b(?:aOSCreateTask|aModbus(?:Client|Server)Create|'
                                 r'rs485PortOpen)\s*\(', text), path
            assert not re.search(r'\.(?:task|stack_bytes|priority)\s*=',
                                 text), path
        if path.name == "protocol_tables.c":
            assert not re.search(
                r'\b(?:aDataBase_instance|aMemory_layout|kvdb_sec_info|'
                r'aDataBaseKvIndexStorage_t)\b', text), path
        if path.is_relative_to(root / "app/protocol"):
            assert not re.search(r'#\s*include\s*[<"](?:aDrv|aDev)',
                                 text), path
        if folder == "platform/aLib":
            assert not re.search(
                r'#\s*include\s*[<"](?:aOS|aDrv|aDev|aBus|aMemory|'
                r'aDataBase|aShell|aLog|aModbus|aComm)', text), path
        else:
            # 编译器原子类型、内存序及操作统一通过 aLib 适配。
            assert not re.search(r'#\s*include\s*<stdatomic\.h>|\b_Atomic\b|'
                                 r'\batomic_\w+\b|\bmemory_order_\w+\b|'
                                 r'\bATOMIC_[A-Z_]+\b', text), path

os_backend = root / "platform/aOS/backend/freertos/aOS_freertos.c"
assert not re.search(r'\b(?:atomic_\w+|memory_order_\w+)\b|<stdatomic\.h>',
                     os_backend.read_text()), os_backend

# 业务公共头不需要厂商头、OS 内核头或实例私有布局。
includes = ["platform/aLib/include", "platform/aOS/public",
            "device/aDev_LED",
            "platform/aDrv/include", "device/aDev_usart/include",
            "device/aDev_Flash25q", "func/aBus/include", "func/aMemory",
            "func/aDataBase", "func/aShell/include", "func/aLog",
            "func/aModbus/include", "func/aComm/include",
            "app/protocol/inc"]
headers = ("aDataLayout.h", "aComm_endpoint.h",
           "aBus_endpoint.h", "aModbus_endpoint.h", "aLayout.h", "aCrc.h", "aAtomic.h", "aOS.h", "aDrv.h", "aDev_usart.h", "aDev_flash25q.h",
           "aDrv_dma.h", "aDrv_dma_memory.h",
           "aDev_led.h", "aDev_led_instance.h",
           "aBus.h", "aBus_event.h", "aModbus_client.h",
           "aModbus_server.h", "aMemory.h", "aDataBase.h", "aShell.h", "aLog.h",
           "aComm.h", "aComm_instance.h", "aComm_mapping.h",
           "aComm_mapping_instance.h", "aModbus.h",
           "aComm_channel.h", "aComm_channel_instance.h",
           "protocol.h", "IDU_sig_table.h", "FAN_sig_table.h")
command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-fsyntax-only", *["-I" + path for path in includes],
           "-x", "c", "-"]
for header in headers:
    subprocess.run(command, input=f'#include "{header}"\n',
                   text=True, cwd=root, check=True)
# 通信配置只使用端点契约；总线实例和事件桥接由 protocol.c 装配。
for source in ("FAN_modbus_master.c", "IDU_modbus_slave.c"):
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
        "-Iplatform/aLib/include", "-Iplatform/aOS/public",
        "-Ifunc/aComm/include", "-Ifunc/aModbus/include",
        "-Iapp", "-Iapp/protocol/inc", f"app/protocol/{source}",
    ], cwd=root, check=True)
# 设备数据头不依赖 aBus/aModbus；protocol.h 显式借用 aBus。
subprocess.run([
    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
    "-Iplatform/aLib/include", "-Iapp/protocol/inc",
    "-DAPP_MODBUS_ENABLE=1", "-DABUS_ENABLE=1", "-x", "c", "-",
], input=('#include "IDU_sig_table.h"\n'
          '#include "FAN_sig_table.h"\n'
          '#if defined(ABUS_SIG) || defined(ABUS_PARAMS) || '
          'defined(ABUS_PARAM) || defined(ABUS_PARAM_REF)\n'
          '#error "SIG expansion macros leaked into application headers"\n'
          '#endif\n'),
    text=True, cwd=root, check=True)
# 只读定义与数据库不消费 aBus 的分配/锁配置。
subprocess.run(command + ["-DABUS_STATIC_ENABLE=0", "-DABUS_DYNAMIC_ENABLE=0",
                          "-DABUS_LOCK_MODE=99"],
               input='#include "aBus_table.h"\n#include "aDataBase.h"\n',
               text=True, cwd=root, check=True)
print("Architecture/public header/metadata isolation checks passed")
