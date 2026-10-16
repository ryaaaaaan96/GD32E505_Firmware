#!/usr/bin/env python3
"""输入拒绝、可复现生成及生成结果的真实 C 编译/运行检查。"""
from pathlib import Path
import copy
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from points import model
from generate_points import render, write_outputs

BASE = json.loads((ROOT / "app/protocol/config/points.json").read_text())


class PointsTest(unittest.TestCase):
    def test_reject_invalid_definitions(self):
        cases = [
            ("duplicate key", lambda d: d["devices"][0]["signals"][1].update(
                key=2001)),
            ("duplicate device", lambda d: d["devices"][1].update(device_id=1)),
            ("default overflow", lambda d: d["devices"][0]["signals"][1].update(
                default=256)),
            ("default outside range", lambda d: d["devices"][0]["signals"][1]
                .update(default=201)),
            ("bad range", lambda d: d["devices"][0]["signals"][1].update(
                range={"min": 100, "max": 0})),
            ("version overflow", lambda d: d["devices"][0]["signals"][0]
                .update(data_version=4294967296)),
            ("negative version", lambda d: d["devices"][0]["signals"][0]
                .update(data_version=-1)),
            ("unused version", lambda d: d["devices"][0]["signals"][0]
                .update(persistent=False, data_version=1)),
            ("unknown property", lambda d: d.update(comment="typo")),
            ("unknown type", lambda d: d["devices"][0]["signals"][0].update(
                type="float")),
            ("identifier collision", lambda d: d["devices"][0]["signals"][0]
                .update(name="COUNT")),
            ("bad RAW default", lambda d: d["devices"][0]["signals"][5]
                .update(default=[1, 2])),
            ("struct tail padding", lambda d: next(t for t in
                d["devices"][0]["types"] if t["kind"] == "struct")
                .update(size=18)),
            ("struct offset", lambda d: next(t for t in
                d["devices"][0]["types"] if t["kind"] == "struct")
                ["fields"][1].update(offset=2)),
            ("C injection", lambda d: d["devices"][0].update(name="X;exit(1)")),
        ]
        for name, mutate in cases:
            with self.subTest(name=name):
                document = copy.deepcopy(BASE)
                mutate(document)
                with self.assertRaises(ValueError):
                    model.validate(document)

    def test_reject_invalid_maps(self):
        cases = [
            lambda m: m["target"].update(sig_key=65000),
            lambda m: m["target"].update(param="nonexistent"),
            lambda m: m.update(quantity=1),
            lambda m: m.update(address=65535),
            lambda m: m.update(area="input_registers", access=["WRITE"]),
            lambda m: m.update(subscribe=["MARK_ONLY", "REQUEST_READ"]),
            lambda m: m.update(subscribe=["UPDATED"]),
            lambda m: m.update(publish=["UPDATED"]),
            lambda m: m.update(poll_time_ms=2147483648),
            lambda m: m.pop("poll_time_ms"),
        ]
        for mutate in cases:
            document = copy.deepcopy(BASE)
            client = next(p for p in document["protocols"]
                          if p["role"] == "client")
            mutate(client["maps"][0])
            with self.assertRaises(ValueError):
                model.validate(document)
        document = copy.deepcopy(BASE)
        server = next(p for p in document["protocols"] if p["role"] == "server")
        server["maps"][1]["address"] = server["maps"][0]["address"]
        with self.assertRaises(ValueError):
            model.validate(document)

    def test_persistent_versions(self):
        document = copy.deepcopy(BASE)
        signals = document["devices"][0]["signals"]
        signals[0]["data_version"] = 4294967295
        signals[0], signals[1] = signals[1], signals[0]
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "points.json"
            source.write_text(json.dumps(document))
            files = render(source)
            self.assertIn("IDU_SIG_TEST_001_KEY, 4294967295U)",
                          files["persistence/sig_persist.inc"])
            self.assertIn("IDU_SIG_TEST_002_KEY, 0U)",
                          files["persistence/sig_persist.inc"])

    def test_loader_rejects_ambiguous_json(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "input.json"
            for raw in ('{"x":1,"x":2}', '{"x":NaN}', '{"x":1.0}'):
                source.write_text(raw)
                with self.assertRaises(ValueError):
                    model.load(source)

    def test_generation_lifecycle(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "output"
            source = Path(folder) / "input.json"
            source.write_text(json.dumps(BASE))
            files = render(source)
            self.assertEqual(write_outputs(output, files), 10)
            times = {n: (output / n).stat().st_mtime_ns for n in files}
            self.assertEqual(write_outputs(output, render(source)), 0)
            self.assertEqual(times, {n: (output / n).stat().st_mtime_ns
                                     for n in files})
            self.assertEqual(write_outputs(output, files, check=True), 0)
            changed = output / "inc/IDU_sig_table.h"
            changed.write_text("edited")
            with self.assertRaises(ValueError):
                write_outputs(output, files, check=True)
            self.assertEqual(changed.read_text(), "edited")
            write_outputs(output, files)
            source.write_text('{"format_version": 999}')
            run = subprocess.run([sys.executable,
                str(ROOT / "scripts/generate_points.py"),
                "--input", str(source),
                "--output", str(output)], capture_output=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertEqual(changed.read_text(), files["inc/IDU_sig_table.h"])
            smaller = copy.deepcopy(BASE)
            smaller["protocols"] = []
            source.write_text(json.dumps(smaller))
            write_outputs(output, render(source))
            self.assertFalse(
                (output / "generated/FAN_modbus_master.inc").exists())
            self.assertTrue((output / "inc/IDU_sig_table.h").exists())

    def test_generated_c_and_static_capacity(self):
        document = copy.deepcopy(BASE)
        # 任意重排：稳定 Key 不变，映射与持久化仍引用正确的新枚举。
        signals = document["devices"][0]["signals"]
        signals[0], signals[1] = signals[1], signals[0]
        for device in document["devices"]:
            for sig in device["signals"]:
                sig["storage"] = "managed"
                sig["persistent"] = False
                sig.pop("data_version", None)
        document["devices"].append({"name": "AUX", "device_id": 3,
            "types": [], "signals": [{"name": "COUNT_VALUE", "key": 9,
            "type": "s32", "flags": [], "default": -2147483648}]})
        client = next(p for p in document["protocols"] if p["role"] == "client")
        client["maps"][0]["target"] = {
            "device": "IDU", "sig_key": 2007, "param": "u32_value"}
        client["maps"][0]["quantity"] = 2
        client["maps"][0]["poll_time_ms"] = "once"
        with tempfile.TemporaryDirectory() as folder:
            temp = Path(folder)
            source = temp / "points.json"
            source.write_text(json.dumps(document))
            write_outputs(temp, render(source))
            (temp / "test.c").write_text(r'''
#include "aBus_instance.h"
#include "aModbus_mapping.h"
#include <assert.h>
#include <string.h>
#define PROTOCOL_BUS_INSTANCE_ID 123U
#include "generated/sig_tables.inc"
#include "generated/FAN_modbus_master.inc"
int main(void) {
    aBusHandle_t *bus;
    _Static_assert(PROTOCOL_SIG_COUNT == 101U, "capacity");
    _Static_assert(PROTOCOL_PERSIST_SIG_COUNT == 0U, "empty persistence");
    _Static_assert(IDU_SIG_TEST_001 == 1U, "reordered index");
#if ABUS_DYNAMIC_ENABLE
    assert(aBusCreate(123U, tables, 3U, &bus) == A_STATUS_OK);
#else
    aBusHandle_t instance;
    aBusSigState_t states[PROTOCOL_SIG_COUNT];
    aBusInstanceStructInit(&instance, states, PROTOCOL_SIG_COUNT);
    bus = &instance;
    assert(aBusInitStatic(123U, tables, 3U, bus) == A_STATUS_OK);
#endif
    uint8_t buffer[128];
    for (size_t t = 0; t < 3; t++) {
        for (size_t i = 0; i < tables[t].sig_count; i++) {
            const aBusSig_t *sig = &tables[t].sigs[i];
            assert(sig->size <= sizeof(buffer));
            aBusGetIndexRequest_t request = {.deviceID = tables[t].deviceID,
                .sigIndex = i, .dst = buffer, .size = sig->size,
                .timeout = A_TIMEOUT_NO_WAIT};
            assert(aBusGetByIndex(bus, &request) == A_STATUS_OK);
            if (sig->default_data)
                assert(memcmp(buffer, sig->default_data, sig->size) == 0);
        }
    }
    assert(client_maps[0].comm.sigIndex == IDU_SIG_TEST_007);
    assert(client_maps[0].comm.paramIndex == IDU_TEST_007_U32);
    assert(client_maps[0].comm.poll_time_ms == ACOMM_POLL_ONCE);
    int32_t value;
    aBusGetKeyRequest_t get = {.deviceID = 3, .sigKey = 9,
        .dst = &value, .size = sizeof(value), .timeout = A_TIMEOUT_NO_WAIT};
    assert(aBusGetByKey(bus, &get) == A_STATUS_OK && value == INT32_MIN);
#if ABUS_DYNAMIC_ENABLE
    assert(aBusDestroy(bus) == A_STATUS_OK);
#else
    assert(aBusDeInitStatic(bus) == A_STATUS_OK);
#endif
}
''')
            for dynamic in (0, 1):
                command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
                    "-Werror", "-pthread", "-fsanitize=undefined",
                    "-fno-sanitize-recover=all", "-DABUS_EVENT_ENABLE=0",
                    "-DABUS_DEF_CHECK_ENABLE=1", "-DABUS_STATIC_ENABLE=1",
                    "-DABUS_LOCK_MODE=1", f"-DABUS_DYNAMIC_ENABLE={dynamic}",
                    "-Wl,-T,func/aBus/port/gcc/aBus_sections_host.ld"]
                for include in (temp, temp / "inc", ROOT / "benchmarks",
                    ROOT / "platform/aLib/include",
                    ROOT / "platform/aOS/public",
                    ROOT / "func/aBus/include", ROOT / "func/aComm/include",
                    ROOT / "func/aModbus/include"):
                    command += ["-I" + str(include)]
                executable = temp / "test"
                command += [str(temp / "test.c"), "func/aBus/src/aBus.c",
                    "benchmarks/host_port.c", "-o", str(executable)]
                subprocess.run(command, cwd=ROOT, check=True)
                subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main(verbosity=2)
