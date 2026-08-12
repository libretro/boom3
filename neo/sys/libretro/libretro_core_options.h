#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include "../libretro-common/include/libretro.h"
#include "../libretro-common/include/retro_inline.h"

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition */


struct retro_core_option_v2_category option_cats_us[] = {
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
	{
      "doom_game",
      "Game (Restart Required)",
      NULL,
      "Which game the loaded content belongs to. 'Auto' detects Resurrection of Evil when the content sits in a directory named 'd3xp' (the retail layout); override for unconventional layouts.",
      NULL,
      NULL,
      {
         { "auto",  "Auto (Detect)" },
         { "doom3", "Doom 3" },
         { "d3xp",  "Doom 3: Resurrection of Evil" },
         { NULL, NULL },
      },
      "auto"
   },
	{
      "doom_shadow_smoothing",
      "Smooth Shadows From Moving Lights",
      NULL,
      "Rebuild moving lights' shadow volumes every rendered frame at the interpolated light position. Removes per-tic shadow popping from rotating/swinging lights at output rates above 60 fps, at some CPU cost. No effect at 60 fps.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
	{
      "doom_framerate",
      "Frame rate (Restart Required)",
      NULL,
      "Choose the desired frame rate.",
      NULL,
      NULL,
      {
         { "auto",            "Auto"},
         { "50",              "50fps"},
         { "60",              "60fps"},
         { "72",              "72fps"},
         { "75",              "75fps"},
         { "90",              "90fps"},
         { "100",              "100fps"},
         { "119",              "119fps"},
         { "120",              "120fps"},
         { "144",              "144fps"},
         { "155",              "155fps"},
         { "160",              "160fps"},
         { "165",              "165fps"},
         { "180",              "180fps"},
         { "200",              "200fps"},
         { "240",              "240fps"},
         { "244",              "244fps"},
         { "300",              "300fps"},
         { "360",              "360fps"},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_sound_samplerate",
      "Sound Samplerate (Hint) (Restart Required)",
      NULL,
      "Output sample rate. 'Auto' asks the frontend for the rate its audio "
      "device is actually running at and renders directly at it, so nothing "
      "has to resample. Doom 3's assets are authored at 44.1kHz; picking a "
      "rate explicitly resamples them once at load instead. Applied at "
      "startup.",
      NULL,
      NULL,
      {
         { "auto",        "Auto" },
         { "32000",       "32kHz" },
         { "44100",       "44.1kHz" },
         { "48000",       "48kHz" },
         { "96000",       "96kHz" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_color_format",
      "Color Format (Restart Required)",
      NULL,
      "24-bit is the standard XRGB8888 output. 30-bit Color (HDR) renders "
      "into a 10-bit surface and emits HDR10 (PQ, Rec.2020): use with the "
      "frontend's HDR output enabled on an HDR display. The conversion "
      "honors the frontend's paper white, peak luminance, and Colour Boost "
      "settings, and dithers the 10-bit quantization.",
      NULL,
      NULL,
      {
         { "24bit",     "24-bit (Standard)" },
         { "30bit-hdr", "30-bit Color (HDR)" },
         { NULL, NULL },
      },
      "24bit"
   },
   {
      "doom_hdr_precision",
      "HDR Scene Precision (Restart Required)",
      NULL,
      "Scene buffer depth in 30-bit HDR mode. 10-bit quantizes each "
      "additive light pass to 10-bit steps and caps accumulation at one "
      "stop of headroom via the encoding fold. FP16 (half-float) makes "
      "per-pass quantization effectively disappear and removes the "
      "accumulation ceiling entirely - light sums build unbounded and "
      "roll off only at output. 10-bit also leaves only 2 bits of "
      "destination alpha (4 levels instead of 256), so the handful of "
      "materials that blend against destination alpha band visibly; "
      "FP16 restores full alpha. FP16 costs double the scene buffer "
      "bandwidth and requires float render target support (any desktop "
      "GPU).",
      NULL,
      NULL,
      {
         { "10bit", "10-bit" },
         { "fp16",  "FP16 (Half-Float)" },
         { "fp32",  "FP32 (Full-Float)" },
         { NULL, NULL },
      },
      "10bit"
   },
   {
      "doom_hdr_true_blend",
      "HDR True Multi-Pass Blending (Restart Required)",
      NULL,
      "With an FP16/FP32 scene buffer, lets stacked translucent passes "
      "accumulate past white instead of clamping at every pass - "
      "overlapping smoke planes, thick fog volumes, and explosion "
      "clouds keep their internal structure instead of saturating to "
      "flat white, and the sum rolls off only at output. Disable to "
      "restore per-pass clamping (the classic look, and an escape "
      "hatch if heavy particle overdraw on a float buffer costs too "
      "much fillrate on your GPU). No effect on the 10-bit buffer, "
      "which clamps in hardware.",
      NULL,
      NULL,
      {
         { "enabled",  "Enabled" },
         { "disabled", "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_hdr_luma_blend",
      "HDR Luminance-Aware Highlights",
      NULL,
      "When a pixel is too bright for the buffer, roll it off toward "
      "its own luminance instead of clipping each colour channel "
      "separately. Per-channel clipping changes the colour of anything "
      "overbright - stacked fire, muzzle flashes and coolant glows "
      "drift toward whichever channel saturated first, which is where "
      "the magenta and cyan fringes on bright effects come from. "
      "Rolling off toward luminance keeps the hue and lets the pixel "
      "fade to white the way a real highlight does. Costs about ten "
      "extra shader instructions per fragment. No effect with True "
      "Multi-Pass Blending on a float buffer, which has no ceiling to "
      "roll off against.",
      NULL,
      NULL,
      {
         { "disabled", "Disabled" },
         { "enabled",  "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_headroom",
      "HDR Scene Headroom",
      NULL,
      "Preserves overbright lighting in 30-bit HDR mode. The scene is "
      "encoded at half intensity so light sums that previously clamped "
      "at white survive into the buffer, and the output pass restores "
      "scale - clipped highlights (stacked lights, muzzle flashes, "
      "specular sums) regain their shape and land above paper white. "
      "HUD and menus stay at exactly paper white. Costs exactly one bit "
      "of the 10-bit scene buffer, everywhere and not just in shadows: "
      "the scene occupies 512 of 1024 codes, and the output dither "
      "cannot recover it, because the expand happens before the decode "
      "so each surviving code is already two output codes wide. FP16 "
      "scene precision is the better answer if your GPU supports it "
      "(any desktop GPU does) and makes this option unnecessary - it is "
      "automatically bypassed there. No effect in 24-bit mode.",
      NULL,
      NULL,
      {
         { "enabled",  "Enabled" },
         { "disabled", "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_hdr_particle_lights",
      "HDR Direct Light Projection",
      NULL,
      "Bright particle systems become real light sources in 30-bit HDR "
      "mode: the largest spark/ember clusters in view drive a small "
      "pool of genuine dynamic lights, casting localized diffuse and "
      "specular highlights onto nearby surfaces through the engine's "
      "normal shading path. Shadowless by design. Each active light "
      "adds interaction passes on nearby geometry - the 4-light "
      "setting is the performance-heavy choice. Live-switchable.",
      NULL,
      NULL,
      {
         { "disabled", "Disabled" },
         { "2",        "2 Lights" },
         { "4",        "4 Lights" },
         { NULL, NULL },
      },
      "2"
   },
   {
      "doom_specular_falloff",
      "Specular Falloff Shape",
      NULL,
      "How a specular highlight fades. 'Original' is the game's own "
      "curve, which is exactly zero below a threshold - so a surface "
      "seen at a distance, whose normals have been averaged down by "
      "mipmapping, loses its highlight entirely rather than softening, "
      "and it flickers back as the view moves. 'Tailed' keeps the same "
      "highlight width but fades smoothly to nothing instead of cutting "
      "off, which stops that flicker at the cost of a slightly softer "
      "look on rough surfaces. Takes effect when images are reloaded.",
      NULL,
      NULL,
      {
         { "original", "Original" },
         { "tailed",   "Tailed (less shimmer)" },
         { NULL, NULL },
      },
      "original"
   },
   {
      "doom_hdr_specular",
      "HDR Specular Boost",
      NULL,
      "Scales specular reflection energy on lit surfaces in 30-bit HDR "
      "mode. Dielectric specular is physically a few percent of incident "
      "light and reads as mud under an SDR ceiling; boosting it restores "
      "reflective punch, and highlights that reach the bloom threshold "
      "bleed into HDR headroom. Lit surfaces only - HUD, menus, and "
      "fullbright content are untouched. Needs somewhere to put the "
      "boosted energy, so it is ignored when the scene buffer is 10-bit "
      "AND HDR Scene Headroom is disabled - in that combination it would "
      "only clip more pixels to white. No effect in 24-bit mode.",
      NULL,
      NULL,
      {
         { "disabled", "Disabled" },
         { "moderate", "Moderate (2x)" },
         { "strong",   "Strong (3x)" },
         { NULL, NULL },
      },
      "moderate"
   },
   {
      "doom_hdr_bloom",
      "HDR Bloom",
      NULL,
      "Multi-band bloom for 30-bit HDR mode: bright content (lamps, "
      "plasma, the engine's own glare sprites) is extracted in linear "
      "light, Gaussian-blurred at two scales (tight core plus wide "
      "haze), and composited before the highlight roll-off - so glow "
      "genuinely exceeds paper white and lands in the HDR headroom, "
      "which standard output cannot express. Firefly-compressed at "
      "extraction to prevent specular flicker. No effect in 24-bit "
      "mode.",
      NULL,
      NULL,
      {
         { "disabled", "Disabled" },
         { "subtle",   "Subtle" },
         { "standard", "Standard" },
         { "intense",  "Intense" },
         { NULL, NULL },
      },
      "standard"
   },
   {
      "doom_hdr_aces2_surround",
      "HDR: ACES 2.0 Surround",
      NULL,
      "The viewing-environment compensation from the ACES 2.0 transform: "
      "dim is the reference assumption and the previous fixed behaviour; "
      "dark suits a lights-off room, average a bright one. Changing it "
      "re-solves the transform tables. Only affects the aces2full curve.",
      NULL,
      NULL,
      {
         { "dark", NULL },
         { "dim", NULL },
         { "average", NULL },
         { NULL, NULL },
      },
      "dim"
   },
   {
      "doom_hdr_highlight_desat",
      "HDR: Highlight Desaturation",
      NULL,
      "Khronos PBR Neutral's hue-preservation stage, made available to "
      "the curves that lack one: saturated highlights that would clip per "
      "channel and skew hue are mixed toward the achromatic compressed "
      "peak by how much the peak was compressed. Ignored under Neutral, "
      "ACES 2.0 (Full) and AgX, which manage hue themselves.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_agx_look",
      "HDR: AgX Look",
      NULL,
      "A grade applied only under the AgX transform. Punchy is the "
      "official higher-contrast, higher-saturation look - contrast 1.35 "
      "and saturation 1.4 - expressed on this pipeline's ordering: the "
      "grade sits on the transform's output. Ignored by every other "
      "curve.",
      NULL,
      NULL,
      {
         { "default", NULL },
         { "punchy",  NULL },
         { NULL, NULL },
      },
      "default"
   },
   {
      "doom_specular_aa",
      "Specular Antialiasing (Toksvig)",
      NULL,
      "Widens the specular lobe where the surface is rough at the current "
      "mip footprint, using the normal shortening that mip averaging "
      "already encodes. This band-limits the highlight itself, so subpixel "
      "speculars stop shimmering - with or without supersampling, and it "
      "composes with it. Flat, full-length normals are untouched. Applied "
      "when shaders load, so changing it takes effect on the next level "
      "load or renderer restart.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_ssaa",
      "Supersampling (2x2)",
      NULL,
      "Renders the scene at twice the width and height and resolves each "
      "output pixel from its four samples. Works in both colour modes, "
      "with the resolve each one needs: 24-bit content is display-referred "
      "so the four samples are box-averaged, which is exact for it; 30-bit "
      "HDR content is linear light where a plain average lets one hot "
      "sample dominate and edges against highlights stay roped, so HDR "
      "resolves with a tone-weighted Karis average instead - each sample "
      "weighted by 1 over 1+brightness before the mean and unweighted "
      "after. Costs 4x fill rate and scene memory: at 4K output roughly "
      "130MB for the 24-bit target and 265MB for the HDR one, so it is "
      "priced for high-end hardware or lower output resolutions. Under "
      "HDR the bloom reads slightly tighter because the pyramid runs at "
      "the supersampled size.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "2x2",      "2x2 (4 samples per pixel)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_chromatic",
      "HDR: Chromatic Aberration",
      NULL,
      "Splits red and blue slightly apart toward the edges of the frame, "
      "the way a lens does when it focuses different wavelengths at "
      "different magnifications. It grows with the square of distance "
      "from the centre, so the middle of the screen stays clean and only "
      "the corners fringe. At 4K the corner shifts by about 3 pixels on "
      "Subtle, 6 on Strong and 12 on Heavy; a quarter of the way out from "
      "the centre even Heavy is about a quarter of a pixel, which is the "
      "point - a lens is sharpest where you are looking. In this game the "
      "centre is usually the subject and the corners are usually dark or "
      "bloomed, so the effect reads as a soft anamorphic edge rather than "
      "an obvious split. The offset is a fixed fraction "
      "of the frame rather than a pixel count, so it stays the same "
      "artifact at any resolution, which is what a lens does. This "
      "is a lens artifact, not a film one, and it is separate from "
      "Halation for that reason; they are often used together but they "
      "come from different parts of a camera.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "subtle",   "Subtle (3px corner at 4K)" },
         { "strong",   "Strong (6px)" },
         { "heavy",    "Heavy (12px)" },
         { "debug",    "Debug (show the displacement)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_halation",
      "HDR: Halation",
      NULL,
      "Warms the bloom around bright areas toward red, the way film does. "
      "In a real print the glow around a bright window is orange rather "
      "than white, because light passes through the emulsion, scatters "
      "off the backing and comes back reddened. This tints the existing "
      "bloom rather than adding a pass, so it costs one instruction. It "
      "is a film response, not a shadow effect - it appears around "
      "anything bright, which is what film does. Pairs naturally with "
      "Filmic Log but is not tied to it.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "subtle",   "Subtle" },
         { "strong",   "Strong" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_bloom_convolution",
      "HDR Convolution Bloom",
      NULL,
      "Replaces HDR Bloom's two fixed blur scales with a six-octave "
      "pyramid, accumulated back up with a tent filter. The result "
      "approximates convolving the scene with one wide, heavy-tailed "
      "point spread instead of adding two discrete Gaussians. Glow "
      "carries much further from its source and fades smoothly out, "
      "rather than stopping at a visible edge where the blur kernel "
      "ends - the halo look older engines had. Total bloom energy is "
      "held identical to the two-band path, so the same light is spread "
      "wider: expect softer, dimmer highlight cores and far more "
      "spill. Costs a handful of extra passes on small buffers. "
      "Requires HDR Bloom to be enabled; no effect in 24-bit mode.",
      NULL,
      NULL,
      {
         { "disabled", "Disabled" },
         { "enabled",  "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_filmiclog_contrast",
      "Filmic Log: Contrast",
      NULL,
      "Which of Sobotka's seven contrast curves Filmic Log uses. They are "
      "separately authored lookups rather than one curve with a knob, and "
      "what separates them is the low end - at a scene value of 0.002, "
      "the sort of dim specular a distant emergency light leaves on wet "
      "metal, Very Low returns 0.0078 where Very High returns 0.00001. "
      "Lower contrast lifts those highlights enough to tell matte armour "
      "from wet organic growth from a steel door while the room around "
      "them stays dark; higher contrast crushes them for a harder, more "
      "claustrophobic look. Base is Blender's default.",
      NULL,
      NULL,
      {
         { "verylow",  "Very Low (most shadow detail)" },
         { "low",      "Low" },
         { "medium",   "Medium" },
         { "base",     "Base (default)" },
         { "medhigh",  "Medium High" },
         { "high",     "High" },
         { "veryhigh", "Very High (most crushed)" },
         { NULL, NULL },
      },
      "base"
   },
   {
      "doom_hdr_filmiclog_range",
      "Filmic Log: Highlight Range",
      NULL,
      "How many stops above mid grey the Filmic Log roll-off's encode "
      "covers. Blender's default is about 4, which was chosen for camera "
      "footage where 1.0 is a nominal white - it saturates at a scene "
      "value of 19 and everything brighter maps identically. This "
      "engine's HDR scene routinely goes past that: multi-pass lights "
      "accumulate unbounded on an FP16 target and the specular boost "
      "multiplies on top, so bright speculars and bloom land in the flat "
      "region. Widening the range keeps them separable at the cost of "
      "lifting mid grey slightly - 0.389 at 4 stops, 0.416 at 6, 0.442 "
      "at 8. Only affects that one roll-off.",
      NULL,
      NULL,
      {
         { "4",  "4 stops (Blender default)" },
         { "6",  "6 stops (saturates at 64)" },
         { "8",  "8 stops (saturates at 256)" },
         { "10", "10 stops (saturates at 1024)" },
         { NULL, NULL },
      },
      "4"
   },
   {
      "doom_hdr_bloom_range",
      "HDR: Bloom Range",
      NULL,
      "How much brightness the bloom extraction is allowed to carry. The "
      "bright pass limits what it extracts before blurring it, so a "
      "single-texel specular spike cannot flicker the whole halo frame to "
      "frame. That limit was set when the scene was clamped at 1.0, and "
      "it caps every extraction below 1: a lamp at 4 and a lamp at 64 "
      "both bloom at roughly 0.8, so brightness stops mattering. Widening "
      "it keeps the anti-flicker property but lets a brighter source "
      "bloom brighter - at Wide a source at 4 extracts 2.0 instead of "
      "0.8. Needs a curve with headroom to show it, so ACES 2.0 and "
      "Filmic Log benefit most and Reinhard least.",
      NULL,
      NULL,
      {
         { "standard", "Standard (clamped)" },
         { "wide",     "Wide (4x)" },
         { "widest",   "Widest (16x)" },
         { NULL, NULL },
      },
      "standard"
   },
   {
      "doom_hdr_particle_energy",
      "HDR: Particle Energy",
      NULL,
      "Scales additive particle stages - muzzle flashes, plasma bolts, "
      "fireballs, sparks - separately from static emissive surfaces. They "
      "are the same kind of stage to the renderer but want very different "
      "amounts: a monitor reads as a light at 4x, while Filmic Log's "
      "desaturation crosstalk does not begin until about 78 in scene "
      "terms and is obvious around 400. At those levels a projectile core "
      "washes out to white and keeps its hue at the edges, the way a real "
      "camera renders a bright source, instead of clipping to a flat "
      "block of colour. Wants Filmic Log or ACES 2.0; Reinhard has no "
      "range up there to show it.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "bright",   "Bright (8x)" },
         { "intense",  "Intense (32x)" },
         { "blinding", "Blinding (96x)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_spectral_mix",
      "Pseudo-Spectral Light Mixing",
      NULL,
      "The engine lights a surface by multiplying light and albedo one "
      "channel at a time. Real light and pigment interact across a "
      "spectrum, and the per-channel product fails hardest where this "
      "game lives - a narrow, saturated light over a saturated surface. "
      "A 620nm flare on a cyan surface should give a dark red; the "
      "per-channel product gives a green. This mixes them in a basis "
      "fitted against spectral ground truth instead, which halves the "
      "colour error and stops opposing lights meeting in a muddy line. "
      "Blue light on a yellow surface goes from (0.10, 0.18, 0.10) to "
      "(0.13, 0.23, 0.18). Neutrals are unaffected by construction. "
      "Takes effect when programs next load.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_emissive",
      "HDR: Emissive Headroom",
      NULL,
      "Scales the additive stages of materials - the glow layers on "
      "monitors, keypads and light panels - so they reach into the range "
      "above paper white. Without it a screen's texture white is 1.0, the "
      "same value a fully lit wall reaches, so it renders no brighter "
      "than the corridor around it; through ACES 2.0 at a 1000 nit peak "
      "that is 107 nits, where 1.5x reaches 159, 2x reaches 206 and 4x "
      "reaches 355. Only additive stages are "
      "affected, so diffuse surfaces and the overall brightness do not "
      "move. Needs a curve with headroom to spend - ACES 2.0 or Filmic "
      "Log show it most, Reinhard's soft knee least.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "slight",   "Slight (1.5x)" },
         { "subtle",   "Subtle (2x)" },
         { "strong",   "Strong (4x)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_peak",
      "HDR: Display Peak Luminance",
      NULL,
      "Overrides the peak brightness the frontend reports. ACES 2.0's "
      "tone scale is parameterised by this single number and rebuilds "
      "when it moves, so it retargets the whole transform rather than "
      "just scaling the output - mid grey lands on 10.0 nits at a 100 "
      "nit peak, 14.5 at 1000 and 17.8 at 10000, with the roll-off "
      "adjusting to match. Leave on Auto unless the frontend reports "
      "nothing, in which case it falls back to 1000, or unless your "
      "display's real peak differs from what it claims. Only the full "
      "ACES 2.0 transform uses it; the other roll-offs have no notion "
      "of an absolute peak.",
      NULL,
      NULL,
      {
         { "auto",  "Auto (ask the frontend)" },
         { "400",   "400 nits" },
         { "600",   "600 nits" },
         { "1000",  "1000 nits" },
         { "1500",  "1500 nits" },
         { "2000",  "2000 nits" },
         { "4000",  "4000 nits" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_hdr_aces2_gamut",
      "ACES 2.0: Limiting Gamut",
      NULL,
      "Which gamut the full ACES 2.0 transform compresses colour toward. "
      "Its gamut compression works by pulling out-of-range colour onto a "
      "boundary, so the boundary should be the one the display actually "
      "has. Most HDR displays are close to P3, which covers about 72% of "
      "Rec.2020 - leaving this at Rec.2020 on such a panel means the "
      "compression stops short and the panel clips the difference "
      "itself, which is the harsh clipping the transform exists to "
      "avoid. Rec.709 is for an SDR-gamut display. Only affects the full "
      "transform; no other roll-off has a gamut to limit to.",
      NULL,
      NULL,
      {
         { "rec2020", "Rec.2020 (widest)" },
         { "p3",      "P3-D65 (most HDR displays)" },
         { "rec709",  "Rec.709 (SDR gamut)" },
         { NULL, NULL },
      },
      "rec2020"
   },
   {
      "doom_hdr_gt_toe",
      "GT Roll-Off: Toe",
      NULL,
      "Where GT's linear section starts. Everything below this is the "
      "toe, so a larger value gives a longer, softer approach out of "
      "black and slightly darker mid-tones; a smaller one puts more of "
      "the image on the straight section. Only affects the GT "
      "(Uchimura) roll-off.",
      NULL,
      NULL,
      {
         { "0.10", "0.10 (short toe)" },
         { "0.16", "0.16" },
         { "0.22", "0.22 (default)" },
         { "0.30", "0.30" },
         { "0.40", "0.40 (long toe)" },
         { NULL, NULL },
      },
      "0.22"
   },
   {
      "doom_hdr_gt_shoulder",
      "GT Roll-Off: Linear Length",
      NULL,
      "How much of the range stays perfectly linear before the "
      "shoulder takes over. Longer keeps mid-tones truer and bends "
      "later but more sharply; shorter starts easing into the "
      "highlights sooner. Only affects the GT (Uchimura) roll-off.",
      NULL,
      NULL,
      {
         { "0.20", "0.20 (early shoulder)" },
         { "0.30", "0.30" },
         { "0.40", "0.40 (default)" },
         { "0.50", "0.50" },
         { "0.70", "0.70 (late shoulder)" },
         { NULL, NULL },
      },
      "0.40"
   },
   {
      "doom_hdr_expansion",
      "HDR Expansion",
      NULL,
      "The game renders into a 0-1 range, so without expansion the "
      "brightness above paper white carries only bloom and whatever "
      "accumulated past white - ordinary bright pixels stop at paper "
      "white. Expansion lifts the top of the range into that headroom "
      "so highlights read as brighter than white. Mid-tones below the "
      "knee are never moved, which is what keeps the image from "
      "looking washed out. 'Inverse Tonemap' expands each colour "
      "channel on its own: simple, and it over-saturates bright "
      "colours and shifts their hue toward whichever channel was "
      "strongest. 'Hue-Preserving' expands the luminance and scales "
      "the channels together, so a highlight gets brighter along its "
      "own colour instead of drifting. No effect on SDR output.",
      NULL,
      NULL,
      {
         { "disabled", "None" },
         { "inverse",  "Inverse Tonemap (Basic Expansion)" },
         { "hue",      "Hue-Preserving Expansion" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "doom_hdr_rolloff",
      "HDR Highlight Roll-Off",
      NULL,
      "How highlights use the headroom between paper white and the "
      "display's peak in 30-bit HDR mode. Reinhard (Soft-Knee) is the "
      "reference: mid-tones match 24-bit output exactly, only the top "
      "end eases toward the peak, and highlights use the full headroom. "
      "ACES (Filmic) applies the ACES curve normalized to paper white, "
      "which visibly LIFTS mid-tone brightness (mid grey rises from "
      "0.18 to 0.33) as well as reshaping highlights - a deliberate, "
      "brighter filmic look, not a calibration error. Hejl (Filmic) is "
      "the Hejl and Burgess-Dawson curve, lifting mid-tones by about "
      "the same amount as ACES but with a harder shoulder, so "
      "highlights compress later and more abruptly - more contrast in "
      "the upper mid-tones. Both filmic curves flatten out on their "
      "own (ACES at 1.29x paper white, Hejl at 1.46x), so both are "
      "carried out to the display peak by a shoulder rather than "
      "stopping there. Pick Reinhard for a faithful comparison against "
      "24-bit. Where the filmic curves put mid grey, and how far their "
      "highlights reach before the shoulder takes over: GT 0.216 at "
      "1.21x, Hable 0.221 at 4.23x, Drago 0.277 at 7.95x, ACES 0.330 "
      "at 1.29x, Hejl 0.330 at 1.46x, Unreal 0.350 at 1.37x. Lottes is "
      "the outlier - mid grey drops to 0.074 in exchange for a 12.3x "
      "highlight range, so it is far darker and contrastier than the "
      "rest. Two entries mention ACES 2.0 and they are not the same "
      "thing: 'ACES 2.0 Tone Scale' is that curve alone applied per "
      "channel, while 'ACES 2.0 (Full Output Transform)' is the whole "
      "thing - appearance model, chroma compression, gamut compression "
      "- which desaturates highlights toward white instead of letting "
      "one channel clip on its own. It wants real scene range rather than "
      "fabricated range: pair it with FP16 Scene Precision and "
      "unbounded blending, which let multi-pass lights accumulate past "
      "1.0 for real, and turn HDR Expansion off - expansion invents "
      "that range per channel, which arrives at the transform as extra "
      "saturation it then has to undo. It is also absolute: it decides "
      "luminance itself from the display's peak, so the Paper White "
      "setting acts on it as exposure rather than as a straight output "
      "multiply: the scene is exposed by it and the transform then "
      "decides luminance, so the setting still moves the picture where "
      "it says while the highlight roll-off stays the transform's.",
      NULL,
      NULL,
      {
         /* Grouped by family rather than by the order they were added:
            the Reinhards including Devlin's photoreceptor variant, then
            all four ACES entries together - the two fits, the 2.0 tone
            scale and the full 2.0 transform - then the filmic curves,
            then the two modern hue-preserving ones, then the plain
            mathematical ones, then the remaining classics. */
         { "reinhard",  "Reinhard (Soft-Knee)" },
         { "rplain",    "Reinhard (Plain)" },
         { "rext",      "Reinhard (Extended, White 4)" },
         { "jodie",     "Reinhard-Jodie (Hue-Preserving)" },
         { "devlin",    "Reinhard-Devlin (Classic)" },
         { "aces",      "ACES (Narkowicz)" },
         { "acesfit",   "ACES Fitted (Hill)" },
         { "aces2",     "ACES 2.0 Tone Scale" },
         { "aces2full", "ACES 2.0 (Full Output Transform)" },
         { "hejl",      "Hejl (Filmic)" },
         { "filmicalu", "Filmic ALU (Hejl 2015)" },
         { "filmiclog", "Filmic (Sobotka)" },
         { "hable",     "Hable (Uncharted 2)" },
         { "hable2017", "Hable (2017 Piecewise)" },
         { "lottes",    "Lottes (AMD)" },
         { "gt",        "GT (Uchimura)" },
         { "unreal",    "Unreal" },
         { "neutral",   "Khronos PBR Neutral" },
         { "agx",       "AgX" },
         { "drago",     "Drago (Logarithmic)" },
         { "expo",      "Exponential" },
         { "tumblin",   "Tumblin-Rushmeier (Classic)" },
         { "ward",      "Ward (Classic, Linear)" },
         { "schlick",   "Schlick (Classic)" },
         { NULL, NULL },
      },
      "reinhard"
   },
   {
      "doom_hrtf",
      "Sound: HRTF (Headphones)",
      NULL,
      "Binaural rendering of spatialized sounds through the built-in KEMAR "
      "HRTF - positional audio for headphones. Sounds wrong on speakers. "
      "'Auto' leaves the s_HRTF cvar in charge, so console or config "
      "settings are respected; 'Enabled'/'Disabled' override it.",
      NULL,
      NULL,
      {
         { "auto",        "Auto (s_HRTF cvar)" },
         { "disabled",    NULL },
         { "enabled",     NULL },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      "Internal resolution (Restart Required)",
      NULL,
      "Choose the resolution to render at.",
      NULL,
      NULL,
      {
         { "320x240",     NULL },
         { "400x300",     NULL },
         { "512x384",     NULL },
         { "640x480",     NULL },
         { "800x600",     NULL },
         { "1024x768",    NULL },
         { "1152x864",    NULL },
         { "1280x1024",   NULL },
         { "1600x1200",   NULL },

         /* DG added modes */
         { "1280x720",    NULL },
         { "1366x768",    NULL },
         { "1440x900",    NULL },
         { "1400x1050",   NULL },
         { "1600x900",    NULL },
         { "1680x1050",   NULL },
         { "1920x1080",   "1920x1080 (Default)" },
         { "1920x1200",   NULL },
         { "2048x1152",   NULL },
         { "2560x1600",   NULL },
         { "3200x2400",   NULL },
         { "3840x2160",   NULL },
         { "4096x2304",   NULL },
         { "2880x1800",   NULL },
         { "2560x1440",   NULL },
         { "1440x1080",   NULL },
         { "1280x800",    NULL },

         /* 21:9 */
         { "2560x1080",   NULL },
         { "3440x1440",   NULL },
         { "3840x1600",   NULL },
         { "5120x2160",   NULL },

         /* 32:9 */
         { "3840x1080",   NULL },
         { "5120x1440",   NULL },
         { "7680x2160",   NULL },

         { NULL, NULL },
      },
      "1920x1080"
   },
   {
   "doom_machine_spec",
   "Quality Preset (requires core restart)",
   NULL,
   "Select the engine quality preset.",
   NULL,
   NULL,
   {
      { "auto",  "Auto (Detect by RAM)" },
      { "0",     "Low" },
      { "1",     "Medium" },
      { "2",     "High" },
      { "3",     "Ultra" },
      { NULL, NULL },
   },
   "auto"
   },
   {
      "doom_invert_y_axis",
      "Invert Y Axis",
      NULL,
      "Invert the right analog stick's Y axis.",
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_mouse_sensitivity",
      "Mouse Sensitivity (Keyboard + Mouse mode)",
      NULL,
      "Adjust mouse look sensitivity when using the Keyboard + Mouse "
      "input device. Higher values = faster camera movement. "
      "Has no effect in gamepad modes.",
      NULL,
      NULL,
      {
         { "0.5",  "0.5 (Very Slow)" },
         { "1.0",  "1.0 (Slow)"      },
         { "1.5",  "1.5"             },
         { "2.0",  "2.0"             },
         { "2.5",  "2.5"             },
         { "3.0",  "3.0 (Default)"   },
         { "4.0",  "4.0"             },
         { "5.0",  "5.0 (Fast)"      },
         { "6.0",  "6.0"             },
         { "8.0",  "8.0 (Very Fast)" },
         { NULL, NULL },
      },
      "3.0"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us, /* RETRO_LANGUAGE_ENGLISH */
   &options_ja,      /* RETRO_LANGUAGE_JAPANESE */
   &options_fr,      /* RETRO_LANGUAGE_FRENCH */
   &options_es,      /* RETRO_LANGUAGE_SPANISH */
   &options_de,      /* RETRO_LANGUAGE_GERMAN */
   &options_it,      /* RETRO_LANGUAGE_ITALIAN */
   &options_nl,      /* RETRO_LANGUAGE_DUTCH */
   &options_pt_br,   /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   &options_pt_pt,   /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   &options_ru,      /* RETRO_LANGUAGE_RUSSIAN */
   &options_ko,      /* RETRO_LANGUAGE_KOREAN */
   &options_cht,     /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   &options_chs,     /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   &options_eo,      /* RETRO_LANGUAGE_ESPERANTO */
   &options_pl,      /* RETRO_LANGUAGE_POLISH */
   &options_vn,      /* RETRO_LANGUAGE_VIETNAMESE */
   &options_ar,      /* RETRO_LANGUAGE_ARABIC */
   &options_el,      /* RETRO_LANGUAGE_GREEK */
   &options_tr,      /* RETRO_LANGUAGE_TURKISH */
   &options_sk,      /* RETRO_LANGUAGE_SLOVAK */
   &options_fa,      /* RETRO_LANGUAGE_PERSIAN */
   &options_he,      /* RETRO_LANGUAGE_HEBREW */
   &options_ast,     /* RETRO_LANGUAGE_ASTURIAN */
   &options_fi,      /* RETRO_LANGUAGE_FINNISH */
   &options_id,      /* RETRO_LANGUAGE_INDONESIAN */
   &options_sv,      /* RETRO_LANGUAGE_SWEDISH */
   &options_uk,      /* RETRO_LANGUAGE_UKRAINIAN */
};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should only be called inside retro_set_environment().
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
