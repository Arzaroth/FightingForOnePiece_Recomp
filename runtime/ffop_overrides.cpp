#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/gs/gs_cpu_backend.h"

#include <cstdlib>
#include <iostream>

namespace
{
    // crt0 calls SetupThread(stack 0x01F80000, size 0x80000): the main stack ends at the top of RAM, where the
    // runtime puts its interrupt and callback stacks by default, so the first timer interrupt overwrote main's
    // saved registers. Keep the runtime's pools and stacks in the kernel RAM below the ELF instead.
    constexpr uint32_t kReservedBase = 0x00080000u;
    constexpr uint32_t kReservedTop = 0x00100000u;
    constexpr uint32_t kMainStackBottom = 0x01F80000u;

    // playMovie(path, params): plays MOVIES/FO_1.PSS or ONEP_OP3.PSS to the end through libmpeg, returns 0. The
    // runtime's libmpeg calls the game's audio stream callback before the game has set up its audio ring, and the
    // callback's memcpy then wipes most of RAM. PS2X_FFOP_MOVIES=1 plays them anyway.
    constexpr uint32_t kPlayMovie = 0x001B6528u;

    void playMovieSkipped(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        setReturnS32(ctx, 0);
        ctx->pc = getRegU32(ctx, 31);
    }

    void applyFightingForOnePiece(PS2Runtime &runtime)
    {
        runtime.configureReservedMemory(kReservedBase, kReservedTop, kMainStackBottom);
        // The title and menus are stacks of full-screen textured sprites: at the default 4 pixels per GS cycle the
        // modelled drawing takes the whole field and the game runs at a few frames per second.
        runtime.gsForPresentation().setDrawRates({256u, 32u});
        // The CPU rasterizer's GS thread is saturated by those sprites (~25 game frames per second on the title);
        // rasterizing on the backend's workers gives ~54.
        GSCpuBackend::SetAsyncRasterization(true);
        if (const char *movies = std::getenv("PS2X_FFOP_MOVIES"); !(movies && movies[0] == '1'))
            runtime.registerFunction(kPlayMovie, playMovieSkipped);
        std::cerr << "[ffop] Fighting for One Piece overrides applied" << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Fighting for One Piece (SLPS-25545)", "SLPS_255.45", 0x0010001Cu, 0x14E499F9u, applyFightingForOnePiece)
