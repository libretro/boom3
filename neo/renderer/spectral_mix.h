/*
===========================================================================

Pseudo-spectral light/albedo mixing.

id Tech 4 lights a surface with a per-channel product, lightColor *
albedo.  Real light and real pigment interact across a spectrum, and the
per-channel product is only a good approximation when both are
broadband.  It fails hardest exactly where this game lives: a narrowband
source over a saturated surface.  Measured against spectra, a 620nm
flare on the ColorChecker cyan patch gives (3.89, 0.25, -0.11), while
the per-channel product gives (-0.34, 1.67, -1.66) - not a muddy version
of the right colour, a different hue.

The engine has no spectra, only RGB, so nothing here recovers a true
answer.  What it does is change the basis the product happens in:

    result = Minv * ( (M * light) * (M * albedo) )

Fitted against ground truth built by lifting both RGB inputs to spectra
(Jakob 2019), multiplying them spectrally and reading the result back.
Over 240 held-out light/albedo pairs the mean colour error - luminance
matched, so this is hue and saturation only - falls from 0.2247 to
0.1188.  Just under half.

The rows sum to one, which is not cosmetic: it makes white a fixed point
of both M and its inverse, so a white light on a white surface returns
exactly white and every neutral in the game is untouched.  Verified to
nine decimal places rather than assumed.  A fit without that constraint
scored no better on held-out data and turned white into 1.155 with a
tint, which is the kind of thing that would have shipped looking like a
gamma bug.

Not wired to anything.  Using it means changing where the product
happens, which is inside interaction.vfp - and that ships in the game's
pak rather than in this tree.

===========================================================================
*/

#ifndef __SPECTRAL_MIX_H__
#define __SPECTRAL_MIX_H__

/* row-major; rows sum to 1 */
static const float spectral_mix_forward[3][3] = {
	0.7393385f, 0.2156547f, 0.0450068f,
	0.0644825f, 0.8529482f, 0.0825692f,
	0.0589680f, 0.0610964f, 0.8799356f
};

static const float spectral_mix_inverse[3][3] = {
	1.3859350f, -0.3476714f, -0.0382636f,
	-0.0964334f, 1.2045286f, -0.1080952f,
	-0.0861814f, -0.0603349f, 1.1465163f
};

#endif /* !__SPECTRAL_MIX_H__ */
