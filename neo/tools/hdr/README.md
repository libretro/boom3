# HDR frame harness

Drives the core's own composite under OSMesa - `hdr_ensure_target`,
`hdr_bind_scene`, `hdr_present` - rather than a reimplementation of
them.  The functions are static and inlined away at -O3, so
libretro.cpp is rebuilt once at `-O1 -fno-inline`, its statics are
globalised with `objcopy`, and this links against that object plus
every other object the build already produced.

    cd neo && make -j4 && tools/hdr/run.sh

## Status: incomplete

It does not yet get through a frame.  `hdr_ensure_target` now returns
true - which took resolving the core's GL symbols through
`rglgen_resolve_symbols` and setting the four ARB entry points
`hdr_arb_available()` tests, both of which the frontend normally does
before any core code runs - but the run faults further in, and the
remaining engine state it needs has not been identified.

That is recorded here rather than left as a passing test, because the
first version of this file *did* report PASS, and it was wrong.  It
reported `floor 13, wall 153, highlight 255`, which are the input quads
at 0.05, 0.6 and 6.0 scaled to bytes and clamped - untouched.  Every
HDR path had declined silently: no proc-address hook, so no ARB entry
points, so `hdr_arb_available()` false, so `hdr_ensure_target` false,
so `hdr_present` returned immediately and the harness read back what it
had drawn itself.

The lesson is the one this whole exercise keeps teaching: a test that
has never been shown to fail on broken code is not evidence.  The
harness now refuses to continue if `hdr_ensure_target` declines, so it
cannot pass that way again.

## What it is for

Every HDR fault that reached a build in this work was in the sequence,
not the arithmetic:

  - a lookup table uploaded with the default unpack alignment, so its
    rows arrived sheared.  `glTexImage2D` reported no error.
  - a flag stored in the scene target's alpha, whose meaning depended
    on which blend mode each 2D material happened to use.
  - a second full-screen pass that bound its program but none of the
    state its draw needed, so the frame came out black.

None of those is visible to assembling a program or checking that a GL
call did not error.
