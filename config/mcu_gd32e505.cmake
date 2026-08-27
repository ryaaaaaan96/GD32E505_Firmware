# GD32E505 芯片配置。platform 层只消费这些变量，不保存产品参数。
set(MCU_DEVICE GD32E505VET7)
set(MCU_PUBLIC_DEFINITIONS
    GD32E50X
    GD32E50X_CL
)
set(MCU_STARTUP_VARIANT gd32e50x_cl)

# Arm GCC 与 FreeRTOS port 配置。
set(MCU_CPU cortex-m33)
set(MCU_FPU fpv5-sp-d16)
set(MCU_FLOAT_ABI hard)
set(MCU_CORE_CLOCK_HZ 180000000)
set(FREERTOS_PORT GCC/ARM_CM33_NTZ/non_secure)
