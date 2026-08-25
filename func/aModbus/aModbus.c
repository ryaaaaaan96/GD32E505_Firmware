#include "aModbus.h"

uint16_t aModbusCrc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    if (data == NULL) return 0U;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

int aModbusRtuFrameValid(const uint8_t *frame, size_t length)
{
    if ((frame == NULL) || (length < 4U)) return 0;
    const uint16_t expected = (uint16_t)frame[length - 2U] | ((uint16_t)frame[length - 1U] << 8U);
    return aModbusCrc16(frame, length - 2U) == expected ? 1 : 0;
}
