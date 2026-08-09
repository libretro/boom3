# HDR frame harness

Runs the core's own composite under OSMesa and reads the frame back.

The point is that it calls the shipped functions - `hdr_ensure_target`,
`hdr_bind_scene`, `hdr_present` - rather than a reimplementation of
them.  Every HDR fault that reached a build in this work was in the
sequence rather than the arithmetic:

  - a lookup table uploaded with the default unpack alignment, so its
    rows arrived sheared.  `glTexImage2D` reported no error.
  - a flag stored in the scene target's alpha, whose meaning depended on
    which blend mode each 2D material happened to use.
  - a second full-screen pass that bound its program but none of the
    state its draw needed, so the frame came out black.

None of those could be seen by assembling a program or checking that a
GL call did not error, and none of them could be seen by a harness that
supplies its own bind order - which is the mistake that let the third
one ship after the first two.

    cd neo && make -j4 && tools/hdr/run.sh

Checks: the frame is not black, and the dark floor, lit wall and blown
highlight come back in that order.  Run it against a known-bad commit
and it fails; that is the only way to know a test is worth anything.
