# Fighting for One Piece recompilation notes

## Mapping the ELF

`SLPS_255.45` is stripped, entry `0x10001C`, one loadable segment at `0x100000` (`.text` through `.bss`, 0xAFA0EC bytes
in memory). `recomp/ffop.toml` is `ps2_analyzer` output with the paths made relative to the repository root:

- 223 SCE SDK functions bound to runtime handlers (`[general].stubs`, `name@0xADDRESS`), 366 more recognised as
  library code without a runtime handler.
- No jump tables detected, no patches.
- `ps2_recomp` emits 3,873 sources. It reports unresolved `JR`/`JALR` targets in some functions and promotes
  fallback entries for them (for example 72 in `sub_001EBF40`); these are the first candidates for a Ghidra map and
  hint lists, as in the Soulcalibur III port.

## VU1 microcode

`ps2_vu_aot` finds only 2 microcode fragments (62 instruction pairs) in the ELF. The rest is presumably loaded from
`FILES.FOA` at run time and runs in the VU1 interpreter until it is found and compiled.

## Status

Not booted yet.
