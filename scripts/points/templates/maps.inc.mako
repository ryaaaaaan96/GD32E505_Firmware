/* 自动生成：业务回调、端口和设备参数仍由宿主 .c 提供。 */
#include "../inc/protocol_generated.h"
% for device in devices:
#include "../inc/${device["name"]}_sig_table.h"
% endfor

% if protocol.get("ranges"):
static const aModbusMappingAddressRange_t ranges[] = {
% for block in protocol["ranges"]:
    {
        .area = AMODBUS_AREA_${block["area"].upper()},
        .address = ${block["address"]}U, .quantity = ${block["quantity"]}U,
        .flags = ${" | ".join("ACOMM_MAP_FLAG_" + f for f in block["access"])},
% for operation in ("read", "write"):
% if operation in block:
        .${operation} = ${block[operation]},
% endif
% endfor
% if "context" in block:
        .context = &${block["context"]},
% endif
    },
% endfor
};
% endif

static const aModbus${"Client" if protocol["role"] == "client" else "Server"}Map_t ${"client_maps" if protocol["role"] == "client" else "maps"}[] = {
% for mapping in protocol["maps"]:
<% sig = mapping["signal"]; prefix = sig["device"]["name"] %>\
    {
        .comm = {
            .flags = ${" | ".join("ACOMM_MAP_FLAG_" + f for f in mapping["access"]) + (" | ACOMM_MAP_FLAG_BREAK_BEFORE" if mapping.get("break_before") else "")},
% if protocol["role"] == "client":
            .poll_time_ms = ${"ACOMM_POLL_ONCE" if mapping["poll_time_ms"] == "once" else str(mapping["poll_time_ms"]) + "U"},
% endif
            .deviceID = ${prefix}_SIG_DEVICE_ID,
            .sigIndex = ${prefix}_SIG_${sig["name"]},
            .paramIndex = ${mapping["param_symbol"]},
            .subscribe_events = ${c_events(mapping.get("subscribe", []))},
            .publish_events = ${c_events(mapping.get("publish", []))},
        },
        .area = AMODBUS_AREA_${mapping["area"].upper()},
        .address = ${mapping["address"]}U,
        .quantity = ${mapping["quantity"]}U,
        .word_order = AMODBUS_WORD_${mapping["word_order"].upper()},
    },
% endfor
};
