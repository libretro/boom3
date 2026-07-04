# boom3 — Doom 3 (dhewm3) libretro core

A libretro core running the Doom 3 engine (based on dhewm3 1.5.5), for use in
RetroArch and other libretro frontends. Two cores build from this tree:

- **boom3** — Doom 3 (base game)
- **boom3_xp** — Doom 3: Resurrection of Evil (`make D3XP=1`)

## Game data

Load the core with the path to a `.pk4` file (or the game directory). The
engine locates the rest of the data relative to it.

- **Retail disc data works unpatched**: 1.0/1.1 disc installs are supported
  directly (missing script symbols from pre-1.2 data are linked optionally).
  1.3.1-patched data and the Steam/GOG/Humble releases work as well.
- **The free demo** (`demo00.pk4` from `doom3-linux-1.1.1286-demo`) is fully
  playable and is what the in-tree test harness uses.
- **Resurrection of Evil** requires the boom3_xp core and the d3xp data.
- Environmental reverb uses the game's `efxs/<map>.efx` files where present
  (retail data ships them; the console logs `sound: found efxs/...` per map).

## Requirements

OpenGL 1.4 compatibility profile with ARB vertex/fragment programs, VBOs,
cube maps, and a depth+stencil framebuffer (stencil shadow volumes), or
OpenGL ES 2.0 via the translated shader path. RetroArch's GL/glcore video
drivers satisfy this; the core requests depth and stencil through the
HW-render contract.

## Core options

| Option | Meaning |
| --- | --- |
| `Framerate` | Output/presentation rate, 30–240 or `auto` (display rate). Simulation always runs 60 Hz game tics; higher rates render interpolated frames between tics. |
| `Resolution` | Internal render resolution. |
| `Quality Preset` | Engine quality preset (`com_machineSpec`), auto-detected by default. Requires restart. |
| `Invert Y Axis`, `Mouse Sensitivity` | Input tuning. Mouse deltas are accumulated fractionally — no motion is lost at any sensitivity. |

Notable cvars (console): `g_frameInterpolation` (render-side interpolation,
default on), `s_useReverb` / `s_reverbGain` (environmental reverb),
`com_showFPS`.

## Properties worth knowing

**Deterministic simulation.** Game state is a pure function of input and
frame count: no wall clocks, threads, or rendering feedback reach the
simulation. Audio output is byte-identical across runs — the s16 mixer is
additionally bit-exact across compilers and architectures (pure integer,
Q15 gains), and the reverb shares that property. The core negotiates float
audio output where the frontend supports it and falls back to s16.

**Framerate independence.** 60 Hz simulation with render-side interpolation:
sub-tic time for skeletal animation/particles/shader time, sub-tic mouse
look, and prev→cur transform interpolation for entities and the first-person
view origin — all presentation-only, provably inert to the simulation, and
exact no-ops at 60 fps.

**Savestates.** Supported at the `basic` level (manual save/load, slots,
auto-state) through the engine's savegame machinery, fully in memory. A
restore is a synchronous map reload (a few seconds), so rewind and run-ahead
are not supported. States are platform- and endian-dependent. Note: core
info files older than this feature declare `savestate = "false"` and will
block savestates in RetroArch until updated.

**Input latency.** One core frame input-to-photon; input is polled every
frame including zero-tic frames, mouse wheel maps to the game's wheel keys,
and at output rates above 60 fps mouse look renders sub-tic.

## Building

    make -C neo -j$(nproc)            # boom3_libretro
    make -C neo -j$(nproc) D3XP=1     # boom3_xp_libretro (clean between variants)

Pure `make`/GCC; autodetects Linux/MinGW/macOS. `-D_D3XP` applies globally,
so run `make clean` when switching variants. **Header changes require a
clean build** — the Makefile has no dependency tracking.

## Testing

`tools/retro_host/` is a headless libretro frontend used to verify this
core: raw audio capture as a byte-exact determinism oracle, in-game
savestate round-trips, and a framebuffer probe — all runnable under
xvfb/llvmpipe with the free demo data. See its README.

## Credits and license

GPL. Based on [dhewm3](https://dhewm3.org) (id Software's Doom 3 GPL
release); libretro port originally by Rinnegatamante. See COPYING.txt.
