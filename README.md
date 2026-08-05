# boom3 — Doom 3 (dhewm3) libretro core

A libretro core running the Doom 3 engine (based on dhewm3 1.5.5), for use in
RetroArch and other libretro frontends. **One core runs both Doom 3 and
Doom 3: Resurrection of Evil**: the compiled game module is the d3xp (RoE)
code, a superset of the base game, and the title is selected at load time -
automatically when the content sits in a directory named `d3xp` (the retail
layout), or explicitly via the `Game` core option.

## Game data

Load the core with the path to a `.pk4` file (or the game directory). The
engine locates the rest of the data relative to it.

- **Retail disc data works unpatched**: 1.0/1.1 disc installs are supported
  directly (missing script symbols from pre-1.2 data are linked optionally).
  1.3.1-patched data and the Steam/GOG/Humble releases work as well.
- **The free demo** (`demo00.pk4` from `doom3-linux-1.1.1286-demo`) is fully
  playable and is what the in-tree test harness uses.
- **Resurrection of Evil**: load a pk4 from the `d3xp` directory (base data
  must be installed beside it, as on a retail install).
- Environmental reverb uses the game's `efxs/<map>.efx` files where present
  (retail data ships them; the console logs `sound: found efxs/...` per map).
- Pre-1.3.1 language files lack menu strings the (1.3.1-era) game code
  queries; the console hints once and shows raw `#str_` ids in a few menu
  spots. 1.3.1-patched data provides the strings.

## Requirements

OpenGL 1.4 compatibility profile with ARB vertex/fragment programs, VBOs,
cube maps, and a depth+stencil framebuffer (stencil shadow volumes), or
OpenGL ES 2.0 via the translated shader path. RetroArch's GL/glcore video
drivers satisfy this; the core requests depth and stencil through the
HW-render contract.

30-bit HDR mode additionally needs a frontend implementing the
`HDR10_2101010` pixel format and HDR environment queries (RetroArch
1.22+), frontend HDR output enabled, and an HDR display; the FP16/FP32
scene precisions need float render-target support (any desktop GPU).
On frontends or drivers without a 10-bit path the core logs the refusal
and falls back to 24-bit.

File I/O uses a hybrid VFS: local-first (memory-mapped paks and all
zero-copy paths intact, byte-identical on desktop), with the
frontend's VFS as fallback for URI-shaped paths and on sandboxed
platforms (Android scoped storage) - where content unreachable by
direct I/O works through the frontend, at the cost of the zero-copy
tier for those files only.

## Core options

| Option | Meaning |
| --- | --- |
| `Framerate` | Output/presentation rate, 30–240 or `auto` (display rate). Simulation always runs 60 Hz game tics; higher rates render interpolated frames between tics. |
| `Resolution` | Internal render resolution. |
| `Quality Preset` | Engine quality preset (`com_machineSpec`), auto-detected by default. Requires restart. |
| `Invert Y Axis`, `Mouse Sensitivity` | Input tuning. Mouse deltas are accumulated fractionally — no motion is lost at any sensitivity. |
| `Game` | `auto` (default) selects Doom 3 or Resurrection of Evil from the content directory name; `doom3`/`d3xp` force it. |
| `Shadow Smoothing` | Soft-edged stencil shadows. |
| `Color Format (Restart Required)` | `24-bit (Standard)`: the stock XRGB8888 path. `30-bit Color (HDR)`: renders into a 10-bit target and emits HDR10 (PQ, Rec.2020) honoring the frontend's paper-white, peak-luminance, and Colour Boost settings, with dithered 10-bit quantization. Needs frontend HDR output and an HDR display; works under both the gl and glcore video drivers. |
| `HDR Scene Precision (Restart Required)` | Scene buffer depth in 30-bit HDR mode. `10-bit` quantizes each additive light pass to 10-bit steps with one stop of fold headroom. `FP16 (Half-Float)` removes per-pass quantization; `FP32 (Full-Float)` adds full-float accumulation for extreme translucent stacking where half-float rounding could absorb tiny contributions - at 4x the 10-bit bandwidth, the heavy-hit tier. |
| `HDR True Multi-Pass Blending (Restart Required)` | On an FP16/FP32 scene buffer, stacked translucent passes accumulate past white instead of clamping per pass: smoke planes, fog volumes, and explosion clouds keep internal structure instead of saturating flat. `Disabled` restores per-pass clamping - the escape hatch if float-buffer particle overdraw costs too much fillrate. No effect on the 10-bit buffer. |
| `HDR Scene Headroom` | Preserves overbright lighting in 30-bit HDR mode (`Enabled`/`Disabled`): the scene encodes at half intensity so light sums that clamped at white survive, and the output pass restores scale - clipped highlights regain shape and land above paper white. HUD/menus stay at paper white. ~1 bit of shadow precision, covered by dither. |
| `HDR Direct Light Projection` | Bright particle clusters (sparks, embers) drive a pool of real dynamic lights in 30-bit HDR mode (`Disabled`/`2 Lights`/`4 Lights`), casting localized diffuse and specular onto nearby surfaces through the normal shading path. Shadowless; each light adds interaction passes - 4 is the heavy setting. |
| `HDR Specular Boost` | Scales specular reflection energy on lit surfaces in 30-bit HDR mode (`Disabled`/`Moderate (2x)`/`Strong (3x)`): restores reflective punch that an SDR ceiling flattens; boosted highlights bleed into HDR headroom via bloom. HUD/menus untouched; no effect in 24-bit mode. |
| `HDR Bloom` | Multi-band bloom for 30-bit HDR mode (`Disabled`/`Subtle`/`Standard`/`Intense`): bright content is extracted in linear light, blurred at two scales, and composited before the roll-off so glow genuinely exceeds paper white into HDR headroom. Firefly-compressed at extraction. No effect in 24-bit mode. |
| `HDR Highlight Roll-Off` | Shape of highlight expansion into HDR headroom. `Reinhard (Soft-Knee)` is the reference: mid-tones match 24-bit exactly, only the top end eases toward the peak. `ACES (Filmic)` visibly lifts mid-tone brightness as well as reshaping highlights - a deliberately brighter filmic look inherent to normalizing a compressive curve across expansion headroom. |
| `Sound: HRTF (Headphones)` | Binaural rendering via the built-in KEMAR HRTF. `Auto` (default) defers to the `s_HRTF` cvar; `Enabled`/`Disabled` override it. Headphones only. |
| `Sound Samplerate (Hint)` | Audio output rate: 32000/44100/48000/96000 or `auto` (ask the frontend for its device rate and render directly at it, so nothing downstream resamples). Doom 3's assets are authored at 44100. Resolved once at startup. |

Notable cvars (console): `g_frameInterpolation` (render-side interpolation,
default on), `s_useReverb` / `s_reverbGain` (environmental reverb),
`s_HRTF` (binaural rendering for headphones), `s_outputLimiter`
(soft-knee output saturator), `com_showFPS`. See the Sound section and
Configuration.md.

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

**Savestates.** Supported through the engine's savegame machinery, fully
in memory, with a per-tic cache so repeated size/serialize queries within
a tic are free. A restore into the **same map** takes a fast path: media
and render geometry are retained, the loading screen is skipped entirely,
and only entity state rebuilds; restoring into a different map is a full
synchronous load. The core declares the `MUST_INITIALIZE` serialization
quirk (no state exists before a map is loaded — frontends should query
again in-game). States are platform- and endian-dependent, and multiple
megabytes each, which is why rewind remains impractical regardless of
support being declared.

**Input latency.** One core frame input-to-photon; input is polled every
frame including zero-tic frames, mouse wheel maps to the game's wheel keys,
and at output rates above 60 fps mouse look renders sub-tic.

## 30-bit HDR output

With `Color Format` set to 30-bit, the engine renders unmodified —
gamma-encoded Rec.709, exactly as always — into a private scene target,
and a final pass converts to what an HDR10 swapchain expects: exact
inverse-sRGB linearization, highlight roll-off across the paper-white →
display-peak headroom, Rec.709→2020 gamut rotation honoring the
frontend's Colour Boost setting, SMPTE ST 2084 (PQ) encoding over
absolute 0–10000 nits, and a ±0.5 LSB dither against the sRGB→PQ remap's
banding. Paper white, peak luminance, gamut mode, and output mode are
re-queried from the frontend every frame. HDR10 and scRGB swapchains
share the encoding — verified colorimetrically identical on hardware.

The enhancement stack layers on top, each independently switchable:
multi-band **bloom** (extracted in linear light, firefly-compressed,
composited before the roll-off so glow genuinely exceeds paper white);
**specular boost** on lit surfaces only; **scene headroom** (the scene
encodes at half intensity so additive light sums that clamped at white
survive, restored at output — HUD stays at exactly paper white);
**scene precision** up to FP16/FP32 targets that eliminate per-pass
write quantization, where **true multi-pass blending** unclamps
accumulation entirely — stacked smoke and fog keep internal structure
instead of saturating flat — and `_currentRender` copies follow the
scene's depth so refraction effects stop re-quantizing to 8-bit; and
**direct light projection**, which turns the largest particle clusters
in view into a pool of real dynamic lights casting genuine diffuse and
specular on nearby surfaces. Performance ladder, cheap to heavy:
10-bit → FP16 → FP32, and 0 → 2 → 4 particle lights; everything is
inert in 24-bit mode, which remains the untouched stock path byte for
byte.

`Reinhard (Soft-Knee)` roll-off is the reference (mid-tones match
24-bit exactly); `ACES (Filmic)` deliberately lifts mid-tone brightness
along with its filmic shoulder — inherent to normalizing a compressive
curve across expansion headroom, documented so it isn't mistaken for a
calibration error.

## Sound

The sound system is a from-scratch software mixer replacing dhewm3's
OpenAL backend, built for byte-exact reproducibility. There is no audio
thread and no ring buffer: each `retro_run` mixes exactly the frames it
emits (a rational accumulator distributes `rate/framerate` with a
multiple-of-8 carry, so the block sequence is a pure function of the
frame index), and the master sample clock advances only by mixed audio —
no wall clock exists anywhere in the audio path.

**Two pipelines, one semantics.** When the frontend negotiates float
output, mixing runs in float with the output normalization folded into
the per-block gains; otherwise an all-integer pipeline runs: s16
samples, Q15 gains with a 2.0 ceiling (products proven to fit a single
32-bit multiply), int32 accumulation, round-half-up requantizers, and
closed-form gain ramps — bit-exact across compilers and architectures
by construction, with any auto-vectorization bit-identical. Overload is
handled at the final narrow by a stateless soft-knee saturator
(`s_outputLimiter`, default on): identity below ¾ full scale — normal
content stays byte-exact — and a monotonic rational knee above, so
stacked loud sounds compress instead of squaring off, with no permanent
level pad and no lost resolution.

**Spatialization.** Distance/occlusion attenuation as in the original
engine, plus a spectral half the original never had: portal-occluded
sources pass a 5 kHz one-pole shelf (`hfGain = s_occlusionGainHF ^
detour-meters`), implemented twice — float state for the float pipeline,
toward-zero-truncating integer state for the s16 pipeline, so the
integer pipeline's determinism claim survives it. `s_HRTF` (default
off, headphones) replaces the two-speaker pan law for spatialized mono
sources with binaural convolution from the baked MIT KEMAR set: 368
directions, full-phase 128-tap responses (ITD in the taps, per-channel
state is just FIR history), per-block bilinear direction blending,
resampled once at startup to the output rate, and bit-deterministic in
the s16 pipeline via int64 tap sums.

**Environmental reverb.** The EFX preset database drives a built-in
8-line FDN (predelay, early-reflection taps, series allpasses, per-line
low shelf and damping, Householder feedback, echo, modulation, density
tap slew, EAXREVERB pan vectors, output band split) — every parsed
EAXREVERB property is implemented. Twin implementations: float, and a
pure-integer path whose toward-zero requantizers guarantee strict
contraction, so tails provably decay to exact zero. Per-source sends
carry room rolloff and air-absorption HF loss (a second per-channel
shelf, also int/float twinned). The float path flushes sub-denormal
state and both paths gate to zero cost on silence — the FDN never
grinds on dead air, and FTZ-on/FTZ-off runs reconverge to identical
exact zero (asserted by the bench via MXCSR). The ROE enviro-suit
muffling is ported to this mixer and processes the final mix after the
reverb.

**Decode and resampling.** WAV/OGG via libretro-common; PCM is
resampled once at load with the integer sinc, streaming Ogg through the
float sinc pinned to its scalar path (SIMD mask 0) so the same binary
produces the same bytes on every machine. Sinc handles are pooled, and
resampler ring/phase state is serialized so a restored stream continues
bit-exactly.

**Savestates.** The state blob carries a versioned DSP section: the
reverb and enviro objects as guarded raw images, every per-channel
shelf state, binaural FIR history for active channels, decoder stream
and resampler state, the music-diversity RNG seed, the audio pacing
accumulators, and the output sample rate (mismatched-rate loads skip
the section with a logged warning instead of failing subtly). A restore
is byte-reproducible against an uninterrupted run.

**Verification.** `bench/snd_mix_bench.cpp` is the correctness harness:
kernel bit-exactness scalar-vs-SIMD on x86-64, aarch64, and armv7
(qemu), integer-reverb impulse/decay and feature oracles, FTZ
invariance, saturator properties (exhaustive identity region,
monotonicity, symmetry), HRTF ITD/ILD and history-carry tests, and
float-vs-integer agreement bounds. The retro_host frontend closes the
loop with byte-exact audio capture of full game runs.

## Building

    make -C neo -j$(nproc)            # boom3_libretro (runs both games)

Pure `make`/GCC; autodetects Linux/MinGW/macOS. **Header changes require a
clean build** — the Makefile has no dependency tracking.

## Testing

`tools/retro_host/` is a headless libretro frontend used to verify this
core: raw audio capture as a byte-exact determinism oracle, in-game
savestate round-trips, and a framebuffer probe — all runnable under
xvfb/llvmpipe with the free demo data. See its README.

## Credits and license

GPL. Based on [dhewm3](https://dhewm3.org) (id Software's Doom 3 GPL
release); libretro port originally by Rinnegatamante. See COPYING.txt.
