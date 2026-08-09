/*
===========================================================================

ACES 2.0 output transform, stage one: Hellwig2022 JMh.

Ported from the Academy reference CTL.  CPU only - the shader backends
will consume hue tables built from this, not this code.

===========================================================================
*/

#ifndef __ACES2_JMH_H__
#define __ACES2_JMH_H__

typedef struct {
	double	RGB_to_CAM16_c[9];
	double	CAM16_c_to_RGB[9];
	double	cone_to_Aab[9];
	double	Aab_to_cone[9];
	double	F_L_n;
	double	cz;
	double	inv_cz;
	double	A_w_J;
	double	inv_A_w_J;
} aces2JMhParams_t;

/* prims is red xy, green xy, blue xy, white xy - the CTL's Chromaticities */
void	ACES2_InitJMhParams( const double prims[8], aces2JMhParams_t *p );

void	ACES2_RGBToJMh( const double RGB[3], const aces2JMhParams_t *p, double JMh[3] );
void	ACES2_JMhToRGB( const double JMh[3], const aces2JMhParams_t *p, double RGB[3] );

/* The achromatic axis on its own.
 *
 * Y here is on the reference's luminance scale, where diffuse white is
 * 100 and not 1 - the parameters are built with XYZ_w scaled by
 * ref_luminance.  Passing a 0..1 value gives a plausible-looking answer
 * that is wrong by about 89% in J, which is exactly what it did to me
 * the first time. */
double	ACES2_JToY( double J, const aces2JMhParams_t *p );
double	ACES2_YToJ( double Y, const aces2JMhParams_t *p );

#endif /* !__ACES2_JMH_H__ */

/*
===========================================================================

The display gamut cusp table.

For each hue, the cusp is the most colourful point the display's own
primaries can reach - the edge of the RGB cube, seen in JMh.  The gamut
compression stage needs it per pixel, and finding it involves a search,
so it is found once per hue here and read back by interpolation later.

TotalTableSize is the reference's 360 entries plus two that wrap the
ends, so interpolation across 0 and 360 degrees needs no special case.

===========================================================================
*/

#define ACES2_TABLE_SIZE		360
#define ACES2_TABLE_TOTAL		( ACES2_TABLE_SIZE + 2 )
#define ACES2_BASE_INDEX		1

typedef struct {
	double	J[ACES2_TABLE_TOTAL];
	double	M[ACES2_TABLE_TOTAL];
	double	h[ACES2_TABLE_TOTAL];
} aces2CuspTable_t;

void	ACES2_BuildCuspTable( const aces2JMhParams_t *limit, double peakLuminance,
							  aces2CuspTable_t *table );
void	ACES2_CuspForHue( const aces2CuspTable_t *table, double hue, double JM[2] );

/*
===========================================================================

The ACES 2.0 tone scale.

Forward takes scene-referred linear and returns luminance in cd/m2.
Inverse takes that luminance DIVIDED by the 100 reference, not the
luminance itself - the reference's own comment says cd/m2 but every
caller divides first, and taking it at its word makes the round trip
fail by four orders of magnitude.

===========================================================================
*/

typedef struct {
	double	n, n_r, g, t_1, c_t, s_2, u_2, m_2;
	double	forward_limit;
	double	inverse_limit;
} aces2TSParams_t;

void	ACES2_InitTSParams( double peakLuminance, aces2TSParams_t *p );
double	ACES2_ToneScaleFwd( double x, const aces2TSParams_t *p );
double	ACES2_ToneScaleInv( double Yn, const aces2TSParams_t *p );

/*
===========================================================================

Chroma compression.

Between the tone scale and the gamut compression.  The tone scale moves
J and leaves M alone, which on its own makes tone-mapped colour look
more saturated than it was; this pulls M back by an amount that depends
on how far up the lightness range the pixel landed, and on how much
reach the rendering space has at that hue.

The reach table is the second of the two hue tables - the outer limit of
M at maximum lightness in the reach primaries, found per hue by search.

===========================================================================
*/

typedef struct {
	double	reachM[ACES2_TABLE_TOTAL];
	double	limit_J_max;
	double	model_gamma_inv;
	double	sat;
	double	sat_thr;
	double	compr;
	double	chroma_compress_scale;
} aces2ChromaParams_t;

void	ACES2_InitChromaParams( const aces2JMhParams_t *input,
								const aces2JMhParams_t *reach,
								const aces2TSParams_t *ts,
								double peakLuminance,
								aces2ChromaParams_t *p );

void	ACES2_ChromaCompressFwd( const double JMh[3], double tonemappedJ,
								 const aces2ChromaParams_t *p, double out[3] );
void	ACES2_ChromaCompressInv( const double JMh[3], double J,
								 const aces2ChromaParams_t *p, double out[3] );
