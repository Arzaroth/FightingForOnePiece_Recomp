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

- Main thread stack. crt0 calls `SetupThread(gp, stack 0x01F80000, size 0x80000)`, so the main stack ends at the
  top of RAM, where the runtime grows its interrupt and callback stacks by default. The first timer interrupt
  (libtimer's handler at 0x1BD628, then the kernel alarm handler) wrote its frame over main's saved `ra`/`s0`, `main`
  (0x1BBB60) returned into crt0 and `exit`. Found by breaking on `EeScheduler::makeDormant` in gdb and reading the
  dispatch history (`g_dispatchHistory` in `ps2_runtime.cpp`). `runtime/ffop_overrides.cpp` moves the pools and stacks
  to 0x80000-0x100000 with `configureReservedMemory`, as Soulcalibur III does. sc3-port's
  `runtime/setupthread-stack` branch does the same automatically from the `SetupThread` arguments (tested on FFOP).
- `sceCdLayerSearchFile` (0x1D31D8) retried `SifBindRpc` to the CDVD search server forever; it is bound to the
  runtime's `sceCdSearchFile` (the layer argument does not matter on a single-layer disc).
- Movies. `playMovie(path, params)` at 0x1B6528 plays `MOVIES/FO_1.PSS` and `ONEP_OP3.PSS` through libmpeg. The
  runtime's libmpeg calls the game's DATA stream callback (0x1B8728, registered with `sceMpegAddStrCallback`) before
  the game has set up its audio ring at 0xBE8600, and the callback's `memcpy` then copies 0x110185D5 bytes to 0x3830.
  The override makes `playMovie` return 0 at once; `PS2X_FFOP_MOVIES=1` plays them anyway.

## Drawing

- VIF1 packet builders. The analyzer bound `sceVif1PkOpenDirectCode`, `sceVif1PkCloseGifTag` and `sceVif1PkAddGsAD`
  to runtime handlers, which wrote broken packets next to the game's own `sceVif1PkTerminate`: 77M VIF1 NOPs and no
  IMAGE transfer. They run the game's code now. sc3-port fixed the handlers on `hle/vif1-packet` (same packets as the
  game's code, checked on FFOP).
- memalign. The game's allocator (0x1793B8) calls `memalign` (0x1D78A0), which tail-jumps into `_memalign_r`
  (0x1D78C8, bound to the runtime). `ps2_recomp` had merged the two into one function, so the jump became a local
  `goto` into newlib's own `_memalign_r`, which mallocs a larger block from the runtime and frees its leading
  fragment, here the whole block. Every aligned allocation lived in freed memory; the next one landed on it and the
  resource loader's sprite table (`gp-0x1ac4`) was overwritten with texture data, so every sprite had a null
  definition and the screen stayed black. Fixed in `ps2_recomp` on main (718c312): a function ends at the first
  stub-bound start inside it, so the tail jump reaches the bound `_memalign_r`. (Before that, `memalign@0x1D78A0` was
  bound game-side.) Found with a gdb watchpoint on the table pointer and
  a timeline of `malloc_r` / `guestFree` / `fioRead`.

- Textures. Every PATH2 IMAGE upload (`sceVif1PkRefLoadImage`: CNT [NOP, DIRECTHL 1] + IMAGE tag, then REF [NOP,
  DIRECTHL n] + pixels, TTE on) landed in VRAM 8 bytes off: after the DIRECT that carried the IMAGE tag, the runtime's
  VIF1 took the next bytes as raw image data, so the REF tag's two VIFcodes became the first two texels. PSMT8 tiles
  uploaded as PSMCT32 then looked scrambled, and the CLUT was shifted by two entries. Found by dumping the texture and
  CLUT from guest RAM (clean once run through the GS swizzle in Python) and comparing with the CLUT in a VRAM dump.
  Fixed on `ffop/vif1-path2-image`: the image continues in the next DIRECT/DIRECTHL's data.

## Into the fight

- Main menu: START on the title, CIRCLE to confirm (the game's pad word swaps the PS2 button bytes: 0x0800 START,
  0x1000 UP, 0x0020 CIRCLE, 0x0040 CROSS). `PS2X_PAD_SCRIPT="22:START/800,27:CIRCLE/400,35:CIRCLE/400,43:CIRCLE/400"`
  reaches the Luffy vs Ace fight in about 90 s.
- Speed. The title stacks full-screen textured sprites; at the runtime's default GS fill model (4 pixels per GS cycle)
  the modelled drawing took the whole field and the game ran at ~5 frames per second. The override models 8 pixels per
  GS cycle (`setDrawRates({256, 32})`, as Soulcalibur III): ~24. The profile then showed the CPU rasterizer's
  GSFrontend thread saturated (SampleTexture, WritePixelBlock); `GSCpuBackend::SetAsyncRasterization(true)` draws on
  the backend's workers: ~54 (paraLLEl-GS: ~60). The GS shadow check found no difference on boot and the title. At
  ~24 fps the single-buffered menus (FRAME and DISPFB both at FBP 0) were presented half-drawn; at ~54 they are not.
- After the VS screen the main thread jumped to 0x12C1C8, a function reached only through a data pointer. The 36
  pointer targets `scripts/find_code_pointers.py` finds are in `recomp/code_ptr_hints.txt` and folded into
  `entry_points`.
- Once, the fight load ran a VU1 microprogram that never ended and executed `0xfffffffb` as a lower instruction at
  several PCs, then stalled; the next runs on the same build did not. No MPG upload was truncated (22,469 traced).

## Debugging recipes

- gdb with ASLR off puts RDRAM at the same host address every run (0x7fffe1cd8010 here), so guest variables can be
  read after interrupting the runner: see `build/state.sh`-style scripts (run, SIGINT after N s, `x/..wx`).
- Generated functions are host functions: `break sub_XXXXXXXX_0xaddr` and `dprintf` on them, or on a generated
  source line, give guest call counts and arguments (`ctx->r[n]`). Breakpoints on a function also fire on resumes
  into its middle, so read arguments at the caller's call site instead.
- `PS2X_PAD_SCRIPT` button names are upper case (`START`, `CROSS`); hold a button (`START/20000`) to be sure a poll
  sees it.

## Status

Title, main menu, character select, VS screen and the Luffy vs Ace fight (Round 1, Ready, GO, attacks, the pause
menu) run on PS2Recomp `main` 718c312. Movies are skipped. Next: fight speed, sound, and the rest of the modes.
