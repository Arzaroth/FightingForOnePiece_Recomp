#!/usr/bin/env python3
"""usage: vu1_blobs_elf.py <game.elf> <out.elf>

The game keeps most of its VU1 microcode as raw blobs in the ELF, each behind a 32-byte header whose first two
words are the blob's address and size (the header sits right before the blob), and builds the MPG uploads at run
time, so ps2_vu_aot finds no MPG VIFcodes for them. This writes every blob as a chain of MPGs to VU address 0 (where
the game loads them) in the one loadable segment of a minimal ELF, for ps2_vu_aot to compile.
"""
import struct
import sys

HEADER_SIZE = 0x20
VU1_CODE_SIZE = 0x4000
MPG_PAIRS = 256


def load_segments(elf):
    phoff = struct.unpack_from("<I", elf, 0x1C)[0]
    count = struct.unpack_from("<H", elf, 0x2C)[0]
    segments = []
    for index in range(count):
        kind, offset, vaddr, _, filesz, _, _, _ = struct.unpack_from("<8I", elf, phoff + index * 32)
        if kind == 1:
            segments.append((offset, vaddr, filesz))
    return segments


def file_offset(segments, vaddr):
    for offset, base, size in segments:
        if base <= vaddr < base + size:
            return vaddr - base + offset
    return None


def blobs(elf):
    segments = load_segments(elf)
    for offset, _, size in segments:
        for header in range(offset, offset + size - HEADER_SIZE, 16):
            address, length = struct.unpack_from("<2I", elf, header)
            if 0 < length <= VU1_CODE_SIZE and length % 8 == 0 and \
                    file_offset(segments, address) == header + HEADER_SIZE:
                yield elf[header + HEADER_SIZE:header + HEADER_SIZE + length]


def mpg_chain(code):
    out = bytearray()
    for first in range(0, len(code) // 8, MPG_PAIRS):
        pairs = min(MPG_PAIRS, len(code) // 8 - first)
        if first:
            out += bytes(4)
        out += struct.pack("<I", (0x4A << 24) | ((pairs & 0xFF) << 16) | first)
        out += code[first * 8:(first + pairs) * 8]
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())
    with open(sys.argv[1], "rb") as source:
        elf = source.read()
    data = bytearray(4)
    for code in blobs(elf):
        data += mpg_chain(code) + bytes(4)
    data_offset = 0x100
    header = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
    header += struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, 0, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    program = struct.pack("<8I", 1, data_offset, 0, 0, len(data), len(data), 5, 16)
    with open(sys.argv[2], "wb") as out:
        out.write(header + program + bytes(data_offset - len(header) - len(program)) + data)


if __name__ == "__main__":
    main()
