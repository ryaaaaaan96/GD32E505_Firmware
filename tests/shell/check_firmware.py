#!/usr/bin/env python3
"""Check the current ARM ELF's command layout and Flash load segments."""
from pathlib import Path
import struct
import sys

path = Path(sys.argv[1])
data = path.read_bytes()
assert data[:6] == b"\x7fELF\x01\x01", "Expected little-endian ELF32"
header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
assert header[1] == 40, "Expected ARM ELF"
phoff, shoff = header[4:6]
phsize, phnum, shsize, shnum, strindex = header[8:13]
sections = [struct.unpack_from("<10I", data, shoff + i * shsize)
            for i in range(shnum)]
strings = sections[strindex]
strings_data = data[strings[4]:strings[4] + strings[5]]
named = {}
for section in sections:
    offset = section[0]
    name = strings_data[offset:strings_data.index(0, offset)].decode()
    named[name] = section

commands = named[".ashell_commands"]
marks = named[".ashell_marks"]
count = named[".ashell_command_count"]
assert count[5] == 2, "Count must be a real uint16_t object"
number = struct.unpack_from("<H", data, count[4])[0]
assert number >= 2, "Expected built-in help/version"
assert number == marks[5]
assert commands[5] == number * 12, "ARM command array contains padding"
assert commands[3] % 4 == 0 and count[3] % 2 == 0
for section in (commands, marks, count):
    assert section[2] & 2 and not section[2] & 1, "Expected read-only allocation"
    assert 0x08000000 <= section[3] < 0x08080000, "Expected Flash address"

loads = []
for i in range(phnum):
    segment = struct.unpack_from("<8I", data, phoff + i * phsize)
    if segment[0] == 1 and segment[4]:
        loads.append((segment[3], segment[3] + segment[4]))
loads.sort()
for previous, following in zip(loads, loads[1:]):
    assert previous[1] <= following[0], "Physical load images overlap"
print(f"ARM ELF: {number} read-only commands, uint16 count, no load overlap")
