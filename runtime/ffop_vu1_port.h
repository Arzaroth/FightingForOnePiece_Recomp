#pragma once

#include "runtime/native/native_vu1_stream.h"

namespace ffop
{
    // The VU1 programs lifted to C++ (recomp/vu1_lift_spec.json, generated/vu1lift/) in the sandbox's place:
    // FFOP_VU1_PORT=1 runs them, FFOP_VU1_PORT=check runs each beside the sandbox and reports where they differ.
    // A program or start pc not lifted stays on the sandbox. Returns whether a mode is on.
    bool installVu1Port(ps2x::native::Vu1StreamAdapter::Config &config);
    // Every few seconds of frames: how many programs ran lifted, and in check mode how many differed.
    void reportVu1Port();
}
