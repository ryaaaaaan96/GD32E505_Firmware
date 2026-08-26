/*
 * GNU assembler startup for the GD32E50x connectivity-line devices.
 *
 * The vector order and handler names are derived from GigaDevice's
 * startup_gd32e50x_cl.s in GD32E50x Firmware Library V1.7.0. The GNU reset
 * sequence was cross-checked against the earlier project implementation in
 * old/io-interface-board and adapted to the current linker/newlib contract.
 *
 * Copyright (c) 2025, GigaDevice Semiconductor Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

    .syntax unified
    .cpu cortex-m33
    .fpu fpv5-sp-d16
    .thumb

    .section .isr_vector,"a",%progbits
    .align 2
    .global g_pfnVectors
    .global __Vectors
    .global __Vectors_End
    .global __Vectors_Size
    .type g_pfnVectors, %object

g_pfnVectors:
__Vectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word DebugMon_Handler
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler

    .word WWDGT_IRQHandler
    .word LVD_IRQHandler
    .word TAMPER_IRQHandler
    .word RTC_IRQHandler
    .word FMC_IRQHandler
    .word RCU_CTC_IRQHandler
    .word EXTI0_IRQHandler
    .word EXTI1_IRQHandler
    .word EXTI2_IRQHandler
    .word EXTI3_IRQHandler
    .word EXTI4_IRQHandler
    .word DMA0_Channel0_IRQHandler
    .word DMA0_Channel1_IRQHandler
    .word DMA0_Channel2_IRQHandler
    .word DMA0_Channel3_IRQHandler
    .word DMA0_Channel4_IRQHandler
    .word DMA0_Channel5_IRQHandler
    .word DMA0_Channel6_IRQHandler
    .word ADC0_1_IRQHandler
    .word CAN0_TX_IRQHandler
    .word CAN0_RX0_IRQHandler
    .word CAN0_RX1_IRQHandler
    .word CAN0_EWMC_IRQHandler
    .word EXTI5_9_IRQHandler
    .word TIMER0_BRK_TIMER8_IRQHandler
    .word TIMER0_UP_TIMER9_IRQHandler
    .word TIMER0_TRG_CMT_TIMER10_IRQHandler
    .word TIMER0_Channel_IRQHandler
    .word TIMER1_IRQHandler
    .word TIMER2_IRQHandler
    .word TIMER3_IRQHandler
    .word I2C0_EV_IRQHandler
    .word I2C0_ER_IRQHandler
    .word I2C1_EV_IRQHandler
    .word I2C1_ER_IRQHandler
    .word SPI0_IRQHandler
    .word SPI1_I2S1ADD_IRQHandler
    .word USART0_IRQHandler
    .word USART1_IRQHandler
    .word USART2_IRQHandler
    .word EXTI10_15_IRQHandler
    .word RTC_Alarm_IRQHandler
    .word USBHS_WKUP_IRQHandler
    .word TIMER7_BRK_TIMER11_IRQHandler
    .word TIMER7_UP_TIMER12_IRQHandler
    .word TIMER7_TRG_CMT_TIMER13_IRQHandler
    .word TIMER7_Channel_IRQHandler
    .word 0
    .word EXMC_IRQHandler
    .word 0
    .word TIMER4_IRQHandler
    .word SPI2_I2S2ADD_IRQHandler
    .word UART3_IRQHandler
    .word UART4_IRQHandler
    .word TIMER5_DAC_IRQHandler
    .word TIMER6_IRQHandler
    .word DMA1_Channel0_IRQHandler
    .word DMA1_Channel1_IRQHandler
    .word DMA1_Channel2_IRQHandler
    .word DMA1_Channel3_IRQHandler
    .word DMA1_Channel4_IRQHandler
    .word ENET_IRQHandler
    .word ENET_WKUP_IRQHandler
    .word CAN1_TX_IRQHandler
    .word CAN1_RX0_IRQHandler
    .word CAN1_RX1_IRQHandler
    .word CAN1_EWMC_IRQHandler
    .word USBHS_IRQHandler
    .word 0
    .word SHRTIMER_IRQ2_IRQHandler
    .word SHRTIMER_IRQ3_IRQHandler
    .word SHRTIMER_IRQ4_IRQHandler
    .word SHRTIMER_IRQ5_IRQHandler
    .word SHRTIMER_IRQ6_IRQHandler
    .word USBHS_EP1_OUT_IRQHandler
    .word USBHS_EP1_IN_IRQHandler
    .word SHRTIMER_IRQ0_IRQHandler
    .word SHRTIMER_IRQ1_IRQHandler
    .word CAN2_TX_IRQHandler
    .word CAN2_RX0_IRQHandler
    .word CAN2_RX1_IRQHandler
    .word CAN2_EWMC_IRQHandler
    .word I2C2_EV_IRQHandler
    .word I2C2_ER_IRQHandler
    .word USART5_IRQHandler
    .word I2C2_WKUP_IRQHandler
    .word USART5_WKUP_IRQHandler
    .word TMU_IRQHandler

__Vectors_End:
    .equ __Vectors_Size, __Vectors_End - __Vectors
    .size g_pfnVectors, . - g_pfnVectors

    .section .text.Reset_Handler,"ax",%progbits
    .align 2
    .global Reset_Handler
    .weak Reset_Handler
    .type Reset_Handler, %function
    .thumb_func
Reset_Handler:
    /* Copy initialized data from its flash load address to SRAM. */
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata

.Lcopy_data:
    cmp r1, r2
    bcs .Lzero_bss_start
    ldr r3, [r0], #4
    str r3, [r1], #4
    b .Lcopy_data

.Lzero_bss_start:
    /* Clear the zero-initialized data section in SRAM. */
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0

.Lzero_bss:
    cmp r0, r1
    bcs .Lruntime_init
    str r2, [r0], #4
    b .Lzero_bss

.Lruntime_init:
    /* Initialize the MCU, C runtime, and then enter the application. */
    bl SystemInit
    bl __libc_init_array
    bl main

.Lmain_returned:
    b .Lmain_returned

    .size Reset_Handler, . - Reset_Handler

    .section .text.Default_Handler,"ax",%progbits
    .align 2
    .global Default_Handler
    .type Default_Handler, %function
    .thumb_func
Default_Handler:
    b Default_Handler
    .size Default_Handler, . - Default_Handler

    .macro weak_handler name
    .weak \name
    .thumb_set \name, Default_Handler
    .endm

    weak_handler NMI_Handler
    weak_handler HardFault_Handler
    weak_handler MemManage_Handler
    weak_handler BusFault_Handler
    weak_handler UsageFault_Handler
    weak_handler SVC_Handler
    weak_handler DebugMon_Handler
    weak_handler PendSV_Handler
    weak_handler SysTick_Handler

    weak_handler WWDGT_IRQHandler
    weak_handler LVD_IRQHandler
    weak_handler TAMPER_IRQHandler
    weak_handler RTC_IRQHandler
    weak_handler FMC_IRQHandler
    weak_handler RCU_CTC_IRQHandler
    weak_handler EXTI0_IRQHandler
    weak_handler EXTI1_IRQHandler
    weak_handler EXTI2_IRQHandler
    weak_handler EXTI3_IRQHandler
    weak_handler EXTI4_IRQHandler
    weak_handler DMA0_Channel0_IRQHandler
    weak_handler DMA0_Channel1_IRQHandler
    weak_handler DMA0_Channel2_IRQHandler
    weak_handler DMA0_Channel3_IRQHandler
    weak_handler DMA0_Channel4_IRQHandler
    weak_handler DMA0_Channel5_IRQHandler
    weak_handler DMA0_Channel6_IRQHandler
    weak_handler ADC0_1_IRQHandler
    weak_handler CAN0_TX_IRQHandler
    weak_handler CAN0_RX0_IRQHandler
    weak_handler CAN0_RX1_IRQHandler
    weak_handler CAN0_EWMC_IRQHandler
    weak_handler EXTI5_9_IRQHandler
    weak_handler TIMER0_BRK_TIMER8_IRQHandler
    weak_handler TIMER0_UP_TIMER9_IRQHandler
    weak_handler TIMER0_TRG_CMT_TIMER10_IRQHandler
    weak_handler TIMER0_Channel_IRQHandler
    weak_handler TIMER1_IRQHandler
    weak_handler TIMER2_IRQHandler
    weak_handler TIMER3_IRQHandler
    weak_handler I2C0_EV_IRQHandler
    weak_handler I2C0_ER_IRQHandler
    weak_handler I2C1_EV_IRQHandler
    weak_handler I2C1_ER_IRQHandler
    weak_handler SPI0_IRQHandler
    weak_handler SPI1_I2S1ADD_IRQHandler
    weak_handler USART0_IRQHandler
    weak_handler USART1_IRQHandler
    weak_handler USART2_IRQHandler
    weak_handler EXTI10_15_IRQHandler
    weak_handler RTC_Alarm_IRQHandler
    weak_handler USBHS_WKUP_IRQHandler
    weak_handler TIMER7_BRK_TIMER11_IRQHandler
    weak_handler TIMER7_UP_TIMER12_IRQHandler
    weak_handler TIMER7_TRG_CMT_TIMER13_IRQHandler
    weak_handler TIMER7_Channel_IRQHandler
    weak_handler EXMC_IRQHandler
    weak_handler TIMER4_IRQHandler
    weak_handler SPI2_I2S2ADD_IRQHandler
    weak_handler UART3_IRQHandler
    weak_handler UART4_IRQHandler
    weak_handler TIMER5_DAC_IRQHandler
    weak_handler TIMER6_IRQHandler
    weak_handler DMA1_Channel0_IRQHandler
    weak_handler DMA1_Channel1_IRQHandler
    weak_handler DMA1_Channel2_IRQHandler
    weak_handler DMA1_Channel3_IRQHandler
    weak_handler DMA1_Channel4_IRQHandler
    weak_handler ENET_IRQHandler
    weak_handler ENET_WKUP_IRQHandler
    weak_handler CAN1_TX_IRQHandler
    weak_handler CAN1_RX0_IRQHandler
    weak_handler CAN1_RX1_IRQHandler
    weak_handler CAN1_EWMC_IRQHandler
    weak_handler USBHS_IRQHandler
    weak_handler SHRTIMER_IRQ2_IRQHandler
    weak_handler SHRTIMER_IRQ3_IRQHandler
    weak_handler SHRTIMER_IRQ4_IRQHandler
    weak_handler SHRTIMER_IRQ5_IRQHandler
    weak_handler SHRTIMER_IRQ6_IRQHandler
    weak_handler USBHS_EP1_OUT_IRQHandler
    weak_handler USBHS_EP1_IN_IRQHandler
    weak_handler SHRTIMER_IRQ0_IRQHandler
    weak_handler SHRTIMER_IRQ1_IRQHandler
    weak_handler CAN2_TX_IRQHandler
    weak_handler CAN2_RX0_IRQHandler
    weak_handler CAN2_RX1_IRQHandler
    weak_handler CAN2_EWMC_IRQHandler
    weak_handler I2C2_EV_IRQHandler
    weak_handler I2C2_ER_IRQHandler
    weak_handler USART5_IRQHandler
    weak_handler I2C2_WKUP_IRQHandler
    weak_handler USART5_WKUP_IRQHandler
    weak_handler TMU_IRQHandler

    .section .note.GNU-stack,"",%progbits
