#!/usr/bin/env python3
"""The lifted programs of recomp/vu1_lift_spec.json as a table for runtime/ffop_vu1_port.cpp: their declarations, then
one FFOP_VU1_PROGRAM(name, hash, pairs, entries...) line each.

  scripts/vu1_lift_table.py <spec.json> <out.inc>
"""
import json
import sys


def number(value):
    return int(value, 0) if isinstance(value, str) else value


def main():
    spec_path, out_path = sys.argv[1:3]
    programs = json.load(open(spec_path))['programs']
    lines = ['namespace ps2x::vu1lift', '{']
    for program in programs.values():
        lines.append('    template <bool kSticky>')
        lines.append(f'    uint64_t {program["function"]}(uint32_t entry, VU1State &state, uint8_t *data, uint32_t top, '
                     'Vu1Kick out, void *user);')
    lines.append('}')
    lines.append('')
    lines.append('#define FFOP_VU1_PROGRAMS \\')
    for program in programs.values():
        entries = ', '.join(f'{number(pc):#x}u' for pc in program['entries'])
        lines.append(f'    FFOP_VU1_PROGRAM({program["function"]}, {number(program["hash"]):#018x}ull, '
                     f'{number(program["pairs"])}u, {entries}) \\')
    lines.append('')
    text = '\n'.join(lines) + '\n'
    try:
        if open(out_path).read() == text:
            return
    except FileNotFoundError:
        pass
    with open(out_path, 'w') as out:
        out.write(text)


if __name__ == '__main__':
    main()
