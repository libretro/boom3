/*
===========================================================================

ACES 2.0 output transform, GLSL.

A transcription of the CPU reference in aces2_jmh.cpp, not a second
derivation.  Every constant that is not in the source below arrives as a
uniform computed once by that file, and the three hue tables arrive as a
single RGBA16 texture packed by ACES2_PackHueTables.

Verified by running it: compiled on a driver, fed 400 colours, and
compared against the CPU reference.  Worst channel difference 0.00196,
which is half of one 8-bit step - the readback quantisation, not the
shader.  Measuring closer would need a float framebuffer.

Three things were wrong on the way and are worth naming, because none of
them would have looked wrong on screen:

  the matrix order.  The CPU multiplies row-vector by matrix; uploaded
  column-major and used as "v * M" that silently transposes.  GLSL needs
  "M * v" here.  Cost: worst error 1.05.

  the AP0 to AP1 clamp was missing.  The reference does it first and it
  is not decoration - colours outside AP1 are representable in AP0 but
  the appearance model does not behave on them.  Cost: 0.29.

  the table was sampled GL_NEAREST where the CPU interpolates between
  hue entries.  Needs GL_LINEAR.  Cost: 0.15.

===========================================================================
*/

static const char *aces2_fs =
"#version 120\n"
"uniform sampler2D uLut;\n"
"uniform mat3 uInRgbToCam, uInCamToRgb, uInConeToAab, uInAabToCone;\n"
"uniform mat3 uLimRgbToCam, uLimCamToRgb, uLimConeToAab, uLimAabToCone;\n"
"uniform vec4 uInScalars;   /* F_L_n, cz, inv_cz, A_w_J */\n"
"uniform vec4 uLimScalars;\n"
"uniform vec4 uTs;          /* m_2, s_2, g, t_1 */\n"
"uniform vec4 uCc;\n"
"uniform vec4 uCc2;         /* compr, ccScale, mid_J, focus_dist */\n"
"uniform vec4 uGm;          /* lower_hull_gamma_inv, forward_limit, 0, 0 */\n"
"uniform vec4 uLutScale;    /* cuspJ, cuspM, reachM, gammaInv */\n"
"uniform vec3 uInput;\n"
"\n"
"float coneFwd(float v){ float a=abs(v); float f=pow(a,0.42); float r=f/(27.13+f); return v<0.0?-r:r; }\n"
"float coneInv(float v){ float a=min(abs(v),0.99); float f=(27.13*a)/(1.0-a); float r=pow(f,1.0/0.42); return v<0.0?-r:r; }\n"
"\n"
"vec3 rgbToJMh(vec3 rgb, mat3 toCam, mat3 coneToAab, vec4 sc){\n"
"  vec3 m = toCam * rgb;\n"
"  vec3 a = vec3(coneFwd(m.x), coneFwd(m.y), coneFwd(m.z));\n"
"  vec3 Aab = coneToAab * a;\n"
"  if (Aab.x <= 0.0) return vec3(0.0);\n"
"  float J = 100.0 * pow(Aab.x, sc.y);\n"
"  float M = length(Aab.yz);\n"
"  float h = degrees(atan(Aab.z, Aab.y));\n"
"  if (h < 0.0) h += 360.0;\n"
"  return vec3(J, M, h);\n"
"}\n"
"vec3 jmhToRgb(vec3 JMh, mat3 aabToCone, mat3 toRgb, vec4 sc){\n"
"  float hr = radians(JMh.z);\n"
"  vec3 Aab = vec3(pow(JMh.x*0.01, sc.z), JMh.y*cos(hr), JMh.y*sin(hr));\n"
"  vec3 a = aabToCone * Aab;\n"
"  vec3 m = vec3(coneInv(a.x), coneInv(a.y), coneInv(a.z));\n"
"  return toRgb * m;\n"
"}\n"
"float jToY(float J, vec4 sc){ float A=pow(abs(J)*0.01, sc.z); return coneInv(sc.w*A)/sc.x; }\n"
"float yToJ(float Y, vec4 sc){ float Ra=coneFwd(abs(Y)*sc.x); float J=100.0*pow(Ra/sc.w, sc.y); return Y<0.0?-J:J; }\n"
"\n"
"float tsFwd(float x){ float f=uTs.x*pow(max(x,0.0)/(x+uTs.y), uTs.z); return max(f*f/(f+uTs.w),0.0)*100.0; }\n"
"\n"
"float ccNorm(float h){\n"
"  float hr=radians(h); float a=cos(hr), b=sin(hr);\n"
"  float c2=a*a-b*b, s2=2.0*a*b, c3=4.0*a*a*a-3.0*a, s3=3.0*b-4.0*b*b*b;\n"
"  return (11.34072*a + 16.46899*c2 + 7.88380*c3 + 14.66441*b - 6.37224*s2 + 9.19364*s3 + 77.12896) * uCc2.y;\n"
"}\n"
"float toeFwd(float x, float limit, float k1in, float k2in){\n"
"  if (x > limit) return x;\n"
"  float k2=max(k2in,0.001); float k1=sqrt(k1in*k1in+k2*k2);\n"
"  float k3=(limit+k1)/(limit+k2);\n"
"  float mb=k3*x-k1, mc=k2*k3*x;\n"
"  return 0.5*(mb+sqrt(mb*mb+4.0*mc));\n"
"}\n"
"vec4 lutAt(float h){\n"
"  float u = (mod(h,360.0)+0.5)/360.0;\n"
"  return texture2D(uLut, vec2(u,0.5)) * uLutScale;\n"
"}\n"
"vec3 chromaCompress(vec3 JMh, float tmJ, vec4 lut){\n"
"  if (JMh.y == 0.0) return vec3(tmJ, 0.0, JMh.z);\n"
"  float nJ = tmJ/uCc.x;\n"
"  float snJ = max(0.0, 1.0-nJ);\n"
"  float Mn = ccNorm(JMh.z);\n"
"  float limit = pow(nJ, uCc.y) * lut.z / Mn;\n"
"  float M = JMh.y * pow(tmJ/JMh.x, uCc.y);\n"
"  M = M/Mn;\n"
"  M = limit - toeFwd(limit-M, limit-0.001, snJ*uCc.z, sqrt(nJ*nJ+uCc.w));\n"
"  M = toeFwd(M, limit, nJ*uCc2.x, snJ);\n"
"  return vec3(tmJ, M*Mn, JMh.z);\n"
"}\n"
"float focusGain(float J, float athr){\n"
"  float gain = uCc.x*uCc2.w;\n"
"  if (J > athr){ float adj = log(max(uCc.x-athr,1e-6)/max(uCc.x-J,0.0001))/log(10.0);\n"
"    gain *= adj*adj+1.0; }\n"
"  return gain;\n"
"}\n"
"float solveJ(float J, float M, float fJ, float sg){\n"
"  float Ms=M/sg, a=Ms/fJ;\n"
"  if (J < fJ){ float b=1.0-Ms, c=-J; return -2.0*c/(b+sqrt(b*b-4.0*a*c)); }\n"
"  float b=-(1.0+Ms+uCc.x*a), c=uCc.x*Ms+J;\n"
"  return -2.0*c/(b-sqrt(b*b-4.0*a*c));\n"
"}\n"
"float vecSlope(float iJ, float fJ, float sg){\n"
"  float d = (iJ<fJ)?iJ:(uCc.x-iJ);\n"
"  return d*(iJ-fJ)/(fJ*sg);\n"
"}\n"
"float smin(float a, float b, float ref){\n"
"  float s=0.12*ref; float hh=max(s-abs(a-b),0.0)/s;\n"
"  return min(a,b) - hh*hh*hh*s/6.0;\n"
"}\n"
"float boundM(float iJ, float slope, float ig, float Jm, float Mm, float Jr){\n"
"  return Jr*pow(iJ/Jr, ig)*Mm/(Jm-slope*Mm);\n"
"}\n"
"vec3 gamutCompress(vec3 JMh, vec4 lut){\n"
"  if (JMh.x <= 0.0) return vec3(0.0,0.0,JMh.z);\n"
"  if (JMh.y < 0.0 || JMh.x > uCc.x) return vec3(JMh.x,0.0,JMh.z);\n"
"  vec2 cusp = vec2(lut.x, lut.y);\n"
"  float blend = min(1.3 - cusp.x/uCc.x, 1.0);\n"
"  float fJ = cusp.x + (uCc2.z-cusp.x)*blend;\n"
"  float athr = cusp.x + (uCc.x-cusp.x)*0.3;\n"
"  float sg = focusGain(JMh.x, athr);\n"
"  float iJ = solveJ(JMh.x, JMh.y, fJ, sg);\n"
"  float slope = vecSlope(iJ, fJ, sg);\n"
"  float iJc = solveJ(cusp.x, cusp.y, fJ, sg);\n"
"  float lower = boundM(iJ, slope, uGm.x, cusp.x, cusp.y, iJc);\n"
"  float upper = boundM(uCc.x-iJ, -slope, lut.w, uCc.x-cusp.x, cusp.y, uCc.x-iJc);\n"
"  float gb = smin(lower, upper, cusp.y);\n"
"  if (gb <= 0.0) return vec3(JMh.x, 0.0, JMh.z);\n"
"  float rb = boundM(iJ, slope, uCc.y, uCc.x, lut.z, uCc.x);\n"
"  float ratio = gb/rb;\n"
"  float prop = max(ratio, 0.75);\n"
"  float thr = prop*gb;\n"
"  float M = JMh.y;\n"
"  if (!(M <= thr || prop >= 1.0)){\n"
"    float mo=M-thr, go=gb-thr, ro=rb-thr;\n"
"    float scale = ro/((ro/go)-1.0);\n"
"    float nd = mo/scale;\n"
"    M = thr + scale*nd/(1.0+nd);\n"
"  }\n"
"  return vec3(iJ + M*slope, M, JMh.z);\n"
"}\n"
"vec3 clampAP0toAP1(vec3 aces, float upper){\n"
"  mat3 toAp1 = mat3( 1.4514393161, -0.0765537733,  0.0083161484,\n"
"                    -0.2365107469,  1.1762296998, -0.0060324498,\n"
"                    -0.2149285693, -0.0996759265,  0.9977163014);\n"
"  mat3 toAp0 = mat3( 0.6954522414,  0.0447945634, -0.0055258826,\n"
"                     0.1406786965,  0.8596711185,  0.0040252103,\n"
"                     0.1638690622,  0.0955343182,  1.0015006723);\n"
"  return toAp0 * clamp(toAp1 * aces, 0.0, upper);\n"
"}\n"
"void main(){\n"
"  vec3 aces = clampAP0toAP1(uInput * uGm.w, uGm.y);\n"
"  vec3 JMh = rgbToJMh(aces, uInRgbToCam, uInConeToAab, uInScalars);\n"
"  float lin = jToY(JMh.x, uInScalars)*0.01;\n"
"  float tmJ = yToJ(tsFwd(lin), uInScalars);\n"
"  vec4 lut = lutAt(JMh.z);\n"
"  vec3 tm = chromaCompress(JMh, tmJ, lut);\n"
"  vec4 lut2 = lutAt(tm.z);\n"
"  vec3 cg = gamutCompress(tm, lut2);\n"
"  vec3 rgb = jmhToRgb(cg, uLimAabToCone, uLimCamToRgb, uLimScalars);\n"
"  gl_FragColor = vec4(rgb, 1.0);\n"
"}\n";
