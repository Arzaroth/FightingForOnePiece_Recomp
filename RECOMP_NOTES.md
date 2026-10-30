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

`ps2_vu_aot` finds only 2 MPG uploads (62 instruction pairs) in the ELF. The rest of the VU1 code is 14 raw blobs
in the ELF's data, each behind a 32-byte header whose first two words are its address and size; the game builds
their MPG uploads at run time, so the tool never sees them. In a fight the 16 KB blob at 0x1EDD60 fills VU1 code
memory from address 0, and running it in the interpreter took ~70% of the game thread (sampling profile): ~15 game
frames per second, the same on paraLLEl-GS. `scripts/vu1_blobs_elf.py` writes every blob as an MPG chain to VU
address 0 in a synthetic ELF, which `scripts/compile_vu1.sh` also hands to `ps2_vu_aot`: the fight runs at ~40.
`FILES.FOA` has MPG-shaped data too (105 fragments when wrapped as an ELF), but none of it was resident in a fight.

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
  dispatch history (`g_dispatchHistory` in `ps2_runtime.cpp`). The runtime now keeps its pools and stacks clear of
  the `SetupThread` stack on its own (`runtime/setupthread-stack`, on main); FFOP needs no `configureReservedMemory`.
- `sceCdLayerSearchFile` (0x1D31D8) retried `SifBindRpc` to the CDVD search server forever; it is bound to the
  runtime's `sceCdSearchFile` (the layer argument does not matter on a single-layer disc).
- Movies. `playMovie(path, params)` at 0x1B6528 plays `MOVIES/FO_1.PSS` and `ONEP_OP3.PSS` through libmpeg, fed
  by its own reader: `sceCdStRead` (wrapper 0x1B7950) into `sceMpegDemuxPss` (0x1C95E0, a guest wrapper around the
  bound `sceMpegDemuxPssRing`), from 0x1B89C0. Its stream callbacks (0x1B8610 video, 0x1B8728 data, registered with
  `sceMpegAddStrCallback`) copy each packet into the game's own rings; the NODATA callback (0x1B87E8) DMAs the video
  ring to the IPU. Before playing, 0x1B7EB4 demuxes until `sceMpegDemuxPss` consumes nothing, which on the console
  happens when a stream callback returns 0 (its ring is full). The runtime queues stream callbacks to run later and
  ignores what they return, so demux always consumes everything: the priming loop reads the whole file and then
  polls `sceCdStRead` at the end of the stream forever (0x1B89C0 with blocking=1 never returns 0), so
  `sceMpegGetPicture` is never reached. Fixing it means running stream callbacks synchronously in demux and
  stopping at the first that returns 0. The override makes `playMovie` return 0 at once; `PS2X_FFOP_MOVIES=1` plays
  them anyway.

## Drawing

- Depth. The runtime's `sceGsSetDefDBuff` read `ztest`, `zpsm` and `clear` from the stack instead of `$t0`-`$t2`,
  so ZBUF masked every Z write (0x0000000100000118 where PCSX2 has 0x0000000001000118, Z24 with writes) and hair
  and hat interiors were drawn over the faces; with Z writes on, the double buffer's clear packet (never seeded)
  left the previous frame's Z and the fight was a grey screen. Both fixed on PS2Recomp main 4376493. Found by
  comparing a PCSX2 GS dump (Shift+F8) with ours.
- VIF1 packet builders. The analyzer bound `sceVif1PkOpenDirectCode`, `sceVif1PkCloseGifTag` and `sceVif1PkAddGsAD`
  to runtime handlers, which wrote broken packets next to the game's own `sceVif1PkTerminate`: 77M VIF1 NOPs and no
  IMAGE transfer. sc3-port fixed the handlers on `hle/vif1-packet` (same packets as the game's code), and they are
  bound to the runtime again since main 2e15545.
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

## Native target

On PS2Recomp's native target (sc3/ffop-region-clamp cc3a1db, native/renderer effa1cb plus one fix: MPEG host clock and the GS page coherence included; VU1 still on the sandbox), set by the override unless the
environment says otherwise.

- Sandboxed VU1 draws (gsWrites) under REGION_CLAMP: GifToNative rescaled U and V into the region's window for the
  native renderer's texture cache, and the GS back end then applied the region again, so the title, menus, timer,
  names and banners came out as smeared, shifted strips (fixed on sc3/ffop-region-clamp; FFOP_NATIVE_VU1=0 hid it).
- GS: `PS2X_GS_RENDERER=native` (GSNativeBackend, OpenGL). The menus draw their translucent layers with TEST
  0x5178b (alpha GEQUAL 0x78, AFAIL FB_ONLY); the back end discarded every failing pixel, so the watermarks, title
  gradient and name plate of character select were missing (native/gs-afail: a second pass for the failing pixels
  under AFAIL's masks).
- IOP: the 14 modules recompile with nothing left to the interpreter (`scripts/recomp_iop.sh`); `PS2X_IOP_NATIVE=1`
  runs a module natively when its file hash and base match the dump. The game registers an EE RPC server (sid
  0x80000704), so it needs the SifRegisterRpc fix (args 5-7 in t0-t2).
- Sound: `PS2X_SPU2_WALLCLOCK=1 PS2X_IOP_WALLCLOCK=1`. FFOP streams through SPU2 voices, never AutoDMA (ADMAS stays
  0), so the AutoDMA starvation SC3 found does not apply; a WAV capture matches the IOP-clock run.
- Fights hold 60 game frames per second (5 fights of 60 s, 300 frames per 5 s), against 35-51 on the CPU GS, and
  still 60 at `PS2X_GS_SCALE=2` (1280x896 targets, the default), menus and intro cinematics included.
- Frames: the main loop's frame wait 0x19E718 (it signals one semaphore and waits on another that the GS vsync
  callback 0x19E5F8 releases) is replaced by EeScheduler::waitHostDeadline (native/renderer): the main thread
  sleeps until the frame cap's deadline while the other threads run, then the real EE time is charged and the frame
  presented, showing the newest finished buffer (the game flips after the wait). The engine steps a fixed amount per
  frame (nothing reads the vblank count the callback keeps at gp-0x2744), so the cap of 60 keeps the game's speed.
  Fights at 298-300 frames per 5 s, sound unchanged. `FFOP_NATIVE_FRAMES=0` keeps the vblank loop.
- VU1, still emulation: every microprogram runs on Vu1StreamAdapter's sandbox (SC3's work, on the VIF1 thread), which
  interprets their compiled code, and their output reaches the GL GS back end through the GS front end's register state
  (gsWrites); the adapter's frame ends at 0x19E718. That only moved the programs off the emulated VU1 (no MSCAL left
  there, fights at 299-300 frames per 5 s); the sandbox and gsWrites are emulation by another name, to keep as
  diagnostics only. To remove: port the microprograms to C++ (SC3's scripts/vu1_port_gen.py; a fight puts half of the
  VIF1 thread in four blobs, 382e53ba, 12e72aa9, 81b61065, ea3c12da) and build the draws natively from the game's data.
  `FFOP_NATIVE_VU1=0` puts the programs back on VU1.
- Movies (`PS2X_FFOP_MOVIES=1`, still off by default): the player pre-fills by demuxing until sceMpegDemuxPssRing takes
  less than it is given, which the runtime only did at 128 decoded pictures ahead, more than FO_1.PSS holds; the
  override sets the limit to 3 (the game's 0x17E300-byte sceMpegCreate work area). With native/renderer d698378 and later (idle
  presentation and the MPEG host clock) FO_1.PSS then plays paced on the PC clock under native frames. ONEP_OP3.PSS
  still stalls early.
- `rand` (0x1D9220) runs the game's own code: the runtime stub returned 15-bit values while the game scales by 2^-31,
  so every random pick was 0 and 0x13C818 (a random entry different from the last one) looped forever on Ace's
  special move, on every renderer.

## Status

Playable on the native target (PS2Recomp sc3/ffop-region-clamp cc3a1db, frames on the host clock; VU1 still emulated on the sandbox): title, main
menu, character select, VS screen and Fighting Mode fights against the CPU (rounds, special moves and their cut-ins,
random opponents and stages since the game's own `rand` runs), with music and sound, at a steady 60 game frames per
second with the GS on OpenGL at 2x. Movies are skipped (`PS2X_FFOP_MOVIES=1` still sits on a black screen in
`playMovie`). The other modes reach their screens (VS Fight vs CPU into a two-player character select, Gamble's rank
select, Training, Replay's file select, the option menu; Recording needs 30,000,000 berries first), not played through
yet. Memory card: Option > DATA SAVE creates `BISLPS-25545sys` on `mc0/` next to the ELF (system data 63488 bytes,
`icon.sys`, `ffop.bin`), and the next boot reads it back.

The CPU GS path (`PS2X_GS_RENDERER=cpu`, the interpreter IOP without `scripts/recomp_iop.sh`) still works: there the
fight runs at 33-50 game frames per second, limited by the CPU rasterizer. VIF1/VU1 run on the worker thread with GS
lag (the override; `PS2X_VIF1_THREAD=0` turns it off); on the 8-core box 4 raster workers is the best count (2: ~34
fps, 3: ~41, 4: ~51, 8: ~7, 12: ~5: idle workers spin then yield and starve the busy threads). The rare harness runs
that never reach the fight are not a game hang: when the box is loaded, the boot falls a few seconds behind and the
wall-clock `PS2X_PAD_SCRIPT` presses land on other menu states.

Next: movies (stream callbacks in the MPEG demux, see above; JC2 found the runtime's sceMpeg hands each audio PES
payload to the game's callbacks with its private-stream-1 sub-header), playing the other modes through.
