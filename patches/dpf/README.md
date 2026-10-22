# DPF patches

CMake applies these to the `deps/DPF` checkout at configure time on Linux (they only change Linux code) (in name order, skipping ones already applied), so
the submodule keeps pointing at upstream DPF. `.gitmodules` sets `ignore = dirty` for DPF, so the patched checkout
doesn't show as modified.

To change a patch: edit `deps/DPF`, then `git -C deps/DPF diff > patches/dpf/<name>.patch` (one patch per change:
reverse-apply the others first, or keep them in separate files). After updating the submodule, reconfigure; CMake
stops with an error if a patch no longer applies.

- `0001-vst3-x11-event-handler.patch`: on Linux, the VST3 UI registers its X11 connection with the host run loop
  (`IRunLoop::registerEventHandler`), so mouse events are handled and redrawn as soon as they arrive. DPF only
  registers a 16 ms timer, which Reaper runs at about 30 Hz, so dragging was choppy. The event callback only
  processes window events (no `uiIdle`), so a redraw's own X11 traffic can't keep waking it; the timer still drives
  idle work. Events Xlib queued while drawing (the fd stays quiet for those) are handled in the same call, for up to
  8 ms. In a test host with a 30 ms timer and 125 Hz mouse motion, frames went from 33/s to 53-66/s under software
  OpenGL (llvmpipe, ~16 ms a frame); with a GPU it should keep up with the mouse. Worth offering upstream.
