/* 自动生成：按 deviceID、sigKey 排序，只为选定项建立持久化索引。 */
% for sig in persistent:
PERSIST_SIG(${sig["device"]["name"]}_SIG_DEVICE_ID, ${sig["device"]["name"]}_SIG_${sig["name"]}_KEY)
% endfor
