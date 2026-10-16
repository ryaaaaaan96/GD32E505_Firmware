"""JSON 只描述数据；本模块校验并解析引用，不执行输入中的表达式。"""
from __future__ import annotations

import copy
import json
from pathlib import Path

from jsonschema import Draft202012Validator

SCALARS = {
    "u8": ("uint8_t", 1, 0, 255),
    "u16": ("uint16_t", 2, 0, 65535),
    "u32": ("uint32_t", 4, 0, 4294967295),
    "s32": ("int32_t", 4, -2147483648, 2147483647),
}
AREAS = {"coils": 0, "discrete_inputs": 1,
         "holding_registers": 2, "input_registers": 3}
KEYWORDS = set("auto break case char const continue default do double else "
    "enum extern float for goto if inline int long register restrict return "
    "short signed sizeof static struct switch typedef union unsigned void "
    "volatile while _Alignas _Alignof _Atomic _Bool _Complex _Generic "
    "_Imaginary _Noreturn _Static_assert _Thread_local".split())


def require(condition, where, message):
    if not condition:
        raise ValueError(f"{where}: {message}")


def identifier(value, where):
    require(value not in KEYWORDS, where, f"C 保留字 {value}")


def unique(values, where):
    require(len(values) == len(set(values)), where, "重复标识")


def object_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "$", f"重复 JSON 属性 {key}")
        result[key] = value
    return result


def reject_number(value):
    raise ValueError(f"当前数据模型只支持整数，不接受 {value}")


def load(path):
    with Path(path).open(encoding="utf-8") as stream:
        data = json.load(stream, object_pairs_hook=object_pairs,
                         parse_float=reject_number,
                         parse_constant=reject_number)
    return validate(data)


def range_check(rule, kind, default, where):
    require(kind in SCALARS, where, "只有整数类型支持上下限")
    low, high = SCALARS[kind][2:]
    minimum, maximum = rule["min"], rule["max"]
    require(type(minimum) is int and type(maximum) is int and
            low <= minimum <= maximum <= high, where, "上下限超出类型或次序错误")
    require(minimum <= default <= maximum, where, "默认值不在范围内")


def default_value(definition, value, where):
    kind = definition["kind"]
    if kind in SCALARS:
        value = 0 if value is None else value
        require(type(value) is int and
                SCALARS[kind][2] <= value <= SCALARS[kind][3],
                where, "默认值超出整数类型")
    elif kind == "raw":
        value = [0] * definition["size"] if value is None else value
        require(isinstance(value, list) and len(value) == definition["size"]
                and all(type(v) is int and 0 <= v <= 255 for v in value),
                where, "RAW 默认值必须是 size 个字节")
    else:
        value = {} if value is None else value
        require(isinstance(value, dict), where, "STRUCT 默认值必须是对象")
        names = {f["name"] for f in definition["fields"]}
        require(not value.keys() - names, where, "默认值包含未知字段")
        value = {field["name"]: default_value(field["definition"],
                 value.get(field["name"]), where + "." + field["name"])
                 for field in definition["fields"]}
    return value


def validate(document):
    schema = json.loads(Path(__file__).with_name("schema.json").read_text())
    error = next(Draft202012Validator(schema).iter_errors(document), None)
    if error:
        path = "$" + "".join(f"[{part}]" for part in error.absolute_path)
        raise ValueError(f"{path}: {error.message}")
    data = copy.deepcopy(document)
    devices = data["devices"]
    unique([d["name"] for d in devices], "devices.name")
    unique([d["device_id"] for d in devices], "devices.device_id")
    exported = set()
    for device in devices:
        prefix = device["name"]
        definitions = {kind: {"kind": kind, "size": entry[1],
                       "name": entry[0], "alignment": entry[1]}
                       for kind, entry in SCALARS.items()}
        unique([t["name"] for t in device["types"]], prefix + ".types")
        # RAW 先解析；STRUCT 只含标量或 RAW，不支持结构体嵌套。
        device["types"].sort(key=lambda t: t["kind"] == "struct")
        for definition in device["types"]:
            name = definition["name"]
            identifier(name, prefix)
            require(name not in exported and name not in definitions and
                    name not in {s[0] for s in SCALARS.values()}, prefix,
                    f"重复或保留类型名 {name}")
            exported.add(name)
            if definition["kind"] == "raw":
                require("fields" not in definition, name, "RAW 没有 fields")
                definition["alignment"] = 1
            else:
                fields = definition.get("fields", [])
                require(fields, name, "STRUCT 必须描述全部字段")
                unique([f["name"] for f in fields], name + ".fields")
                unique([f["symbol"] for f in fields], name + ".symbols")
                offset, alignment = 0, 1
                for field in fields:
                    identifier(field["name"], name)
                    target = definitions.get(field["type"])
                    require(target and target["kind"] != "struct", name,
                            f"字段类型不可用: {field['type']}")
                    require(field["offset"] == offset and
                            offset % target["alignment"] == 0, name,
                            f"{field['name']} 偏移不连续或存在隐式填充")
                    field["definition"] = target
                    offset += target["size"]
                    alignment = max(alignment, target["alignment"])
                require(offset == definition["size"] and
                        offset % alignment == 0, name,
                        "size 不等于字段总长或存在尾部填充，请显式声明保留字段")
                definition["alignment"] = alignment
            definitions[name] = definition
        unique([s["name"] for s in device["signals"]], prefix + ".signals")
        unique([s["key"] for s in device["signals"]], prefix + ".keys")
        for index, sig in enumerate(device["signals"]):
            where = prefix + "." + sig["name"]
            require("data_version" not in sig or sig.get("persistent", False),
                    where, "data_version 仅用于持久化 SIG")
            definition = definitions.get(sig["type"])
            require(definition, where, "未知类型")
            sig["definition"] = definition
            sig["index"] = index
            sig["device"] = device
            sig["value"] = default_value(definition, sig.get("default"), where)
            if "range" in sig:
                range_check(sig["range"], definition["kind"], sig["value"],
                            where)
            rules = sig.get("field_ranges", {})
            if definition["kind"] == "struct":
                fields = {f["name"]: f for f in definition["fields"]}
                require(not rules.keys() - fields.keys(), where,
                        "field_ranges 包含未知字段")
                for name, rule in rules.items():
                    range_check(rule, fields[name]["definition"]["kind"],
                                sig["value"][name], where + "." + name)
            else:
                require(not rules and "DISJOINT" not in sig["flags"], where,
                        "field_ranges / DISJOINT 仅适用于 STRUCT")
        device["by_key"] = {sig["key"]: sig for sig in device["signals"]}
    by_name = {d["name"]: d for d in devices}
    unique([p["name"] for p in data["protocols"]], "protocols")
    for protocol in data["protocols"]:
        name, role = protocol["name"], protocol["role"]
        identifier(name, "protocols")
        require(name != "sig_tables", name, "协议名称与生成文件保留名冲突")
        intervals = []
        for mapping in protocol["maps"]:
            target = mapping["target"]
            device = by_name.get(target["device"])
            sig = device and device["by_key"].get(target["sig_key"])
            require(sig, name, f"目标不存在: {target}")
            definition = sig["definition"]
            if "param" in target:
                field = next((f for f in definition.get("fields", [])
                              if f["name"] == target["param"]), None)
                require(field, name, f"字段不存在: {target}")
                definition = field["definition"]
                mapping["param_symbol"] = (device["name"] + "_" +
                    sig["name"] + "_" + field["symbol"])
            else:
                mapping["param_symbol"] = "ACOMM_MAP_SIG_WHOLE"
            mapping["signal"] = sig
            kind, size, area = (definition["kind"], definition["size"],
                                mapping["area"])
            # 对齐当前 aModbus 单值上限；aBus 自身仍允许更大的数据。
            require(size <= 250, name, "映射目标超过 aModbus 的 250 字节上限")
            require(mapping["word_order"] == "high_first" or
                    kind == "struct" or kind in ("u32", "s32"), name,
                    "low_first 仅支持 32 位标量或 STRUCT")
            if area in ("coils", "discrete_inputs"):
                require(kind == "u8", name, "位区域仅映射一个 U8 标量")
                expected = 1
            else:
                if kind == "struct":
                    require(all(f["definition"]["size"] % 2 == 0
                                for f in definition["fields"]), name,
                            "整组寄存器映射的每个字段必须占完整寄存器")
                require(kind in SCALARS or size % 2 == 0, name,
                        "RAW/STRUCT 长度必须为偶数")
                expected = max(1, size // 2)
            require(mapping["quantity"] == expected, name,
                    f"quantity={mapping['quantity']} 与目标长度不符，应为 {expected}")
            if role == "client":
                require(not set(mapping.get("subscribe", [])) &
                        {"UPDATED", "WRITE_COMPLETE"}, name,
                        "主站仅支持请求订阅或 MARK_ONLY")
                require("poll_time_ms" in mapping, name, "主站显式填写 poll_time_ms")
                require(mapping["quantity"] <=
                        (2000 if area in ("coils", "discrete_inputs") else 125),
                        name, "单个采集项超出一帧容量")
                require(mapping["poll_time_ms"] == 0 or
                        "READ" in mapping["access"], name, "轮询项必须可读")
                if "WRITE" in mapping["access"]:
                    require(mapping["quantity"] <=
                            (1968 if area == "coils" else 123), name,
                            "单个下发项超出一帧容量")
                for event in mapping.get("subscribe", []):
                    if event in ("REQUEST_READ", "REQUEST_WRITE"):
                        require(event.removeprefix("REQUEST_") in
                                mapping["access"], name, "请求订阅缺少访问权限")
            else:
                require("poll_time_ms" not in mapping and
                        not mapping.get("break_before", False) and
                        not mapping.get("publish", []) and
                        not set(mapping.get("subscribe", [])) &
                        {"REQUEST_READ", "REQUEST_WRITE"}, name,
                        "从站不配置轮询、断帧、请求订阅或完成发布")
            require("MARK_ONLY" not in mapping.get("subscribe", []) or
                    len(mapping["subscribe"]) == 1, name,
                    "MARK_ONLY 不能与其他订阅事件组合")
            access_check(mapping, name)
            intervals.append((AREAS[area], mapping["address"],
                              mapping["address"] + mapping["quantity"]))
        ranges = protocol.get("ranges", [])
        require(not ranges or role == "server", name, "地址回调仅用于从站")
        unique([r["name"] for r in ranges], name + ".ranges")
        for block in ranges:
            access_check(block, name)
            for access in ("read", "write"):
                require((access.upper() in block["access"]) ==
                        (access in block), name, "回调与段权限必须对应")
                if access in block:
                    identifier(block[access], name)
            if "context" in block:
                identifier(block["context"], name)
            intervals.append((AREAS[block["area"]], block["address"],
                              block["address"] + block["quantity"]))
        # 主站允许重复采集同一远端地址；从站地址解析必须唯一。
        if role == "server":
            intervals.sort()
            for previous, current in zip(intervals, intervals[1:]):
                require(previous[0] != current[0] or previous[2] <= current[1],
                        name, "离散映射或回调地址段重叠")
            protocol["maps"].sort(
                key=lambda m: (AREAS[m["area"]], m["address"]))
            ranges.sort(key=lambda r: (AREAS[r["area"]], r["address"]))
    data["persistent"] = sorted(
        (s for d in devices for s in d["signals"]
         if s.get("persistent", False)),
        key=lambda s: (s["device"]["device_id"], s["key"]))
    # 枚举、宏和 typedef 共用生成命名空间，及早拒绝会产生无效 C 的命名。
    names = ["PROTOCOL_SIG_COUNT", "PROTOCOL_PARAM_COUNT",
             "PROTOCOL_PERSIST_SIG_COUNT"]
    for device in devices:
        prefix = device["name"]
        names += [prefix + "SigIndex_t", prefix + "_SIG_COUNT",
                  prefix + "_SIG_DEVICE_ID"]
        names += [t["name"] for t in device["types"]]
        for sig in device["signals"]:
            base = prefix + "_SIG_" + sig["name"]
            names += [base, base + "_KEY"]
            if sig["definition"]["kind"] == "struct":
                base = prefix + "_" + sig["name"] + "_"
                names += [base + "PARAM_COUNT"]
                names += [base + f["symbol"]
                          for f in sig["definition"]["fields"]]
    for protocol in data["protocols"]:
        prefix = protocol["name"].upper() + "_"
        names += [prefix + "MAP_COUNT", prefix + "RANGE_COUNT"]
        for block in protocol.get("ranges", []):
            names += [prefix + block["name"] + suffix
                      for suffix in ("_ADDRESS", "_QUANTITY")]
    unique(names, "生成 C 标识符")
    return data


def access_check(item, where):
    require(item["access"], where, "至少提供一种访问权限")
    require(item["address"] + item["quantity"] <= 65536, where, "地址溢出")
    require("WRITE" not in item["access"] or item["area"] in
            ("coils", "holding_registers"), where, "该 Modbus 区域只读")


def c_value(definition, value):
    kind = definition["kind"]
    if kind in SCALARS:
        if value == -2147483648:
            return "(-2147483647 - 1)"
        return str(value) + ("U" if kind != "s32" else "")
    if kind == "raw":
        return "{" + ", ".join(f"{v}U" for v in value) + "}"
    return "{" + ", ".join("." + f["name"] + " = " +
        c_value(f["definition"], value[f["name"]])
        for f in definition["fields"]) + "}"


def c_range(rule, kind):
    definition = {"kind": kind}
    return ("&(const aBusRange_t){.min." + kind + " = " +
            c_value(definition, rule["min"]) + ", .max." + kind + " = " +
            c_value(definition, rule["max"]) + "}")


def c_events(events):
    return " | ".join("ACOMM_" +
        ("SUBSCRIBE_" + e if e == "MARK_ONLY" else
         e if e.startswith("REQUEST_") else "EVENT_" + e)
        for e in events) or "0U"
