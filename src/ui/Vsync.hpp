// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>

namespace xythos {

// Turns off vsync for the window whose OpenGL context is current (call it while drawing). Plugin UIs draw from the
// host's GUI thread, so a buffer swap that waits for the screen's refresh stalls the host's idle loop, which on
// Linux is also what delivers our mouse events: dragging gets choppy. `done` is the drawable it was last applied to,
// so calling it every frame only does the work once per window. Linux (GLX) only; a no-op elsewhere.
void disableVsync(uintptr_t& done);

} // namespace xythos
