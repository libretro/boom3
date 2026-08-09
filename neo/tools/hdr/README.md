# HDR frame harness

Runs the composite the way a frame runs it, not the way a shader test
does.

Every HDR bug that reached a release build in this work was invisible to
an assembly check:

  - a lookup table uploaded with the default unpack alignment, so its
    rows arrived sheared.  `glTexImage2D` returned no error; the data was
    simply wrong.
  - a flag stored in the scene target's alpha, whose meaning depended on
    which blend mode each 2D material happened to use.
  - a second full-screen pass that bound its program but none of the
    state its draw needed, so it rendered nothing and the frame was
    black.

All three passed "the program assembles" and "the call did not error".
None of them could have passed this, which renders a synthetic scene
through the real programs and reads the pixels back.

    gcc -O1 -o frame_harness frame_harness.c \
        -I../../renderer -lOSMesa -lGL -lm
    ./frame_harness                 # expect PASS
    ./frame_harness --omit-state    # reproduces the black frame

It needs the three programs extracted to /tmp/h_comp.arb, /tmp/h_enc.arb
and /tmp/h_vp.arb.  What it checks:

  - the frame is not black
  - each region - HUD, lit wall, highlight - is distinct and ordered
  - the HUD value survives the mapped target unchanged, which is the
    whole point of mapping the scene before the HUD is drawn

The `--omit-state` switch is not a debug aid; it is the regression.  It
reproduces the exact omission that shipped, so the harness can be shown
to fail on the bug rather than merely to pass on the fix.
