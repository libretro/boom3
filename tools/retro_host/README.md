# retro_host - headless verification harness for boom3_libretro

A ~200-line libretro frontend used for regression testing the core without
RetroArch: SDL2 GL context (works under xvfb + llvmpipe), full HW-render
contract (depth+stencil default framebuffer), float audio negotiation, raw
audio capture, savestate round-trip, and a framebuffer luminance probe.

## Build
    g++ -O1 -g -o host host.cpp -lSDL2 -ldl -lGL

## Usage
    xvfb-run -a ./host <content.pk4> <float:0|1> <audio_capture.raw|/dev/null> \
        <frames> [state_test_frame] [framerate] [probe_from] [probe_to]

- audio capture: raw interleaved stereo, s16 or f32 per the float flag.
  The core's determinism makes captures byte-comparable across runs and
  builds: `cmp` against a reference capture is the regression oracle for
  "did this change alter the simulation".
- state_test_frame N: retro_serialize at frame N, retro_unserialize at
  N+120, verifying the savestate round trip in-game.
- framerate: served to the core's doom_framerate option (e.g. 120).
- probe window: after each video_cb, reads the viewmodel region of the
  default framebuffer and reports mean luminance plus an alternation
  metric (mean |L[i]-L[i-1]| vs |L[i]-L[i-2]|): frame-alternating
  rendering defects (e.g. every-other-frame model dropout) produce
  d1 >> d2, smooth motion produces d1 <= d2.

## Getting in-game headlessly
The engine execs autoexec.cfg from the content directory; a deferred map
command survives the demo-detection filesystem restart:

    printf 'wait 400\nmap game/demo_mars_city1\n' > <contentdir>/autoexec.cfg

The freely distributable Doom 3 demo (demo00.pk4) suffices for all of the
above and loads/plays fully under llvmpipe.
