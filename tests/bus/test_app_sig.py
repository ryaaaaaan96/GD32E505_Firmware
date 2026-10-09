#!/usr/bin/env python3
"""Exercise the real SIG command, registry and task with host OS stubs."""
from pathlib import Path
import tempfile, subprocess
root=Path(__file__).resolve().parents[2]
import os
os.chdir(root)
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    (p/'aShell.h').write_text('''#include <stdio.h>
#define ASHELL_PRINT(...) printf(__VA_ARGS__)
#define ASHELL_REPLY(...) ASHELL_PRINT(__VA_ARGS__)
#define ASHELL_CMD_EXPORT(name, fn, desc) \\
int test_command(int argc, char **argv) { return fn(argc, argv); }
''')
    (p/'test.c').write_text('''#include "sig_data.h"
#include "aOS.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
void *aOSAlloc(size_t n) { return malloc(n); }
void aOSFree(void *p) { free(p); }
aStatus_t aOSMutexCreate(aOSMutex_t *m) {
 pthread_mutex_t *p = malloc(sizeof(*p));
 assert(p); assert(pthread_mutex_init(p, NULL) == 0); *m = p;
 return A_STATUS_OK;
}
void aOSMutexDestroy(aOSMutex_t *m) {
 assert(pthread_mutex_destroy(*m) == 0); free(*m); *m = NULL;
}
aStatus_t aOSMutexLock(aOSMutex_t m, aTimeout_t t) {
 (void)t; assert(pthread_mutex_lock(m) == 0); return A_STATUS_OK;
}
aStatus_t aOSMutexUnlock(aOSMutex_t m) {
 assert(pthread_mutex_unlock(m) == 0); return A_STATUS_OK;
}
#include "sig_task.h"
#include <setjmp.h>
static aOSTaskFunction_t task;
static jmp_buf done;
static unsigned delays;
aStatus_t aOSCreateTask(const aOSTaskConfig_t *c, aOSTaskHandle_t *h) {
 (void)h; task=c->function; return A_STATUS_OK;
}
void aOSDelayMs(uint32_t ms) {
 assert(ms==1000); if (++delays==2) longjmp(done,1);
}
int test_command(int argc, char **argv);
int main(void) {
 uint32_t value;
 aBusGetIndexRequest_t get;
 aBusSetIndexRequest_t set;
 char *write[]={"sig","set","1","1","123"};
 char *read[]={"sig","get","1","1"};
 char *bad[]={"sig","set","1","1","4294967296"};
 aBusGetIndexRequestStructInit(&get);
 get.deviceID=APP_SIG_DEVICE_ID; get.sigIndex=APP_BUS_COUNTER; get.dst=&value; get.size=sizeof(value);
 assert(sigDataGet(&get)==A_STATUS_NOT_READY);
 assert(sigDataInit()==A_STATUS_OK);
 assert(sigDataGet(&get)==A_STATUS_OK && value==0);
 assert(test_command(5,write)==0);
 assert(appSigTaskInit()==A_STATUS_OK);
 if(setjmp(done)==0) task(NULL);
 assert(sigDataGet(&get)==A_STATUS_OK && value==124);
 assert(test_command(5,bad)==-1);
 bad[4]="-1"; assert(test_command(5,bad)==-1);
 bad[4]="1x"; assert(test_command(5,bad)==-1);
 bad[3]="999"; bad[4]="1"; assert(test_command(5,bad)==-1);
 assert(sigDataGet(&get)==A_STATUS_OK && value==124);
 get.size=1; assert(sigDataGet(&get)==A_STATUS_INVALID_PARAM);
 get.size=sizeof(value); get.sigIndex=999;
 assert(sigDataGet(&get)==A_STATUS_NOT_FOUND);
 assert(sigDataSet(NULL)==A_STATUS_INVALID_PARAM);
 aBusSetIndexRequestStructInit(&set);
 set.deviceID=APP_SIG_DEVICE_ID; set.sigIndex=APP_BUS_COUNTER; set.src=&value; set.size=sizeof(value);
 value=UINT32_MAX; assert(sigDataSet(&set)==A_STATUS_OK);
 delays=0; if(setjmp(done)==0) task(NULL);
 get.deviceID=APP_SIG_DEVICE_ID; get.sigIndex=APP_BUS_COUNTER;
 assert(sigDataGet(&get)==A_STATUS_OK && value==0);
 assert(test_command(4,read)==0);
 read[3]="0"; assert(test_command(4,read)==0);
 {
  appBusMotor_t motor;
  char *field_write[]={"sig","set","1","0","0","6000"};
  char *field_read[]={"sig","get","1","0","0"};
  get.sigIndex=APP_BUS_MOTOR; get.dst=&motor; get.size=sizeof(motor);
  assert(test_command(6,field_write)==0);
  assert(sigDataGet(&get)==A_STATUS_OK);
  assert(motor.speed==6000 && motor.temperature==25);
  field_write[5]="6001"; assert(test_command(6,field_write)==-1);
  assert(sigDataGet(&get)==A_STATUS_OK && motor.speed==6000);
  field_write[4]="1"; field_write[5]="-2147483648";
  assert(test_command(6,field_write)==0);
  assert(sigDataGet(&get)==A_STATUS_OK);
  assert(motor.temperature==INT32_MIN && motor.speed==6000);
  assert(test_command(5,field_read)==0);
  field_read[4]="99"; assert(test_command(5,field_read)==-1);
  field_write[3]="1"; assert(test_command(6,field_write)==-1);
 }

 return 0;
}
''')
    for dynamic in (0,1):
        for mode in (0,1,2):
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',f'-DABUS_DYNAMIC_ENABLE={dynamic}','-DABUS_STATIC_ENABLE=1',f'-DABUS_LOCK_MODE={mode}',f'-I{tmp}','-Iapp/data/sig','-Iapp/task/sig','-Ifunc/aBus/include','-Iplatform/aOS/public','-Iplatform/aLib/include',str(p/'test.c'),'app/data/sig/sig_data.c','app/data/sig/sig_command.c','app/task/sig/sig_task.c','func/aBus/src/aBus.c','-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld','-o',str(p/'test')]
            subprocess.run(cmd,check=True)
            subprocess.run([str(p/'test')],check=True,stdout=subprocess.DEVNULL)
            print(f'PASS dynamic={dynamic} lock={mode}')
