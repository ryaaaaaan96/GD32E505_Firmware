/* 验证真实 Modbus 规则；通用协议回调的替换测试位于 tests/comm。 */
#include "aModbus_mapping_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void testAdvanceTime(uint32_t milliseconds);
void aOSDelayMs(uint32_t milliseconds) { testAdvanceTime(milliseconds); }

static void raw_slice(void)
{
    aCommDataInfo_t sig = {.type = ALIB_DATA_RAW, .size = 3U};
    const aCommTarget_t target = {7U, 0U, ACOMM_DATA_WHOLE};
    aModbusMapValue_t info;
    uint8_t bytes[] = {0x12U, 0x34U, 0x56U};
    uint16_t wire[2] = {0};
    assert(aModbusMapDescribe(&sig, &target, AMODBUS_AREA_HOLDING_REGISTERS,
        AMODBUS_WORD_HIGH_FIRST, &info) == A_STATUS_OK);
    assert(info.quantity == 2U);
    assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
        AMODBUS_WORD_HIGH_FIRST, 0U, 2U, bytes, wire, A_FALSE) == A_STATUS_OK);
    assert(wire[0] == 0x1234U && wire[1] == 0x5600U);
    assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
        AMODBUS_WORD_HIGH_FIRST, 1U, 1U, bytes, wire, A_FALSE) == A_STATUS_OK);
    assert(wire[0] == 0x5600U);
    wire[0] = 0xABCDU;
    wire[1] = 0xEF00U;
    assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
        AMODBUS_WORD_HIGH_FIRST, 0U, 2U, bytes, wire, A_TRUE) == A_STATUS_OK);
    assert(bytes[0] == 0xABU && bytes[1] == 0xCDU && bytes[2] == 0xEFU);
    wire[1] |= 1U;
    assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
        AMODBUS_WORD_HIGH_FIRST, 0U, 2U, bytes, wire, A_TRUE) ==
        A_STATUS_INVALID_PARAM);
    assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
        AMODBUS_WORD_HIGH_FIRST, 1U, 1U, bytes, wire, A_TRUE) ==
        A_STATUS_NOT_FOUND);
}
static void scalar_slice(void)
{
    const aDataRange_t range = {.min.u32 = 0U, .max.u32 = 0x12345678U};
    aCommDataInfo_t sig = {
        .type = ALIB_DATA_U32, .size = 4U, .range = &range
    };
    const aCommTarget_t target = {7U, 1U, ACOMM_DATA_WHOLE};
    aModbusMapValue_t info;
    uint32_t value = 0x12345678U;
    uint16_t wire[2] = {0};
    for (unsigned order = 0U; order <= 1U; order++) {
        assert(aModbusMapDescribe(&sig, &target, AMODBUS_AREA_HOLDING_REGISTERS,
            (uint8_t)order, &info) == A_STATUS_OK);
        assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
            (uint8_t)order, 0U, 2U, &value, wire, A_FALSE) == A_STATUS_OK);
        uint16_t second = wire[1];
        assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
            (uint8_t)order, 1U, 1U, &value, wire, A_FALSE) == A_STATUS_OK);
        assert(wire[0] == second);
    }
    wire[0] = 0x1234U;
    wire[1] = 0x5679U;
    assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
        AMODBUS_WORD_HIGH_FIRST, 0U, 2U, &value, wire, A_TRUE) ==
        A_STATUS_INVALID_PARAM);
    wire[1]--;
    assert(aModbusMapCopy(&info, AMODBUS_AREA_HOLDING_REGISTERS,
        AMODBUS_WORD_HIGH_FIRST, 0U, 2U, &value, wire, A_TRUE) == A_STATUS_OK);
    assert(value == range.max.u32);
}
int main(void)
{
    raw_slice();
    scalar_slice();
    assert(aModbusCommEndpoint(NULL) == NULL);
    assert(aModbusMapReady(NULL) == A_STATUS_NOT_READY);
    puts("Modbus RAW padding/slices/scalar orders/ranges passed");
    return 0;
}
