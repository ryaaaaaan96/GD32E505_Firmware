#include "IDU_sig_table.h"

static const aBusSig_t data_sigs[IDU_SIG_COUNT] = {
    [IDU_SIG_COUNTER] = {
        .sigKey = IDU_SIG_COUNTER_KEY,
        .type = ALIB_DATA_U32,
        .flags = ABUS_SIG_FLAG_LOCK,
        .size = sizeof(uint32_t)
    }
};

/* 只填充表描述；protocol 统一挂载全部设备点表。 */
aStatus_t IDUSigTableInit(aBusTable_t *table)
{
    const aBusTable_t definition = {
        .sigs = data_sigs,
        .sig_count = IDU_SIG_COUNT,
        .deviceID = IDU_SIG_DEVICE_ID
    };

    if (table == NULL) return A_STATUS_INVALID_PARAM;
    *table = definition;
    return A_STATUS_OK;
}
