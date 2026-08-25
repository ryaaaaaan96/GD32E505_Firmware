#ifndef A_MODBUS_H
#define A_MODBUS_H

#include <stddef.h>
#include <stdint.h>

uint16_t aModbusCrc16(const uint8_t *data, size_t length);
int aModbusRtuFrameValid(const uint8_t *frame, size_t length);

#endif
