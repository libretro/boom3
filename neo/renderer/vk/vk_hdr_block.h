/*
 * The display transform's constants, as the Vulkan composite reads them:
 * std140, so a 3x3 matrix is three columns of four floats.  Filled by
 * the HDR present in libretro.cpp, read by shaders/hdr_composite.frag.
 */
#ifndef VK_HDR_BLOCK_H
#define VK_HDR_BLOCK_H

typedef struct vkb_hdr_block
{
   float gamut[12];          /* uGamut: the output primaries */
   float inRgbToCam[12];     /* ACES 2.0: input RGB to CAM16 */
   float inConeToAab[12];
   float limAabToCone[12];
   float limCamToRgb[12];
   float parms[4];           /* paperWhite/10000, headroom H, curve mode, Reinhard knee */
   float gt[4];              /* GT: toe m, S0, 1-S1, C2 */
   float inScalars[4];       /* F_L_n, cz, inv_cz, A_w_J */
   float limScalars[4];
   float ts[4];              /* m_2, s_2, g, t_1 */
   float cc[4];
   float cc2[4];             /* compr, ccScale, mid_J, focus_dist */
   float gm[4];              /* lower_hull_gamma_inv, forward_limit, lut half texel, exposure */
   float lutScale[4];        /* cuspJ, cuspM, reachM, gammaInv */
   float halBloom[4];        /* xyz halation, w bloom amount */
   float ssaaCa[4];          /* xyz supersample offsets and curve select, w chromatic aberration */
   float outEncBandW[4];     /* x 1 PQ / 0 gamma 2.4, y dither LSB, zw band weights */
   float expandEncFrame[4];  /* xy expansion mode and knee, z 1/encode scale, w frame */
   float filmDesat[4];       /* xyz filmic terms, w highlight desaturation */
   float aces[4];            /* shoulder A, gradient, GT normalisation */
} vkb_hdr_block_t;

/* a GL column-major 3x3 into a block's std140 mat3 */
#define VKB_HDR_MAT3(dst, m) do { \
   (dst)[0] = (m)[0]; (dst)[1] = (m)[1]; (dst)[2]  = (m)[2]; (dst)[3]  = 0.0f; \
   (dst)[4] = (m)[3]; (dst)[5] = (m)[4]; (dst)[6]  = (m)[5]; (dst)[7]  = 0.0f; \
   (dst)[8] = (m)[6]; (dst)[9] = (m)[7]; (dst)[10] = (m)[8]; (dst)[11] = 0.0f; } while (0)

#endif
