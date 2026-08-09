/*
===========================================================================

ACES 2.0 output transform, stage one: the Hellwig2022 JMh appearance space.

This is a port of the corresponding part of the Academy's reference CTL
(Lib.Academy.OutputTransform.ctl), transcribed rather than reinvented.
Every constant here comes from that file; none of it is tuned by eye.

What this stage does and does not do.  It converts linear RGB in some set
of primaries to and from J (lightness), M (colourfulness) and h (hue
angle), which is the space the rest of ACES 2.0 works in.  The tone
scale, the chroma compression and the gamut compression are separate
stages and are not here yet - what ships today under "ACES 2.0 Tone
Scale" is the tone scale alone, applied per channel, and it stays that
way until those land.

It is CPU-only on purpose.  The two shader backends will need hue tables
built from this, and the tables are built once at init; getting the maths
right in one place first, where it can be tested, is cheaper than getting
it wrong in two shader dialects at once.

===========================================================================
*/

#include "sys/platform.h"
#include "renderer/aces2_jmh.h"

#include <math.h>

/* ---- constants, all from the reference ---- */

static const double ref_luminance = 100.0;
static const double L_A           = 100.0;
static const double Y_b           = 20.0;
/* dim surround */
static const double surround_1    = 0.59;
static const double surround_2    = 0.9;

static const double J_scale             = 100.0;
static const double cam_nl_Y_reference  = 100.0;
static const double cam_nl_offset       = 0.2713 * cam_nl_Y_reference;
static const double cam_nl_scale        = 4.0 * cam_nl_Y_reference;

/* CAM16 primaries, used to build MATRIX_16 */
static const double CAM16_PRI[8] = {
	0.8336,  0.1735,		/* red   */
	2.3854, -1.4659,		/* green */
	0.0870, -0.1250,		/* blue  */
	0.3330,  0.3330			/* white */
};

/* ---- small matrix helpers ---- */

static void mat3_mul( const double a[9], const double b[9], double out[9] ) {
	int i, j, k;
	double t[9];
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			double s = 0.0;
			for ( k = 0; k < 3; k++ ) {
				s += a[i*3+k] * b[k*3+j];
			}
			t[i*3+j] = s;
		}
	}
	for ( i = 0; i < 9; i++ ) {
		out[i] = t[i];
	}
}

/* The CTL's mult_f3_f33 treats the vector as a row: out = v * M. */
static void vec3_mul_mat3( const double v[3], const double m[9], double out[3] ) {
	double t[3];
	int j;
	for ( j = 0; j < 3; j++ ) {
		t[j] = v[0] * m[0*3+j] + v[1] * m[1*3+j] + v[2] * m[2*3+j];
	}
	out[0] = t[0]; out[1] = t[1]; out[2] = t[2];
}

static bool mat3_invert( const double m[9], double out[9] ) {
	const double a = m[0], b = m[1], c = m[2];
	const double d = m[3], e = m[4], f = m[5];
	const double g = m[6], h = m[7], i = m[8];
	const double A =  ( e * i - f * h );
	const double B = -( d * i - f * g );
	const double C =  ( d * h - e * g );
	const double det = a * A + b * B + c * C;
	double inv;

	if ( det == 0.0 ) {
		return false;
	}
	inv = 1.0 / det;
	out[0] = A * inv;
	out[1] = -( b * i - c * h ) * inv;
	out[2] =  ( b * f - c * e ) * inv;
	out[3] = B * inv;
	out[4] =  ( a * i - c * g ) * inv;
	out[5] = -( a * f - c * d ) * inv;
	out[6] = C * inv;
	out[7] = -( a * h - b * g ) * inv;
	out[8] =  ( a * e - b * d ) * inv;
	return true;
}

/*
================
RGBtoXYZ

Chromaticities are laid out as the CTL's Chromaticities struct:
red xy, green xy, blue xy, white xy.
================
*/
static void RGB_to_XYZ_matrix( const double p[8], double out[9] ) {
	const double xr = p[0], yr = p[1];
	const double xg = p[2], yg = p[3];
	const double xb = p[4], yb = p[5];
	const double xw = p[6], yw = p[7];

	const double zr = 1.0 - xr - yr;
	const double zg = 1.0 - xg - yg;
	const double zb = 1.0 - xb - yb;

	double M[9] = {
		xr / yr, 1.0, zr / yr,
		xg / yg, 1.0, zg / yg,
		xb / yb, 1.0, zb / yb
	};
	double Minv[9];
	double W[3] = { xw / yw, 1.0, ( 1.0 - xw - yw ) / yw };
	double S[3];
	int i;

	mat3_invert( M, Minv );
	vec3_mul_mat3( W, Minv, S );

	for ( i = 0; i < 3; i++ ) {
		out[i*3+0] = S[i] * M[i*3+0];
		out[i*3+1] = S[i] * M[i*3+1];
		out[i*3+2] = S[i] * M[i*3+2];
	}
}

/* ---- cone response compression ---- */

static double cone_fwd_abs( double Rc ) {
	const double F_L_Y = pow( Rc, 0.42 );
	return F_L_Y / ( cam_nl_offset + F_L_Y );
}

static double cone_inv_abs( double Ra ) {
	const double Ra_lim = Ra < 0.99 ? Ra : 0.99;
	const double F_L_Y = ( cam_nl_offset * Ra_lim ) / ( 1.0 - Ra_lim );
	return pow( F_L_Y, 1.0 / 0.42 );
}

static double cone_fwd( double v ) {
	const double r = cone_fwd_abs( fabs( v ) );
	return v < 0.0 ? -r : r;
}

static double cone_inv( double v ) {
	const double r = cone_inv_abs( fabs( v ) );
	return v < 0.0 ? -r : r;
}

/*
================
ACES2_InitJMhParams

Builds the per-primaries constants: the two matrices that carry linear
RGB into the cone response space and back, and the scalars the lightness
conversion needs.
================
*/
void ACES2_InitJMhParams( const double prims[8], aces2JMhParams_t *p ) {
	double MATRIX_16[9], RGB_TO_XYZ[9], XYZ_w[3], RGB_w[3];
	double D_RGB[3], RGB_wc[3], RGB_Aw[3];
	double cone_to_Aab[9];
	double M1[9], M2[9], RGB_to_CAM16[9], RGB_to_CAM16_c[9];
	double Y_w, k, k4, F_L, F_L_n, cz, A_w;
	double white[3] = { ref_luminance, ref_luminance, ref_luminance };
	const double base_cone_to_Aab[9] = {
		2.0,        1.0,          1.0 / 9.0,
		1.0,      -12.0 / 11.0,   1.0 / 9.0,
		1.0 / 20.0, 1.0 / 11.0,  -2.0 / 9.0
	};
	const double model_gamma = surround_1 * ( 1.48 + sqrt( Y_b / ref_luminance ) );
	int i;

	{
		double XYZ_to_RGB_cam[9];
		RGB_to_XYZ_matrix( CAM16_PRI, XYZ_to_RGB_cam );
		mat3_invert( XYZ_to_RGB_cam, MATRIX_16 );
	}

	RGB_to_XYZ_matrix( prims, RGB_TO_XYZ );
	vec3_mul_mat3( white, RGB_TO_XYZ, XYZ_w );
	Y_w = XYZ_w[1];
	vec3_mul_mat3( XYZ_w, MATRIX_16, RGB_w );

	k  = 1.0 / ( 5.0 * L_A + 1.0 );
	k4 = k * k * k * k;
	F_L = 0.2 * k4 * ( 5.0 * L_A )
		+ 0.1 * ( 1.0 - k4 ) * ( 1.0 - k4 ) * pow( 5.0 * L_A, 1.0 / 3.0 );
	F_L_n = F_L / ref_luminance;
	cz = model_gamma;

	for ( i = 0; i < 3; i++ ) {
		D_RGB[i]  = F_L_n * Y_w / RGB_w[i];
		RGB_wc[i] = D_RGB[i] * RGB_w[i];
		RGB_Aw[i] = cone_fwd( RGB_wc[i] );
	}

	for ( i = 0; i < 9; i++ ) {
		cone_to_Aab[i] = cam_nl_scale * base_cone_to_Aab[i];
	}
	A_w = cone_to_Aab[0*3+0] * RGB_Aw[0]
		+ cone_to_Aab[1*3+0] * RGB_Aw[1]
		+ cone_to_Aab[2*3+0] * RGB_Aw[2];

	/* MATRIX_RGB_to_CAM16 = (RGB_TO_XYZ * MATRIX_16) * ref_luminance,
	 * then the per-channel adaptation applied on the diagonal. */
	mat3_mul( RGB_TO_XYZ, MATRIX_16, M1 );
	for ( i = 0; i < 9; i++ ) {
		M2[i] = 0.0;
	}
	M2[0] = M2[4] = M2[8] = ref_luminance;
	mat3_mul( M1, M2, RGB_to_CAM16 );
	for ( i = 0; i < 3; i++ ) {
		RGB_to_CAM16_c[i*3+0] = RGB_to_CAM16[i*3+0] * D_RGB[0];
		RGB_to_CAM16_c[i*3+1] = RGB_to_CAM16[i*3+1] * D_RGB[1];
		RGB_to_CAM16_c[i*3+2] = RGB_to_CAM16[i*3+2] * D_RGB[2];
	}

	for ( i = 0; i < 3; i++ ) {
		p->cone_to_Aab[i*3+0] = cone_to_Aab[i*3+0] / A_w;
		p->cone_to_Aab[i*3+1] = cone_to_Aab[i*3+1] * 43.0 * surround_2;
		p->cone_to_Aab[i*3+2] = cone_to_Aab[i*3+2] * 43.0 * surround_2;
	}
	mat3_invert( p->cone_to_Aab, p->Aab_to_cone );

	for ( i = 0; i < 9; i++ ) {
		p->RGB_to_CAM16_c[i] = RGB_to_CAM16_c[i];
	}
	mat3_invert( p->RGB_to_CAM16_c, p->CAM16_c_to_RGB );

	p->F_L_n     = F_L_n;
	p->cz        = cz;
	p->inv_cz    = 1.0 / cz;
	p->A_w_J     = cone_fwd_abs( F_L );
	p->inv_A_w_J = 1.0 / p->A_w_J;
}

/*
================
ACES2_RGBToJMh
================
*/
void ACES2_RGBToJMh( const double RGB[3], const aces2JMhParams_t *p, double JMh[3] ) {
	double rgb_m[3], rgb_a[3], Aab[3];
	int i;

	vec3_mul_mat3( RGB, p->RGB_to_CAM16_c, rgb_m );
	for ( i = 0; i < 3; i++ ) {
		rgb_a[i] = cone_fwd( rgb_m[i] );
	}
	vec3_mul_mat3( rgb_a, p->cone_to_Aab, Aab );

	if ( Aab[0] <= 0.0 ) {
		JMh[0] = JMh[1] = JMh[2] = 0.0;
		return;
	}

	JMh[0] = J_scale * pow( Aab[0], p->cz );
	JMh[1] = sqrt( Aab[1] * Aab[1] + Aab[2] * Aab[2] );
	JMh[2] = atan2( Aab[2], Aab[1] ) * ( 180.0 / M_PI );
	if ( JMh[2] < 0.0 ) {
		JMh[2] += 360.0;
	}
}

/*
================
ACES2_JMhToRGB
================
*/
void ACES2_JMhToRGB( const double JMh[3], const aces2JMhParams_t *p, double RGB[3] ) {
	const double h_rad = JMh[2] * ( M_PI / 180.0 );
	double Aab[3], rgb_a[3], rgb_m[3];
	int i;

	Aab[0] = pow( JMh[0] * ( 1.0 / J_scale ), p->inv_cz );
	Aab[1] = JMh[1] * cos( h_rad );
	Aab[2] = JMh[1] * sin( h_rad );

	vec3_mul_mat3( Aab, p->Aab_to_cone, rgb_a );
	for ( i = 0; i < 3; i++ ) {
		rgb_m[i] = cone_inv( rgb_a[i] );
	}
	vec3_mul_mat3( rgb_m, p->CAM16_c_to_RGB, RGB );
}

/*
================
ACES2_JToY / ACES2_YToJ

The achromatic axis on its own, which the tone scale stage will need.
================
*/
double ACES2_JToY( double J, const aces2JMhParams_t *p ) {
	const double A = pow( fabs( J ) * ( 1.0 / J_scale ), p->inv_cz );
	return cone_inv_abs( p->A_w_J * A ) / p->F_L_n;
}

double ACES2_YToJ( double Y, const aces2JMhParams_t *p ) {
	const double Ra = cone_fwd_abs( fabs( Y ) * p->F_L_n );
	const double J = J_scale * pow( Ra * p->inv_A_w_J, p->cz );
	return Y < 0.0 ? -J : J;
}
