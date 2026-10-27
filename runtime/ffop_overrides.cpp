#include "game_overrides.h"
#include "ps2_runtime.h"

#include <iostream>

namespace
{
    void applyFightingForOnePiece(PS2Runtime &runtime)
    {
        (void)runtime;
        std::cerr << "[ffop] Fighting for One Piece overrides applied" << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Fighting for One Piece (SLPS-25545)", "SLPS_255.45", 0x0010001Cu, 0x14E499F9u, applyFightingForOnePiece)
