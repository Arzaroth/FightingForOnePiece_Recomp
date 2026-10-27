#include "game_overrides.h"
#include "ps2_runtime.h"

#include <iostream>

namespace
{
    // crt0 calls SetupThread(stack 0x01F80000, size 0x80000): the main stack ends at the top of RAM, where the
    // runtime puts its interrupt and callback stacks by default, so the first timer interrupt overwrote main's
    // saved registers. Keep the runtime's pools and stacks in the kernel RAM below the ELF instead.
    constexpr uint32_t kReservedBase = 0x00080000u;
    constexpr uint32_t kReservedTop = 0x00100000u;
    constexpr uint32_t kMainStackBottom = 0x01F80000u;

    void applyFightingForOnePiece(PS2Runtime &runtime)
    {
        runtime.configureReservedMemory(kReservedBase, kReservedTop, kMainStackBottom);
        std::cerr << "[ffop] Fighting for One Piece overrides applied" << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Fighting for One Piece (SLPS-25545)", "SLPS_255.45", 0x0010001Cu, 0x14E499F9u, applyFightingForOnePiece)
