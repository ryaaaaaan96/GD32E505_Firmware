#!/usr/bin/env python3
"""External product/library build and rejected OS time/TLS configurations."""
from pathlib import Path
import os
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

with tempfile.TemporaryDirectory(prefix="aclass-product-") as temporary:
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
static void task(void *argument) { (void)argument; }
int main(void) {
    if (aDrvInit() != A_STATUS_OK || aOSInit() != A_STATUS_OK) return 1;
    aOSTaskConfig_t config = AOS_TASK_CONFIG_DEFAULT;
    config.function = task;
    config.name = "return";
    config.stack_bytes = 512;
    if (aOSCreateTask(&config, 0) != A_STATUS_OK) return 1;
    aOSRun();
}
''')
    base = ["cmake", "-S", str(product), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug", f"-DARM_GCC_ROOT={toolchain}"]
    build = product / "build"
    run(base + ["-B", str(build)])
    run(["cmake", "--build", str(build), "--parallel", "4"])
    print("External product/library build passed")
    for name, setting, message in (
        ("tick16", "set(FREERTOS_USE_16_BIT_TICKS 1)", "32-bit ticks at 1000 Hz"),
        ("tick100", "set(FREERTOS_TICK_RATE_HZ 100)", "32-bit ticks at 1000 Hz"),
        ("tls", "set(FREERTOS_NUM_THREAD_LOCAL_STORAGE_POINTERS 1)", "two FreeRTOS TLS slots"),
    ):
        config = product / f"{name}.cmake"
        config.write_text(f'include("{root}/config/freeRTOS_config.cmake")\n{setting}\n')
        run(base + ["-B", str(product / name), f"-DACLASS_FREERTOS_CONFIG_FILE={config}"], message)
        print(f"{name}: invalid configuration rejected")
