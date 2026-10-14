#!/usr/bin/env python3
"""External product/library build and rejected OS time/TLS configurations."""
from pathlib import Path
import os
import re
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
toolchain = os.environ.get("ARM_GCC_ROOT", str(Path.home() / "Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3"))

def run(command, expected=None):
    result = subprocess.run(command, capture_output=True, text=True)
    output = result.stdout + result.stderr
    if expected is not None:
        assert result.returncode != 0 and expected in output, output
    elif result.returncode:
        raise RuntimeError(output)
    return output

with tempfile.TemporaryDirectory(prefix="aclass product ") as temporary:
    product = Path(temporary)
    (product / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.23)
include("{root}/cmake/Aclass.cmake")
aclass_select(NAME external VERSION 1.0 PLATFORM Embedded OS FreeRTOS
    MCU gd32e505 LINKER_SCRIPT GD32E505_flash.ld TOOLCHAIN GCC PRODUCT_DIR "{root}")
project(external LANGUAGES C ASM)
aclass_initialize()
aclass_add_libraries(CONFIG_FILE "{root}/config/aclass_config.cmake")
add_executable(external main.c)
target_link_libraries(external PRIVATE aCore aDrv aOS aclass_project_options)
generate_firmware_images(external)
''')
    (product / "main.c").write_text('''#include "aOS.h"
#include "aDrv.h"
static void task(void *argument) { (void)argument; aOSTaskExit(); }
int main(void) {
    if (aDrvInit() != A_STATUS_OK || aOSInit() != A_STATUS_OK) return 1;
    aOSTaskConfig_t config = AOS_TASK_CONFIG_DEFAULT;
    config.function = task;
    config.name = "explicit_exit";
    config.stack_bytes = 512;
    if (aOSCreateTask(&config, 0) != A_STATUS_OK) return 1;
    aOSRun();
}
''')
    base = ["cmake", "-S", str(product), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug", f"-DARM_GCC_ROOT={toolchain}"]
    build = product / "build"
    run(base + ["-B", str(build)])
    generated = (build / "aOS_config/FreeRTOSConfig.h").read_text()
    assert "#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 1\n" in generated
    command = ["cmake", "--build", str(build), "--parallel", "4"]
    output = run(command)
    report = (build / "bin/external.build.txt").read_text()
    assert "Firmware       : external v1.0" in report
    assert "OS heap reserve: 40960 bytes" in report
    assert "BIN SHA256:" in report
    # 报告须与 ld 区域统计一致，不能把 Flash 中的 data 分类算入 RAM。
    for region in ("FLASH", "RAM"):
        linked = re.search(rf"{region}:\s+(\d+) B", output)
        reported = re.search(rf"{region}: (\d+) /", report)
        assert linked and reported, output
        assert linked[1] == reported[1], report
    elf = build / "bin/external"
    # 异常记录必须避开启动清零和下载 LOAD 区；向量须落到强处理函数。
    tools = Path(toolchain) / "bin"
    symbols = run([str(tools / "arm-none-eabi-nm"), str(elf)])
    addresses = {name: int(address, 16) for address, kind, name in
                 re.findall(r"^([0-9a-fA-F]+) ([A-Za-z]) (\S+)$",
                            symbols, re.M)}
    assert re.search(r" T HardFault_Handler$", symbols, re.M), symbols
    sections = run([str(tools / "arm-none-eabi-objdump"), "-h", str(elf)])
    noinit = re.search(r"\s\.noinit\s+([0-9a-f]+)\s+([0-9a-f]+)[^\n]*\n([^\n]*)",
                       sections)
    assert noinit and "ALLOC" in noinit[3] and "LOAD" not in noinit[3], sections
    begin = int(noinit[2], 16)
    end = begin + int(noinit[1], 16)
    assert addresses["_ebss"] <= begin <= addresses["g_aDrvFaultRecord"] < end
    assert end <= addresses["_estack"]
    vectors = product / "vectors.bin"
    run([str(tools / "arm-none-eabi-objcopy"), "-O", "binary",
         "--only-section=.isr_vector", str(elf), str(vectors)])
    hard_fault = struct.unpack_from("<I", vectors.read_bytes(), 12)[0]
    assert hard_fault == addresses["HardFault_Handler"] | 1
    linked_at = elf.stat().st_mtime_ns
    # 无变更时仍打印摘要，但不触发链接。
    output = run(command)
    assert "Firmware build summary" in output
    assert "Linking C executable" not in output
    assert elf.stat().st_mtime_ns == linked_at
    print("External product/library build passed")
    for name, setting, message in (
        ("tick16", "set(FREERTOS_USE_16_BIT_TICKS 1)", "32-bit ticks at 1000 Hz"),
        ("tick100", "set(FREERTOS_TICK_RATE_HZ 100)", "32-bit ticks at 1000 Hz"),
        ("tls", "set(FREERTOS_NUM_THREAD_LOCAL_STORAGE_POINTERS 0)", "at least one FreeRTOS TLS slot"),
    ):
        config = product / f"{name}.cmake"
        config.write_text(f'include("{root}/config/freeRTOS_config.cmake")\n{setting}\n')
        run(base + ["-B", str(product / name), f"-DACLASS_FREERTOS_CONFIG_FILE={config}"], message)
        print(f"{name}: invalid configuration rejected")
