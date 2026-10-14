/* 自动生成：修改 config/points.json，运行 scripts/generate_points.py。 */
% for sig in device["signals"]:
<% definition = sig["definition"]; group = device["name"] + "_" + sig["name"] %>\
% if definition["kind"] == "struct":
ABUS_PARAMS(${group},
% for field in definition["fields"]:
<% rule = sig.get("field_ranges", {}).get(field["name"]) %>\
    ABUS_PARAM(${group}_${field["symbol"]},
        .offset = offsetof(${definition["name"]}, ${field["name"]}),
        .type = ALIB_DATA_${field["definition"]["kind"].upper()},
        .size = sizeof(${field["definition"]["name"]})${"," if rule else ""}
% if rule:
        .range = ${c_range(rule, field["definition"]["kind"])}
% endif
    )
% endfor
)
% endif
${"ABUS_BOUND_SIG" if sig.get("storage") == "bound" else "ABUS_SIG"}(${sig["name"]}, ${sig["key"]}U, ${definition["name"]},
    .type = ALIB_DATA_${definition["kind"].upper()},
    .flags = ${" | ".join("ABUS_SIG_FLAG_" + f for f in sig["flags"]) or "0U"},
% if "default" in sig:
    .default_data = &(const ${definition["name"]})${c_value(definition, sig["value"]) if definition["kind"] not in SCALARS else "{" + c_value(definition, sig["value"]) + "}"},
% endif
% if "range" in sig:
    .range = ${c_range(sig["range"], definition["kind"])},
% endif
% if definition["kind"] == "struct":
    ABUS_PARAM_REF(${group})
% endif
)

% endfor
