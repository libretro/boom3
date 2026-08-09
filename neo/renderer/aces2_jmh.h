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
