#!/usr/bin/env python3
"""Run USART host tests without hardware: python3 tests/usart/run.py."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
# RX 公共接口在轮询、仅中断、仅 DMA 的裁剪组合中也必须可编译。
for name, interrupt, dma in (("polling", 0, 0), ("interrupt", 1, 0), ("dma", 0, 1)):
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
        "-DADEV_USART_STATIC_ENABLE=1",
        f"-DADEV_USART_INTERRUPT_ENABLE={interrupt}",
        "-DADEV_USART_DIRECT_ENABLE=0", "-DADEV_USART_ASYNC_ENABLE=0",
        "-DADEV_USART_RS485_ENABLE=0",
        f"-DADRV_USART_INTERRUPT_ENABLE={int(bool(interrupt or dma))}",
        f"-DADRV_USART_DMA_ENABLE={dma}",
        "-Itests/usart/mocks", "-Idevice/aDev_usart/include", "-Idevice/aDev_usart/src",
        "-Iplatform/aLib/include", "-Iplatform/aDrv/include",
        "device/aDev_usart/src/aDev_usart_rx.c",
    ], cwd=root, check=True)
    print("RX compile profile:", name, flush=True)

with tempfile.TemporaryDirectory(prefix="aclass-usart-") as directory:
    # 只生成真实平台构建，不使用 ARM 工具链；核对 OS 约束确实传入 aDrv。
    project = Path(directory)
    (project / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.23)
project(usart_priority_config LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BOARD_HXTAL_HZ 8000000)
set(MCU_CORE_CLOCK_HZ 200000000)
set(ACLASS_OS FreeRTOS)
set(AOS_WORKER_STACK_BYTES 1024)
set(AOS_WORKER_PRIORITY 2)
set(FREERTOS_PORT GCC/ARM_CM33_NTZ/non_secure)
set(ACLASS_FREERTOS_CONFIG_FILE "${{CMAKE_CURRENT_SOURCE_DIR}}/os.cmake")
set(ADRV_MODULE_USART_ENABLE ON)
add_library(aclass_project_options INTERFACE)
add_library(aclass_build_options INTERFACE)
add_subdirectory("{root}/platform" platform)
''')
    for minimum, maximum in ((5, 15), (7, 12)):
        (project / "os.cmake").write_text(
            f"set(FREERTOS_LIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY {minimum})\n"
            f"set(FREERTOS_LIBRARY_LOWEST_INTERRUPT_PRIORITY {maximum})\n")
        build = project / f"config-{minimum}-{maximum}"
        result = subprocess.run([
            "cmake", "-S", str(project), "-B", str(build), "-G", "Unix Makefiles",
        ], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        commands = json.loads((build / "compile_commands.json").read_text())
        command = next(entry["command"] for entry in commands
                       if entry["file"].endswith("/usart/aDrv_usart.c"))
        assert f"-DADRV_USART_INTERRUPT_PRIORITY_MIN={minimum}" in command
        assert f"-DADRV_USART_INTERRUPT_PRIORITY_MAX={maximum}" in command
        config = (build / "aOS_config/FreeRTOSConfig.h").read_text()
        assert f"#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY {minimum}\n" in config
        assert f"#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY {maximum}\n" in config
    print("USART/OS priority build configuration passed", flush=True)

    executable = str(Path(directory) / "test_rs485")
    command = [
        "cc", "-std=c11",
        "-DADEV_USART_STATIC_ENABLE=1", "-DADEV_USART_DYNAMIC_ENABLE=1", "-Wall", "-Wextra", "-Werror",
        *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
          if os.environ.get("SANITIZE") else []),
        "-DADRV_USART_INTERRUPT_ENABLE=1", "-DADRV_USART_DMA_ENABLE=1",
        *[f"-DADEV_USART_{feature}_ENABLE=1" for feature in (
            "INTERRUPT", "DIRECT", "ASYNC", "RS485",
        )],
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Itests/usart/mocks", "-Idevice/aDev_usart/include", "-Idevice/aDev_usart/src",
        "-Iplatform/aLib/include", "-Iplatform/aDrv/include",
        "tests/usart/test_rs485.c", "device/aDev_usart/src/aDev_usart.c",
        "device/aDev_usart/src/aDev_usart_rx.c",
        "device/aDev_usart/src/aDev_usart_tx.c",
        "device/aDev_usart/src/aDev_usart_rs485.c", "-o", executable,
    ]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], check=True)

    fifo = str(Path(directory) / "test_fifo")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Iplatform/aLib/include", "tests/usart/test_fifo.c",
                    "-o", fifo], cwd=root, check=True)
    subprocess.run([fifo], check=True)

    irq = str(Path(directory) / "test_irq")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
          if os.environ.get("SANITIZE") else []),
        "-DADRV_USART_INTERRUPT_ENABLE=1",
        "-Itests/usart/mocks", "-Iplatform/aLib/include", "-Iplatform/aDrv/include",
        "-Iplatform/aDrv/src/usart", "tests/usart/test_irq.c",
        "platform/aDrv/src/usart/aDrv_usart_irq.c", "-o", irq,
    ], cwd=root, check=True)
    subprocess.run([irq], check=True)
