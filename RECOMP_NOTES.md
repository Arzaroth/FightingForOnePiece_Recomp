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

## Boot

- The game override matches the ELF (entry `0x10001C`, CRC32 `0x14E499F9`).
- `_sceFsSemInit` (0x1E4830) only creates the fileio semaphore once; the runtime's handler for it is a TODO that
  stops execution. The game reaches it through `_sceFsWaitS`, from `sceFsSetIopBuf` / `sceFsSetIopPrio`, which
  Soulcalibur III never calls. It is no longer stubbed and runs as recompiled code.
- `sceDevctl` (0x1E7758) had no runtime handler, so it went through the fileio RPC client, which is never bound
  because `sceFsInit` is a stub, and waited forever on its semaphore. The only caller, 0x1B5A80, sets the CD spindle
  mode (`sceDevctl("cdrom0:", 0x4380 | 0x4381, ...)`), which means nothing on the host: bound to `ret0`.

## Status

Boots far enough to load all 14 IRX modules and start the sound driver (SIF RPC `0x80000780`) and five EE threads,
with GIF path 2/3 traffic but no frame presented. After about 15 s the main thread (entry `0x10001C`) is dormant, and
thread 6 (entry `0x1004F8`, priority 100), which it created just before, is gone. Threads 3 and 4 wait on semaphores
11 and 14 and nothing runs. Next: find out how the main thread ended and what thread 6 did.
