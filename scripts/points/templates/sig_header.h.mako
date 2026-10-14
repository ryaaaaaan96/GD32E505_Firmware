/* 自动生成：修改 config/points.json，运行 scripts/generate_points.py。 */
#ifndef APP_${device["name"]}_SIG_TABLE_H
#define APP_${device["name"]}_SIG_TABLE_H

#include <stdint.h>
#include "aLayout.h"

#define ${device["name"]}_SIG_DEVICE_ID ${device["device_id"]}U

/* 顺序决定本次固件下标，Key 独立于顺序，发布后保持稳定。 */
typedef enum {
% for sig in device["signals"]:
    ${device["name"]}_SIG_${sig["name"]},
% endfor
    ${device["name"]}_SIG_COUNT
} ${device["name"]}SigIndex_t;

enum {
% for sig in device["signals"]:
    ${device["name"]}_SIG_${sig["name"]}_KEY = ${sig["key"]}U,
% endfor
};

% for definition in device["types"]:
% if definition["kind"] == "raw":
typedef uint8_t ${definition["name"]}[${definition["size"]}];
% else:
typedef struct {
% for field in definition["fields"]:
    ${field["definition"]["name"]} ${field["name"]};
% endfor
} ${definition["name"]};
ALIB_ASSERT_TYPE_SIZE(${definition["name"]}, ${definition["size"]}U);
% for field in definition["fields"]:
ALIB_ASSERT_MEMBER_OFFSET(${definition["name"]},
    ${field["name"]}, ${field["offset"]}U);
% endfor
% endif

% endfor
% for sig in device["signals"]:
% if sig["definition"]["kind"] == "struct":
enum {
% for field in sig["definition"]["fields"]:
    ${device["name"]}_${sig["name"]}_${field["symbol"]},
% endfor
    ${device["name"]}_${sig["name"]}_PARAM_COUNT
};
% endif
% endfor

#endif
