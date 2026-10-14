#!/usr/bin/env python3
"""校验 JSON 并使用 Mako 生成点表；不修改运行逻辑、端口或任务。"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import textwrap

ROOT = Path(__file__).resolve().parents[1]
TEMPLATES = Path(__file__).parent / "points/templates"
MANIFEST = "generated/points_manifest.json"


def format_c(text):
    lines = []
    for line in text.splitlines():
        line = line.rstrip()
        if len(line) > 80 and not line.lstrip().startswith("#") and \
                not line.endswith("\\"):
            indent = line[:len(line) - len(line.lstrip())]
            lines.extend(textwrap.wrap(line, width=80,
                subsequent_indent=indent + "    ", break_long_words=False,
                break_on_hyphens=False, replace_whitespace=False))
        else:
            lines.append(line)
    return "\n".join(lines).rstrip() + "\n"


def render(source):
    try:
        from mako.template import Template
        from points import model
    except ModuleNotFoundError as error:
        raise ValueError("缺少生成依赖，请执行：python -m pip install "
                         "-r scripts/requirements.txt") from error
    document = model.load(source)
    common = {**document, "SCALARS": model.SCALARS,
              "c_value": model.c_value, "c_range": model.c_range,
              "c_events": model.c_events}
    outputs = {}

    def add(path, template, **context):
        compiled = Template(filename=str(TEMPLATES / template),
                            input_encoding="utf-8", strict_undefined=True)
        outputs[path] = format_c(compiled.render(**common, **context))

    for device in document["devices"]:
        name = device["name"]
        add(f"inc/{name}_sig_table.h", "sig_header.h.mako", device=device)
        add(f"sig/{name}_test.inc", "sig.inc.mako", device=device)
    for protocol in document["protocols"]:
        add(f"generated/{protocol['name']}.inc", "maps.inc.mako",
            protocol=protocol)
    add("generated/sig_tables.inc", "tables.inc.mako")
    add("inc/protocol_generated.h", "metadata.h.mako")
    add("persistence/sig_persist.inc", "persist.inc.mako")
    manifest = {
        "format_version": 1,
        "input_sha256": hashlib.sha256(Path(source).read_bytes()).hexdigest(),
        "files": sorted(outputs),
        "devices": [{"name": d["name"], "device_id": d["device_id"],
                     "sig_count": len(d["signals"])}
                    for d in document["devices"]],
        "persistent_count": len(document["persistent"]),
    }
    outputs[MANIFEST] = (json.dumps(manifest, indent=2, ensure_ascii=False)
                         + "\n")
    return outputs


def write_outputs(output, outputs, check=False):
    output = Path(output)
    old_manifest = output / MANIFEST
    previous = (json.loads(old_manifest.read_text())["files"]
                if old_manifest.exists() else [])
    # 仅处理生成器自己记录的相对路径，不删除其他应用文件。
    for name in previous:
        if not isinstance(name, str) or not name:
            raise ValueError("生成清单含非法路径")
        path = Path(name)
        if path.is_absolute() or ".." in path.parts or path.parts[0] not in (
                "generated", "inc", "sig", "persistence"):
            raise ValueError("生成清单含非法路径")
    stale = set(previous) - outputs.keys()
    for name in stale:
        path = output / name
        if path.is_file() and not path.read_text(encoding="utf-8").startswith(
                "/* 自动生成："):
            raise ValueError("拒绝删除没有生成标记的文件：" + name)
    changed = [name for name, content in outputs.items()
               if not (output / name).is_file() or
               (output / name).read_text(encoding="utf-8") != content]
    if check:
        if changed or stale:
            raise ValueError("生成文件不同步：" +
                ", ".join(sorted(set(changed) | stale))
                + "\n请执行 python scripts/generate_points.py")
        return 0
    # 全部校验、渲染成功后才写；单文件原子替换，最后更新清单。
    for name in changed:
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8",
                dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(outputs[name])
        try:
            os.chmod(temporary, 0o644)
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)
    for name in stale:
        (output / name).unlink(missing_ok=True)
    return len(changed) + len(stale)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path,
        default=ROOT / "app/protocol/config/points.json")
    parser.add_argument("--output", type=Path, default=ROOT / "app/protocol")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
        help="只检查输入和生成文件是否同步，不改写源码")
    mode.add_argument("--validate", action="store_true",
        help="校验和试渲染，不写文件")
    args = parser.parse_args()
    try:
        outputs = render(args.input)
        count = 0 if args.validate else write_outputs(
            args.output, outputs, args.check)
    except (OSError, ValueError, KeyError) as error:
        print(f"点表生成失败：{error}", file=sys.stderr)
        return 1
    print(f"点表校验通过：{len(outputs)} 个生成文件，更新 {count} 个")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
