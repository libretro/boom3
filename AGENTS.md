# AGENTS.md

Instructions for anyone, human or agent, changing this core. Read the
section that covers the code you touch before touching it. Where a rule
has a check, the check runs before delivery and its result goes in the
delivery summary, not in the commit message.

## The core never blocks

`retro_run` returns when the frame is done, and nothing in it waits.

- **No sleeping, spinning or joining on a frame path.** No `Sys_Sleep`,
  busy-wait, `sthread_join`, `vkDeviceWaitIdle`, `glFinish`, fence wait
  with a timeout, or `glClientWaitSync` that waits. A GPU fence is polled
  with a zero timeout and the frame goes on without the result.
- **No locks.** Threads talk through atomics, SPSC rings
  (`retro_spsc`) and eventcount park/wake (`retro_eventcount`). The one
  thread the engine owns, the file system's background reader, is the
  model: the main thread hands it requests and takes results without
  waiting, and a read it gives up on is cancelled, not waited for.
- **The main thread is joined only at teardown**, and the reader reads
  in chunks so that join is bounded (`bgread_read`).
- **Everything threaded is behind `HAVE_THREADS`**, and the
  `HAVE_THREADS=0` build links with no thread symbol in it (the Linux
  workflow checks).
- **No driver round trips on the frame path.** `glGetError`,
  `glGet*`, `glReadPixels`, `vkGetQueryPoolResults` with a wait: at init,
  at a load, or in a developer command, never per frame. Whether a
  driver takes a blit or a format is a property of the objects, probed
  once when they are made.

## Nothing compiles on the draw path

After the backend is up, no pipeline, shader module or GL program is
created at a draw, on any thread, inside `retro_run`, or on a load
screen's frames. This covers the Vulkan graphics pipelines, the GL
programs and their binaries, and anything a renderer adds later. A code
path that creates one at its first use is a bug, even when it runs once
per state and even on the first frame after a level load: that frame is
the one the player watches.

- **Every draw has its pipeline ready.** The Vulkan backend's
  pipelines are warmed ahead of the draw: the passes whose state is
  fixed (depth fill, shadows, interactions, fog, blend lights, the low
  resolution particle pass, the composite) when the backend comes up and
  when the scene format changes (`RB_VK_WarmPipelines`), a material's
  stages when the material is parsed (`RB_VK_WarmMaterial`, from
  `idMaterial::Parse`), and the display transform's post passes and
  composite, whose targets follow the colour format options, at the
  same times and at every option change (`hdr_vk_warm`, through
  `GLimp_WarmPrograms`). The default gate run never draws them, which
  is why `hdr_check.sh` runs every colour format through the host.
- **Every GL program is built at `R_InitOpenGL`, at a material's parse,
  or at the option change that needs it.** `R_InitOpenGL` links the
  renderer's programs, the half resolution particle pass's
  (`RB_LowResPrograms`) and the present's (`GLimp_WarmPrograms`: the
  display transform, the 24-bit present, the VR layer, for the options
  as they stand); `update_variables` calls `GLimp_WarmPrograms` again
  after an option change, so a composite that bakes an option in is
  rebuilt there and not at the next frame. A `*_ready()` function that
  builds on first use is the miss path, kept so a draw still renders;
  every build goes through `R_ProgramBuilt`, which counts one outside
  those windows as made on the draw path, reported at unload as
  "boom3: gl programs %u, made on the draw path %u". `tools/egl_host`
  exits 3 unless it is 0.
- **One place derives the key.** A warm fills a `vkb_draw_t` the way
  the pass fills it and hands it to `vkb_warm`; the draw hands the same
  struct to `vkb_draw`; both go through `vkb_draw_key`. A new field on
  the key, a new pass, or a new program means a new warm, in the same
  change, or the gate below fails.
- **The key holds only what the pipeline reads.** `vkb_pipeline_key_normalize`
  masks the state bits to the ones the pipeline consumes and clears an
  axis the pipeline does not read (a mirror view with the stencil off).
  State the shader reads from its block (the alpha test) is not in the
  key. Before a field moves from the key to a uniform or to dynamic
  state, show the dynamic version renders the same.
- **A miss still renders, and is counted.** `vkb_pipeline` makes a
  pipeline it was not warmed for, so the draw is never skipped or
  approximated, and counts it: `vkb_pipeline_misses`. The core reports
  the count at unload and `tools/vk_host` exits 3 unless it is 0; the
  GL renderers' count is `r_programMisses`, gated by `tools/egl_host`
  the same way.
- **No shader cache as the fix.** A disk cache is for a low-end
  machine's load time. It never removes a stutter: the invariants above
  hold with no cache at all, and a first run with none plays the same as
  a run with a warm one.

### The gate

Any change under `neo/renderer/`, to the GL present or display
transform in `neo/sys/libretro/libretro.cpp`, or to how materials are
parsed runs, by hand before delivery, on the free demo (`demo00.pk4`,
see README) on lavapipe and llvmpipe:

    make -C tools/vk_host && make -C tools/egl_host
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
      tools/vk_host/vk_host neo/boom3_libretro.so <path>/demo00.pk4 1500 from=0
    tools/egl_host/egl_host neo/boom3_libretro.so <path>/demo00.pk4 1500 from=0 \
      doom_renderer=arb2 doom_color_format=24bit-tonemapped

- each host exits 0: zero pipelines or programs made on the draw path
  (`tools/egl_host/hdr_check.sh` runs every GL renderer through every
  display transform and fails on a host that exits otherwise);
- the per-frame hashes it prints equal the parent commit's, run the
  same way (a picture change needs its own reason, stated);
- the pipeline count it prints is reported, and does not grow without
  a reason;
- two runs of the build hash alike, on both renderers:

      tools/egl_host/determinism.sh tools/vk_host/vk_host neo/boom3_libretro.so <path>/demo00.pk4 600
      tools/egl_host/determinism.sh tools/egl_host/egl_host neo/boom3_libretro.so <path>/demo00.pk4 600

  (the Vulkan line wants `VK_ICD_FILENAMES` as above). The script wipes
  the state between runs and exits 3 on a frame that differs.

A run that compares two builds starts both from the same state: the
engine writes its config, keys and the objective screenshots into a
`base/` directory beside the content as well as into the save
directory, and a leftover from the previous run shifts the frame a map
load lands on. Delete both before each run; `determinism.sh` does.

`tools/egl_host/hdr_check.sh` runs the same host over every HDR
configuration and inherits the gate.

## Every frame is a function of the frame count

The same build, from the same state, with the same input draws the same
frame N on every run; that is what makes a frame hash a test. Nothing a
frame draws may come from the wall clock or from a thread's timing.

- **The tic clock is the only clock on a drawn path.** `com_frameTime`
  and `com_ticNumber` advance per `retro_run`. `Sys_Milliseconds`,
  `eventLoop->Milliseconds()` and `Core_Milliseconds` are for logs,
  profiling and the load-time report, never for a GUI's time, a
  material's `time` register (`frameShaderTime`), a pacifier's throttle
  or an animation.
- **A count a thread advances is read at the hand-over, not at the
  pump.** The reader's bytes count when the main thread takes the file
  (`ReadFile`'s prefetch hit, `CompleteBackgroundImageLoads`), not when
  the reader finishes it: what has finished by the time a frame polls
  depends on the reader's speed, and the load bar drawn from it would
  differ from run to run.
- **Nothing is drawn more than once a frame.** The phased map load draws
  the loading GUI from `Frame()`; while it runs, a loader that wants the
  bar moved sets its state (`PacifierUpdate`) and does not call
  `UpdateScreen`, which would render frames nobody sees.

## Zero-copy

Every change looks for copies to remove in the code it touches, and
every delivery says what it found. A copy that stays has a reason a
reader can see.

- **One owner, the rest borrow.** A pak entry is read in place through
  the file view (`GetFileView`), a decoder writes into the memory the
  upload will take, and a borrowed slice outlives every consumer,
  including the driver call it is passed to.
- **No slurps.** Files are mapped (`rzip`) or streamed through
  libretro-common's `filestream`; a file read whole into a fresh buffer
  to be parsed or copied on is a bug when it could be mapped.
- **Nothing per frame that could be per load.** No allocation, no
  `memcpy` of a buffer the GPU could read where it is, no re-upload of
  data that did not change. Frame memory comes from `R_FrameAlloc`,
  which is 16-byte aligned on every pointer width (`tools/alignment`
  checks); a bump allocator that packs variable records rounds its
  stride (`CM_PolygonSize`).
- **Measure, then say.** Bytes copied and allocations made are work
  results and go in the delivery summary. They are not frame-time
  results until a timing shows it.

## Savestates round-trip

`retro_unserialize` followed by `retro_serialize` describes the same
world: every object the save carried, and nothing the restore spawned
anew. `tools/retro_host` runs a serialize at frame N, an unserialize at
N+120 and a serialize again, and exits 3 unless the object list at the
head of the rebuilt state (every entity and thread, by class, in order)
equals the restored one. Anything written to the state uninitialised, or
a clock the restore does not carry, shows there or under UBSan.

## Sanitizers

Before a commit: ASan, UBSan and LeakSan over a level load, a thousand
frames and the savestate round trip, on `tools/retro_host` with the
demo (`make SANITIZE=address,undefined`); TSan over anything that
touches the reader or the Vulkan render thread. The sweep reads clean on
the core today; a new report in a touched file is a regression, and a
report in a file the change did not touch is a finding to report.

## Tests ship with fixes

A regression fix comes with a test that fails on the broken code and
passes on the fix, wired into `.github/workflows` where it needs no game
data (`tools/alignment`, `tools/bgread`, `tools/sinc_state`), and into
`tools/retro_host` or `tools/vk_host` where it needs the demo. A
verification blind spot that let a break through becomes a workflow
lane, not a note in a checklist.

## Portability

Every touched C++ file is cross-compiled for Windows with mingw-w64 and
built on Linux before delivery; a C file added to the core or its tools
is C89 (`-std=c89 -ansi -pedantic -Werror=pedantic
-Werror=declaration-after-statement`), with `/* */` comments and
declarations at the top of the block, and builds with both toolchains.
`neo/sys/libretro-common` is a plain copy of RetroArch's
`libretro-common/`, never the standalone repository: it takes only the
files this core builds and the headers they reach, and a resync is a
copy, nothing merged. Core adaptations go in the core's files, never in
a libretro-common file; what a core needs of a libretro-common module
and cannot reach goes into RetroArch's interface first (the resampler's
`state()` came that way) and arrives here by resync.
