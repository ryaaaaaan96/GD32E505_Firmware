#!/usr/bin/env python3
"""Exercise metadata-driven debugging without connecting to a target."""
import importlib.util
import json
from pathlib import Path
import tempfile
import sys

sys.dont_write_bytecode = True

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("aclass_debug", root / "scripts/debug.py")
debug = importlib.util.module_from_spec(spec)
spec.loader.exec_module(debug)
with tempfile.TemporaryDirectory(prefix="aclass-debug-") as directory:
    debug.BUILD_ROOT = Path(directory)
    config = debug.BUILD_ROOT / "Debug"
    config.mkdir()
    elf = config / "different product name.elf"
    elf.touch()
    manifest = config / "firmware-Debug.json"
    manifest.write_text(json.dumps({"elf": str(elf), "device": "TestDevice"}))
    assert debug.find_elf("Debug") == elf
    script = debug.write_gdb_script(elf, "127.0.0.1", 2331, "jlink", False, True,
                                    debug.build_info("Debug")["device"])
    commands = script.read_text()
    assert "monitor device TestDevice" in commands and "load\n" in commands
    assert 'file "' in commands and "different product name.elf" in commands
    script = debug.write_gdb_script(elf, "127.0.0.1", 2331, "jlink", True, False)
    commands = script.read_text()
    assert "load\n" not in commands and "monitor reset" not in commands
    try:
        debug.write_gdb_script(elf, "127.0.0.1", 2331, "jlink", False, True,
                               "Bad\nmonitor reset")
    except ValueError:
        pass
    else:
        raise AssertionError("unsafe device name accepted")
print("Debug metadata/override script tests passed")
