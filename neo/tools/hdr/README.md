# HDR frame harness

Drives the core's own composite under OSMesa - `hdr_ensure_target`,
`hdr_bind_scene`, `hdr_present` - rather than a reimplementation of
them.  Those functions are static and inlined away at -O3, so
libretro.cpp is rebuilt once at `-O1 -fno-inline`, its statics are
globalised with `objcopy`, and this links against that object plus
every other object the build already produced.

    cd neo && make -j4 && tools/hdr/run.sh

## What it takes to run core code outside the frontend

Four things the frontend and engine normally do first.  Each was found
by instrumenting until the fault moved, and missing any one of them
makes every HDR path decline in silence rather than complain:

  - `rglgen_resolve_symbols`, for the core's GL symbols.
  - every `qgl` pointer, bound by including `renderer/qgl_proc.h` with
    the same macro the engine uses.  Picking out the few that
    `hdr_arb_available()` tests is not enough - the program loader also
    calls `qglGetError`, `qglGetString` and `qglGetIntegerv`, and a null
    one of those faults before anything can report why.
  - the four ARB program entry points, which are declared outside
    `qgl_proc.h` and assigned by `R_CheckPortableExtensions`.
  - `idLib::Init()`, because the composite assembles its program text
    with `idStr::snPrintf`.

## What it asserts

It renders the frame twice, under Reinhard and under Filmic Log, and
requires two things: the scene changes with the curve, and the HUD does
not.  That second one is the whole claim behind mapping the scene
before the HUD is drawn, and it cannot be tested with the default
roll-off alone - Reinhard's soft knee is the identity below 0.75, so a
0.5 panel comes out at 0.5 whether or not anything bypasses the curve.
Testing with the default is how the bug hid in the first place.

Before the scene was mapped ahead of the HUD, this failed:

    curve 0:  floor 22, wall 113, highlight 149, HUD 106
    curve 18: floor 11, wall 130, highlight 149, HUD 125
    FAIL: the HUD changed with the curve (106 vs 125)

and afterwards it passes:

    curve 0:  floor 22, wall 113, highlight 148, HUD 106
    curve 18: floor 12, wall 131, highlight 148, HUD 106
    HUD is curve-independent at 106, scene moved 113 -> 131

Same harness, same objects, one changed.  That is the only reason to
believe the pass says anything.

## Negative controls

A harness that has never been shown to fail is not evidence.  The first
version of this file reported PASS while testing nothing: it printed
`floor 13, wall 153, highlight 255`, which are the input quads at 0.05,
0.6 and 6.0 scaled to bytes and clamped.  Every HDR path had declined
and `glReadPixels` handed back what the harness itself had drawn.

So it now checks the output is not the input, and ships two controls:

    ./frame_harness              # PASS
    ./frame_harness --no-bind    # FAIL: the scene never reaches the target
    ./frame_harness --no-arb     # FAIL: the composite declines
    ./frame_harness --stale-map  # FAIL: the mapped image is never rebuilt

It also pumps three swaps per run, because the engine does: every menu
frame and every loading-screen update is a whole frame - composite,
present, rebind - and there are many of them inside one retro_run.
Testing a single swap misses anything that only goes wrong on the
second, which is where a black title screen lived.

## What it is for

Every HDR fault that reached a build in this work was in the sequence,
not the arithmetic: a table uploaded with the default unpack alignment,
so its rows arrived sheared, with no GL error; a flag in the scene
target's alpha whose meaning depended on each 2D material's blend mode;
and a full-screen pass that bound its program but none of the state its
draw needed.  None is visible to assembling a program or checking that
a call did not error.
