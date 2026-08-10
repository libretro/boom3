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

## Negative controls

A harness that has never been shown to fail is not evidence.  The first
version of this file reported PASS while testing nothing: it printed
`floor 13, wall 153, highlight 255`, which are the input quads at 0.05,
0.6 and 6.0 scaled to bytes and clamped.  Every HDR path had declined
and `glReadPixels` handed back what the harness itself had drawn.

So it now checks the output is not the input, and ships two controls:

    ./frame_harness              # PASS: floor 22, wall 113, highlight 149
    ./frame_harness --no-bind    # FAIL: the scene never reaches the target
    ./frame_harness --no-arb     # FAIL: the composite declines

## What it is for

Every HDR fault that reached a build in this work was in the sequence,
not the arithmetic: a table uploaded with the default unpack alignment,
so its rows arrived sheared, with no GL error; a flag in the scene
target's alpha whose meaning depended on each 2D material's blend mode;
and a full-screen pass that bound its program but none of the state its
draw needed.  None is visible to assembling a program or checking that
a call did not error.
