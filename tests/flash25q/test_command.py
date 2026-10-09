#!/usr/bin/env python3
"""验证手动测试命令的擦写边界和校验失败处理。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / "aShell.h").write_text('''#include <stdio.h>
#define ASHELL_PRINT(...) printf(__VA_ARGS__)
#define ASHELL_REPLY(...) ASHELL_PRINT(__VA_ARGS__)
#define ASHELL_CMD_EXPORT(n, f, h) \\
    int test_command(int argc, char **argv) { return f(argc, argv); }
''')
    (path / "test.c").write_text(r'''
#include "flash_device.h"
#include <assert.h>
#include <string.h>
static unsigned char memory[8192];
static unsigned erases, writes;
static int corrupt;
int test_command(int argc, char **argv);
aStatus_t appSystemFlashGetInfo(aDevFlash25qInfo_t *i) {
 *i=(aDevFlash25qInfo_t){.capacity=8192,.erase_size=4096};
 return A_STATUS_OK;
}
void aDevFlash25qReadRequestStructInit(aDevFlash25qReadRequest_t *r)
 { memset(r,0,sizeof(*r)); }
void aDevFlash25qWriteRequestStructInit(aDevFlash25qWriteRequest_t *r)
 { memset(r,0,sizeof(*r)); }
void aDevFlash25qEraseRequestStructInit(aDevFlash25qEraseRequest_t *r)
 { memset(r,0,sizeof(*r)); }
aStatus_t appSystemFlashRead(const aDevFlash25qReadRequest_t *r) {
 assert(r->address+r->size<=8192);
 memcpy(r->data,memory+r->address,r->size);
 if(corrupt) ((unsigned char *)r->data)[0]^=1;
 return A_STATUS_OK;
}
aStatus_t appSystemFlashWrite(const aDevFlash25qWriteRequest_t *r) {
 assert(r->address>=4096 && r->address+r->size<=8192);
 memcpy(memory+r->address,r->data,r->size); ++writes;
 return A_STATUS_OK;
}
aStatus_t appSystemFlashErase(const aDevFlash25qEraseRequest_t *r) {
 assert(r->address==4096 && r->size==4096);
 memset(memory+r->address,255,r->size); ++erases;
 return A_STATUS_OK;
}
int main(void) {
 char *cmd[]={"flash","test","0x1000"};
 const char *invalid[]={"1","8192","-1","+4096","0x",
     "4294967296","4096x"," 4096","0xFFFFFFFF"};
 memset(memory,0x5A,sizeof(memory));
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
  cmd[2]=(char *)invalid[i]; assert(test_command(3,cmd)==-1);
 }
 assert(!erases && !writes);
 cmd[2]="0x1000"; assert(test_command(3,cmd)==0);
 assert(erases==1 && writes==13);
 for(unsigned i=0;i<4096;++i) assert(memory[i]==0x5A);
 corrupt=1; assert(test_command(3,cmd)==-1);
 assert(erases==2 && writes==13);
 cmd[1]="info"; assert(test_command(2,cmd)==0);
 assert(test_command(1,cmd)==-1);
}
''')
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=undefined", "-fno-sanitize-recover=all"]
    for directory in (path, root / "app/devices/system",
                      root / "device/aDev_Flash25q",
                      root / "platform/aDrv/include",
                      root / "platform/aLib/include"):
        command += ["-I", str(directory)]
    exe = path / "test"
    subprocess.run(command + [str(path / "test.c"),
                   str(root / "app/task/system/flash_test.c"),
                   "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
