/**
 * @file aModbus.h
 * @brief Modbus RTU CRC 计算与帧校验工具。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 纯计算接口，无硬件、OS、内存分配或内部状态；可重入。
 * 本模块不实现主从站状态机、串口接收、帧间隔或重试。
 */

#ifndef A_MODBUS_H
#define A_MODBUS_H

#include "aLib.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 计算 Modbus CRC16，初值 0xFFFF，多项式 0xA001。
 * @param[in] data 输入字节数组；计算期间不得被并发修改。
 * @param[in] length 参与 CRC 计算的字节数。
 * @return CRC 数值；data 为 NULL 返回 0，非 NULL 且长度为 0 返回 0xFFFF。
 * @note 线上编码先发送 CRC 低字节，再发送高字节。
 */
uint16_t aModbusCrc16(const uint8_t *data, size_t length);

/**
 * @brief 校验最小帧长度及末尾两个字节的 CRC。
 * @param[in] frame 完整 RTU 帧，包含低字节在前的 CRC。
 * @param[in] length 完整帧字节数，至少 4。
 * @return CRC 匹配返回 A_TRUE；空指针、长度不足或 CRC 不符返回 A_FALSE。
 * @note 不校验从站地址、功能码语义或协议规定的最大帧长。
 */
aBool_t aModbusRtuFrameValid(const uint8_t *frame, size_t length);

#endif
