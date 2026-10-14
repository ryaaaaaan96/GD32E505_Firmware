/* 自动生成：容量与地址常量，避免应用静态资源另写魔数。 */
#ifndef PROTOCOL_GENERATED_H
#define PROTOCOL_GENERATED_H
#define PROTOCOL_PERSIST_SIG_COUNT ${len(persistent)}U
#define PROTOCOL_SIG_COUNT ${sum(len(d["signals"]) for d in devices)}U
#define PROTOCOL_PARAM_COUNT ${sum(len(s["definition"].get("fields", [])) for d in devices for s in d["signals"])}U
% for protocol in protocols:
#define ${protocol["name"].upper()}_MAP_COUNT ${len(protocol["maps"])}U
#define ${protocol["name"].upper()}_RANGE_COUNT ${len(protocol.get("ranges", []))}U
% for block in protocol.get("ranges", []):
#define ${protocol["name"].upper()}_${block["name"]}_ADDRESS ${block["address"]}U
#define ${protocol["name"].upper()}_${block["name"]}_QUANTITY ${block["quantity"]}U
% endfor
% endfor
#endif
