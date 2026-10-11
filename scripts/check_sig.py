#!/usr/bin/env python3
"""检查参与产品构建的 SIG 清单；只接受显式整数 Key，避免静默漏检。"""
import argparse
from pathlib import Path
import re


def expand(path, active=None):
    active = set() if active is None else active
    path = path.resolve()
    if path in active:
        raise ValueError(f'{path}: recursive include')
    active.add(path)
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    text = re.sub(r'#\s*include\s*"([^"]+)"',
                  lambda m: expand(path.parent / m[1], active), text)
    active.remove(path)
    return text


def check(path):
    text = expand(path)
    declarations = re.findall(r'\bABUS_(?:BOUND_)?SIG\s*\(\s*(\w+)\s*,\s*([^,]+),', text)
    if not declarations:
        raise ValueError(f'{path}: no SIG definitions')
    keys, names = set(), set()
    for name, expression in declarations:
        literal = expression.strip()
        if not re.fullmatch(r'(?:0[xX][0-9a-fA-F]+|0|[1-9][0-9]*)'
                            r'(?:[uU](?:[lL]{1,2})?|[lL]{1,2}[uU]?)?',
                            literal):
            raise ValueError(f'{path}: {name}: Key must be an integer literal')
        key = int(re.sub(r'[uUlL]+$', '', literal),
                  16 if literal.lower().startswith('0x') else 10)
        if not 0 <= key <= 65535 or key in keys or name in names:
            raise ValueError(f'{path}: duplicate/out-of-range SIG {name} key={key}')
        keys.add(key)
        names.add(name)
    return len(keys)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tables', type=Path, nargs='+')
    args = parser.parse_args()
    try:
        for table in args.tables:
            print(f'{table.name}: {check(table)} SIG keys checked')
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
