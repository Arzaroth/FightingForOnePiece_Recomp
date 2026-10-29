#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/gs/gs_cpu_backend.h"

#include <cstdlib>
#include <iostream>

namespace
{
    // playMovie(path, params): plays MOVIES/FO_1.PSS or ONEP_OP3.PSS to the end through libmpeg, returns 0. The
    // runtime's libmpeg calls the game's audio stream callback before the game has set up its audio ring, and the
    // callback's memcpy then wipes most of RAM. PS2X_FFOP_MOVIES=1 plays them anyway.
    constexpr uint32_t kPlayMovie = 0x001B6528u;

    // The native target by default: the GS translated to OpenGL at twice the PS2's resolution, the IOP modules
    // recompiled (scripts/recomp_iop.sh), the SPU2 and the IOP on the host clock. The runtime reads these switches at different points of its start, some
    // before game overrides apply, so they are set before main; a value already in the environment wins.
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
        std::cerr << "[ffop] Fighting for One Piece overrides applied" << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Fighting for One Piece (SLPS-25545)", "SLPS_255.45", 0x0010001Cu, 0x14E499F9u, applyFightingForOnePiece)
