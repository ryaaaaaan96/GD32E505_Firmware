#!/usr/bin/env python3
"""验证真实 DMA 驱动和 USART DMA 适配，包含 ISR/查询交错。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="aclass-dma-") as tmp:
    binary = str(Path(tmp) / "dma")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-fno-pie", "-no-pie",
        "-Wl,--defsym=__adrv_dma_ram_start=0x20000000",
        "-Wl,--defsym=__adrv_dma_ram_end=0x20020000",
        "-Wl,--defsym=__adrv_dma_flash_start=0x08000000",
        "-Wl,--defsym=__adrv_dma_flash_end=0x08080000",
        "-DADRV_USART_DMA_ENABLE=1",
        "-Itests/dma/mocks", "-Iplatform/aDrv/include",
        "-Iplatform/aDrv/src", "-Iplatform/aLib/include",
        "tests/dma/test_dma.c", "platform/aDrv/src/dma/aDrv_dma.c",
        "platform/aDrv/src/dma/aDrv_dma_memory.c",
        "platform/aDrv/src/usart/aDrv_usart_dma.c", "-o", binary,
    ], cwd=root, check=True)
    subprocess.run([binary], check=True)

    # 缓存接口只有平台明确声明一致性时才允许为空实现。
    cache_source = Path(tmp) / "cache.c"
    cache_source.write_text('''#include "aDrv_dma_memory.h"
static unsigned char buffer[8];
int main(void)
{
    aDrvDmaTxPrepare(buffer, sizeof(buffer));
    aDrvDmaRxPrepare(buffer, sizeof(buffer));
    aDrvDmaRxComplete(buffer, sizeof(buffer));
    return 0;
}
''')
    cache_binary = str(Path(tmp) / "cache")
    cache_command = [
        "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-Iplatform/aDrv/include", "-Iplatform/aLib/include",
        str(cache_source), "-o", cache_binary,
    ]
    subprocess.run(cache_command + ["-DADRV_DMA_CACHE_COHERENT=1"],
                   cwd=root, check=True)
    subprocess.run([cache_binary], check=True)
    result = subprocess.run(cache_command, cwd=root,
                            capture_output=True, text=True)
    assert result.returncode != 0 and "aDrvDmaRxComplete" in result.stderr

    # 使用真实链接脚本验证 DMA 区包含在启动清零范围，不执行此 MCU 地址 ELF。
    layout_source = Path(tmp) / "layout.c"
    layout_source.write_text('''#include "aDrv_dma_memory.h"
ADRV_DMA_BUFFER unsigned char test_dma_buffer[256];
void Reset_Handler(void) { for (;;) {} }
''')
    layout_binary = str(Path(tmp) / "layout.elf")
    subprocess.run([
        "cc", "-std=c11", "-ffreestanding", "-fno-pie", "-no-pie",
        "-nostdlib", "-fno-asynchronous-unwind-tables",
        "-DADRV_DMA_BUFFER_ALIGNMENT=4",
        "-Iplatform/aDrv/include", "-Iplatform/aLib/include",
        str(layout_source), "-Wl,--build-id=none", "-Wl,-T,GD32E505_flash.ld",
        "-o", layout_binary,
    ], cwd=root, check=True)
    symbols = {}
    for line in subprocess.check_output(["nm", layout_binary], text=True).splitlines():
        address, kind, name = line.split()
        symbols[name] = int(address, 16)
    address = symbols["test_dma_buffer"]
    assert address % 4 == 0
    assert symbols["_sbss"] <= address < address + 256 <= symbols["_ebss"]
    assert symbols["__adrv_dma_ram_start"] <= address
    assert address + 256 <= symbols["__adrv_dma_ram_end"]
    print("DMA cache backend selection and buffer startup layout passed")
