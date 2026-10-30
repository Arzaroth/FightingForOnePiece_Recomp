#include "game_overrides.h"
#include "ps2_recompiled_functions.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/frame_pacer.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/native/native_vif1.h"
#include "runtime/native/native_vu1_stream.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
    // playMovie(path, params): plays MOVIES/FO_1.PSS or ONEP_OP3.PSS to the end through libmpeg, returns 0. The
    // runtime's libmpeg calls the game's audio stream callback before the game has set up its audio ring, and the
    // callback's memcpy then wipes most of RAM. PS2X_FFOP_MOVIES=1 plays them anyway.
    constexpr uint32_t kPlayMovie = 0x001B6528u;

    // The native target by default: the GS translated to OpenGL at twice the PS2's resolution, the IOP modules
    // recompiled (scripts/recomp_iop.sh), the SPU2 and the IOP on the host clock. The runtime reads these switches at
    // different points of its start, some before game overrides apply, so they are set before main; a value already in
    // the environment wins.
    [[maybe_unused]] const bool kNativeDefaults = []
    {
        constexpr const char *kDefaults[][2] = {{"PS2X_GS_RENDERER", "native"}, {"PS2X_GS_SCALE", "2"},
                                                {"PS2X_IOP_NATIVE", "1"}, {"PS2X_SPU2_WALLCLOCK", "1"},
                                                {"PS2X_IOP_WALLCLOCK", "1"}};
        for (const auto &[name, value] : kDefaults)
        {
            if (std::getenv(name))
                continue;
#ifdef _WIN32
            _putenv_s(name, value);
#else
            setenv(name, value, 0);
#endif
        }
        return true;
    }();

    void playMovieSkipped(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        setReturnS32(ctx, 0);
        ctx->pc = getRegU32(ctx, 31);
    }

    // The main loop's frame wait (0x197008): signals one semaphore, waits on another that the GS vsync callback
    // (0x19E5F8) releases, and returns the CSR FIELD bit the callback saved (gp-0x1708) when the display is interlaced
    // (sceGsGetGParam()->interlace, 0x4E3F40), else 1. Natively the frame ends on the host clock instead: the main
    // thread sleeps until the frame cap's deadline while the game's other threads run. The engine steps a fixed amount
    // per frame, so the cap (60) keeps the game's speed.
    constexpr uint32_t kFrameWait = 0x0019E718u;
    constexpr uint32_t kGsGParam = 0x004E3F40u;

    // VU1 off the emulated path: every microprogram runs on the native sandbox and what it sends goes to the GL GS back
    // end with its register writes, in stream order. The frame ends at the main loop's frame wait, after the game has
    // sent the frame. FFOP_NATIVE_VU1=0 leaves the programs on VU1.
    bool g_nativeVu1 = false;
    bool g_nativeVu1OnVif1Thread = false;

    void nativeVu1FrameEnd()
    {
        if (!g_nativeVu1)
            return;
        if (g_nativeVu1OnVif1Thread)
            ps2x::native::runOnVif1Thread([] { ps2x::native::Vu1StreamAdapter::instance().frameEnd(); });
        else
            ps2x::native::Vu1StreamAdapter::instance().frameEnd();
    }

    void installNativeVu1(PS2Runtime &runtime)
    {
        const char *gs = std::getenv("PS2X_GS_RENDERER");
        const char *value = std::getenv("FFOP_NATIVE_VU1");
        if (!(gs && std::strcmp(gs, "native") == 0) || (value && value[0] == '0'))
            return;
        ps2x::native::Vu1StreamAdapter::Config config;
        config.screen.originX = 2048.0f - 320.0f;
        config.screen.originY = 2048.0f - 224.0f;
        config.screen.width = 640.0f;
        config.screen.height = 448.0f;
        config.choose = [](ps2x::native::Vu1StreamAdapter &, uint32_t)
        { return ps2x::native::Vu1StreamAdapter::Choice::Sandbox; };
        config.draw = [](ps2x::native::Vu1StreamAdapter &, uint32_t, bool) {};
        config.gsWrites = true;
        g_nativeVu1OnVif1Thread = runtime.memory().vif1Threaded();
        config.onVif1Thread = g_nativeVu1OnVif1Thread;
        ps2x::native::Vu1StreamAdapter::instance().install(config);
        g_nativeVu1 = true;
        std::cerr << "[ffop] native VU1: every program on the sandbox, drawn by the GS back end"
                  << (g_nativeVu1OnVif1Thread ? ", on the VIF1 thread" : ", on the game thread") << std::endl;
    }

    void frameWaitVu1FrameEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        nativeVu1FrameEnd();
        sub_0019E718_0x19e718(rdram, ctx, runtime);
    }

    std::chrono::steady_clock::time_point g_frameStart{};
    uint64_t g_frameStartCycle = 0;

    void nativeFrameWait(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        nativeVu1FrameEnd();
        using Clock = std::chrono::steady_clock;
        EeScheduler &ee = runtime->eeScheduler();
        const Clock::time_point now = Clock::now();
        if (g_frameStart == Clock::time_point{})
        {
            g_frameStart = now;
            g_frameStartCycle = ee.currentCycle();
        }
        Clock::time_point deadline = now;
        if (const uint32_t cap = runtime->framePacer().cap(); cap != 0u)
            deadline = std::max(now, g_frameStart + std::chrono::duration_cast<Clock::duration>(
                                                        std::chrono::duration<double>(1.0 / cap)));
        const uint32_t gp = getRegU32(ctx, 28);
        ee.waitHostDeadline(
            deadline,
            [runtime]
            {
                EeScheduler &ee = runtime->eeScheduler();
                const Clock::time_point woke = Clock::now();
                const double elapsed = std::min(std::chrono::duration<double>(woke - g_frameStart).count(),
                                                FramePacer::kMaxStepSeconds);
                const uint64_t due = g_frameStartCycle + static_cast<uint64_t>(elapsed * EeScheduler::kEeClockHz);
                if (const uint64_t cycle = ee.currentCycle(); due > cycle)
                    ee.accountCycles(static_cast<uint32_t>(std::min<uint64_t>(due - cycle, UINT32_MAX)));
                g_frameStart = woke;
                g_frameStartCycle = ee.currentCycle();
                ee.presentGuestFrame();
            },
            [rdram, gp](R5900Context &resumed)
            {
                int32_t field = 1;
                if (*reinterpret_cast<const int16_t *>(rdram + kGsGParam) == 1)
                    field = (rdram[(gp - 0x1708u + 1u) & PS2_RAM_MASK] >> 5) & 1;
                setReturnS32(&resumed, field);
                resumed.pc = getRegU32(&resumed, 31);
            });
    }

    void applyFightingForOnePiece(PS2Runtime &runtime)
    {
        // The title and menus are stacks of full-screen textured sprites: at the default 4 pixels per GS cycle the
        // modelled drawing takes the whole field and the game runs at a few frames per second.
        runtime.gsForPresentation().setDrawRates({256u, 32u});
        // The CPU rasterizer's GS thread is saturated by those sprites (~25 game frames per second on the title);
        // rasterizing on the backend's workers gives ~54.
        GSCpuBackend::SetAsyncRasterization(true);
        // VIF1/VU1 on the worker thread and presentation through the GS queue: on the EE thread they pushed fight
        // frames past a field (32 -> 46 game frames per second). PS2X_VIF1_THREAD=0 keeps them on the EE thread.
        if (const char *threaded = std::getenv("PS2X_VIF1_THREAD"); !(threaded && threaded[0] == '0'))
        {
            runtime.memory().setVif1Threaded(true);
            runtime.memory().setGsLag(true);
        }
        if (const char *movies = std::getenv("PS2X_FFOP_MOVIES"); !(movies && movies[0] == '1'))
            runtime.registerFunction(kPlayMovie, playMovieSkipped);
        // Frames on the host clock (nativeFrameWait), each showing the newest finished buffer (the game flips after its
        // frame wait returns). FFOP_NATIVE_FRAMES=0 keeps the vblank loop.
        if (const char *frames = std::getenv("FFOP_NATIVE_FRAMES"); !(frames && frames[0] == '0'))
        {
            runtime.eeScheduler().setNativeFrames(true);
            runtime.gsForPresentation().setPresentNewestDisplayBuffer(true);
            runtime.registerFunction(kFrameWait, nativeFrameWait);
        }
        installNativeVu1(runtime);
        if (g_nativeVu1 && !runtime.eeScheduler().nativeFrames())
            runtime.registerFunction(kFrameWait, frameWaitVu1FrameEnd);
        std::cerr << "[ffop] Fighting for One Piece overrides applied" << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Fighting for One Piece (SLPS-25545)", "SLPS_255.45", 0x0010001Cu, 0x14E499F9u, applyFightingForOnePiece)
