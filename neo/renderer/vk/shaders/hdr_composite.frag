#version 450

// The display transform (libretro.cpp's hdr_fs_src, as the GL renderers
// run it): the supersample resolve, chromatic aberration, bloom, the HDR
// expansion, the roll-off curves, ACES 2.0, the gamut matrix and the PQ or
// gamma encode with its dither.  The constants are vk_hdr_block.h's.

layout(location = 0) in vec2 var_TexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1, std140) uniform Hdr
{
    mat3 gamut;
    mat3 inRgbToCam;
    mat3 inConeToAab;
    mat3 limAabToCone;
    mat3 limCamToRgb;
    vec4 parms;           // paperWhite/10000, headroom H, curve mode, Reinhard knee
    vec4 gt;              // GT: toe m, S0, 1-S1, C2
    vec4 inScalars;       // F_L_n, cz, inv_cz, A_w_J
    vec4 limScalars;
    vec4 ts;              // m_2, s_2, g, t_1
    vec4 cc;
    vec4 cc2;             // compr, ccScale, mid_J, focus_dist
    vec4 gm;              // lower_hull_gamma_inv, forward_limit, lut half texel, exposure
    vec4 lutScale;        // cuspJ, cuspM, reachM, gammaInv
    vec4 halBloom;        // xyz halation, w bloom amount
    vec4 ssaaCa;          // xyz supersample offsets and curve select, w chromatic aberration
    vec4 outEncBandW;     // x 1 PQ / 0 gamma 2.4, y dither LSB, zw band weights
    vec4 expandEncFrame;  // xy expansion mode and knee, z 1/encode scale, w frame
    vec4 filmDesat;       // xyz filmic terms, w highlight desaturation
    vec4 aces;            // shoulder A, gradient, GT normalisation
} u_hdr;

layout(set = 0, binding = 2) uniform sampler2D u_scene;
layout(set = 0, binding = 3) uniform sampler2D u_bloomT;
layout(set = 0, binding = 4) uniform sampler2D u_bloomW;
layout(set = 0, binding = 5) uniform sampler2D u_lut;
layout(set = 0, binding = 6) uniform sampler2D u_filmLut;
layout(set = 0, binding = 7) uniform sampler2D u_filmDesat;
vec3 srgbToLinear(vec3 c) {
  return pow(abs(c), vec3(2.4));
}
float shoulder(float v, float base) {
  if (v <= 1.0) return base;
  float e = v - 1.0;
  return 1.0 + e * u_hdr.aces.x * u_hdr.aces.y / (e + u_hdr.aces.x);
}

float coneFwd(float v){ float a=abs(v); float f=pow(a,0.42); float r=f/(27.13+f); return v<0.0?-r:r; }
float coneInv(float v){ float a=min(abs(v),0.99); float f=(27.13*a)/(1.0-a); float r=pow(f,1.0/0.42); return v<0.0?-r:r; }

vec3 rgbToJMh(vec3 rgb, mat3 toCam, mat3 coneToAab, vec4 sc){
  vec3 m = toCam * rgb;
  vec3 a = vec3(coneFwd(m.x), coneFwd(m.y), coneFwd(m.z));
  vec3 Aab = coneToAab * a;
  if (Aab.x <= 0.0) return vec3(0.0);
  float J = 100.0 * pow(Aab.x, sc.y);
  float M = length(Aab.yz);
  float h = degrees(atan(Aab.z, Aab.y));
  if (h < 0.0) h += 360.0;
  return vec3(J, M, h);
}
vec3 jmhToRgb(vec3 JMh, mat3 aabToCone, mat3 toRgb, vec4 sc){
  float hr = radians(JMh.z);
  vec3 Aab = vec3(pow(JMh.x*0.01, sc.z), JMh.y*cos(hr), JMh.y*sin(hr));
  vec3 a = aabToCone * Aab;
  vec3 m = vec3(coneInv(a.x), coneInv(a.y), coneInv(a.z));
  return toRgb * m;
}
float jToY(float J, vec4 sc){ float A=pow(abs(J)*0.01, sc.z); return coneInv(sc.w*A)/sc.x; }
float yToJ(float Y, vec4 sc){ float Ra=coneFwd(abs(Y)*sc.x); float J=100.0*pow(Ra/sc.w, sc.y); return Y<0.0?-J:J; }

float tsFwd(float x){ float f=u_hdr.ts.x*pow(max(x,0.0)/(x+u_hdr.ts.y), u_hdr.ts.z); return max(f*f/(f+u_hdr.ts.w),0.0)*100.0; }

float ccNorm(float h){
  float hr=radians(h); float a=cos(hr), b=sin(hr);
  float c2=a*a-b*b, s2=2.0*a*b, c3=4.0*a*a*a-3.0*a, s3=3.0*b-4.0*b*b*b;
  return (11.34072*a + 16.46899*c2 + 7.88380*c3 + 14.66441*b - 6.37224*s2 + 9.19364*s3 + 77.12896) * u_hdr.cc2.y;
}
float toeFwd(float x, float limit, float k1in, float k2in){
  if (x > limit) return x;
  float k2=max(k2in,0.001); float k1=sqrt(k1in*k1in+k2*k2);
  float k3=(limit+k1)/(limit+k2);
  float mb=k3*x-k1, mc=k2*k3*x;
  return 0.5*(mb+sqrt(mb*mb+4.0*mc));
}
vec4 lutAt(float h){
  float u = (mod(h,360.0)+0.5)/360.0;
  return texture(u_lut, vec2(u,0.5)) * u_hdr.lutScale;
}
vec3 chromaCompress(vec3 JMh, float tmJ, vec4 lut){
  if (JMh.y == 0.0) return vec3(tmJ, 0.0, JMh.z);
  float nJ = tmJ/u_hdr.cc.x;
  float snJ = max(0.0, 1.0-nJ);
  float Mn = ccNorm(JMh.z);
  float limit = pow(nJ, u_hdr.cc.y) * lut.z / Mn;
  float M = JMh.y * pow(tmJ/JMh.x, u_hdr.cc.y);
  M = M/Mn;
  M = limit - toeFwd(limit-M, limit-0.001, snJ*u_hdr.cc.z, sqrt(nJ*nJ+u_hdr.cc.w));
  M = toeFwd(M, limit, nJ*u_hdr.cc2.x, snJ);
  return vec3(tmJ, M*Mn, JMh.z);
}
float focusGain(float J, float athr){
  float gain = u_hdr.cc.x*u_hdr.cc2.w;
  if (J > athr){ float adj = log(max(u_hdr.cc.x-athr,1e-6)/max(u_hdr.cc.x-J,0.0001))/log(10.0);
    gain *= adj*adj+1.0; }
  return gain;
}
float solveJ(float J, float M, float fJ, float sg){
  float Ms=M/sg, a=Ms/fJ;
  if (J < fJ){ float b=1.0-Ms, c=-J; return -2.0*c/(b+sqrt(b*b-4.0*a*c)); }
  float b=-(1.0+Ms+u_hdr.cc.x*a), c=u_hdr.cc.x*Ms+J;
  return -2.0*c/(b-sqrt(b*b-4.0*a*c));
}
float vecSlope(float iJ, float fJ, float sg){
  float d = (iJ<fJ)?iJ:(u_hdr.cc.x-iJ);
  return d*(iJ-fJ)/(fJ*sg);
}
float smin(float a, float b, float ref){
  float s=0.12*ref; float hh=max(s-abs(a-b),0.0)/s;
  return min(a,b) - hh*hh*hh*s/6.0;
}
float boundM(float iJ, float slope, float ig, float Jm, float Mm, float Jr){
  return Jr*pow(iJ/Jr, ig)*Mm/(Jm-slope*Mm);
}
vec3 gamutCompress(vec3 JMh, vec4 lut){
  if (JMh.x <= 0.0) return vec3(0.0,0.0,JMh.z);
  if (JMh.y < 0.0 || JMh.x > u_hdr.cc.x) return vec3(JMh.x,0.0,JMh.z);
  vec2 cusp = vec2(lut.x, lut.y);
  float blend = min(1.3 - cusp.x/u_hdr.cc.x, 1.0);
  float fJ = cusp.x + (u_hdr.cc2.z-cusp.x)*blend;
  float athr = cusp.x + (u_hdr.cc.x-cusp.x)*0.3;
  float sg = focusGain(JMh.x, athr);
  float iJ = solveJ(JMh.x, JMh.y, fJ, sg);
  float slope = vecSlope(iJ, fJ, sg);
  float iJc = solveJ(cusp.x, cusp.y, fJ, sg);
  float lower = boundM(iJ, slope, u_hdr.gm.x, cusp.x, cusp.y, iJc);
  float upper = boundM(u_hdr.cc.x-iJ, -slope, lut.w, u_hdr.cc.x-cusp.x, cusp.y, u_hdr.cc.x-iJc);
  float gb = smin(lower, upper, cusp.y);
  if (gb <= 0.0) return vec3(JMh.x, 0.0, JMh.z);
  float rb = boundM(iJ, slope, u_hdr.cc.y, u_hdr.cc.x, lut.z, u_hdr.cc.x);
  float ratio = gb/rb;
  float prop = max(ratio, 0.75);
  float thr = prop*gb;
  float M = JMh.y;
  if (!(M <= thr || prop >= 1.0)){
    float mo=M-thr, go=gb-thr, ro=rb-thr;
    float scale = ro/((ro/go)-1.0);
    float nd = mo/scale;
    M = thr + scale*nd/(1.0+nd);
  }
  return vec3(iJ + M*slope, M, JMh.z);
}
vec3 clampAP0toAP1(vec3 aces, float upper){
  mat3 toAp1 = mat3( 1.4514393161, -0.0765537733,  0.0083161484,
                    -0.2365107469,  1.1762296998, -0.0060324498,
                    -0.2149285693, -0.0996759265,  0.9977163014);
  mat3 toAp0 = mat3( 0.6954522414,  0.0447945634, -0.0055258826,
                     0.1406786965,  0.8596711185,  0.0040252103,
                     0.1638690622,  0.0955343182,  1.0015006723);
  return toAp0 * clamp(toAp1 * aces, 0.0, upper);
}
vec3 aces2Transform(vec3 sceneRGB) {
  vec3 aces = clampAP0toAP1(sceneRGB * u_hdr.gm.w, u_hdr.gm.y);
  vec3 JMh = rgbToJMh(aces, u_hdr.inRgbToCam, u_hdr.inConeToAab, u_hdr.inScalars);
  float lin = jToY(JMh.x, u_hdr.inScalars)*0.01;
  float tmJ = yToJ(tsFwd(lin), u_hdr.inScalars);
  vec4 lut = lutAt(JMh.z);
  vec3 tm = chromaCompress(JMh, tmJ, lut);
  vec3 cg = gamutCompress(tm, lutAt(tm.z));
  return jmhToRgb(cg, u_hdr.limAabToCone, u_hdr.limCamToRgb, u_hdr.limScalars);
}
vec3 acesFitRRT(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
vec3 rolloffRGB(vec3 c) {
  if (u_hdr.parms.z > 22.5) {
    return aces2Transform(c);
  }
  if (u_hdr.parms.z > 21.5) {
    mat3 mi = mat3(0.842479062253094, 0.0784335999999992, 0.0792237451477643,
                   0.0423282422610123, 0.878468636469772, 0.0791661274605434,
                   0.0423756549057051, 0.0784336, 0.879142973793104);
    mat3 mo = mat3(1.19687900512017, -0.0980208811401368, -0.0990297440797205,
                   -0.0528968517574562, 1.15190312990417, -0.0989611768448433,
                   -0.0529716355144438, -0.0980434501171241, 1.15107367264116);
    vec3 v = mi * max(c, vec3(0.0));
    v = clamp((log2(max(v, vec3(1e-10))) + 12.47393) / 16.500009, 0.0, 1.0);
    vec3 v2 = v * v, v4 = v2 * v2;
    v = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4
      - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v - 0.00232;
    v = mo * v;
    return pow(max(v, vec3(0.0)), vec3(2.2)) * 1.4491792;
  }
  if (u_hdr.parms.z > 20.5) {
    float sc = 0.76, des = 0.15;
    float x = min(c.r, min(c.g, c.b));
    float off = x < 0.08 ? x - 6.25 * x * x : 0.04;
    vec3 v = c - off;
    float peak = max(v.r, max(v.g, v.b));
    if (peak < sc) return v * 1.1506276;
    float d = 1.0 - sc;
    float np = 1.0 - d * d / (peak + d - sc);
    v *= np / peak;
    float g = 1.0 - 1.0 / (des * (peak - np) + 1.0);
    return mix(v, vec3(np), g) * 1.1506276;
  }
  if (u_hdr.parms.z < 18.5) {
    vec3 t = clamp((log2(max(c, vec3(1e-10))) + 12.4739312) * 0.04, 0.0, 1.0);
    vec3 d = t * 32.0;
    float bz = floor(d.z), fz = d.z - bz;
    float dx = min(d.x, 31.999);
    vec2 uv = vec2((bz * 33.0 + dx + 0.5) / 1089.0, (d.y + 0.5) / 33.0);
    vec3 s0 = texture(u_filmDesat, uv).rgb;
    vec3 s1 = texture(u_filmDesat, uv + vec2(33.0 / 1089.0, 0.0)).rgb;
    t = clamp(mix(s0, s1, fz) * u_hdr.filmDesat.xyz.x, 0.0, 1.0);
    vec3 o;
    o.r = texture(u_filmLut, vec2(t.r, 0.5)).r;
    o.g = texture(u_filmLut, vec2(t.g, 0.5)).r;
    o.b = texture(u_filmLut, vec2(t.b, 0.5)).r;
    return pow(max(o, vec3(0.0)), vec3(2.2)) * u_hdr.filmDesat.xyz.z;
  }
  if (u_hdr.parms.z > 19.5) {
    mat3 mi = mat3(0.59719, 0.07600, 0.02840,
                   0.35458, 0.90834, 0.13383,
                   0.04823, 0.01566, 0.83777);
    mat3 mo = mat3( 1.60475, -0.10208, -0.00327,
                   -0.53108,  1.10813, -0.07276,
                   -0.07367, -0.00605,  1.07602);
    vec3 v = mo * acesFitRRT(mi * c);
    return max(v, vec3(0.0)) * 1.6152077;
  }
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  vec3 tv = c / (1.0 + c);
  return mix(c / (1.0 + l), tv, tv) * 2.0;
}
float rolloff(float v) {
  float H = u_hdr.parms.y;
  if (u_hdr.parms.z > 6.5 && u_hdr.parms.z < 7.5) {
    float n = v / (v + 0.155) * 1.019;
    return shoulder(v, pow(n, 2.2) * 1.3173380);
  }
  if (u_hdr.parms.z > 5.5 && u_hdr.parms.z < 6.5) {
    float n = log(1.0 + max(v, 0.0)) / log(2.0 + 8.0 * pow(max(v, 0.0) / 8.0, 0.2344653));
    return shoulder(v, n * (1.0 / 0.9542425) * 2.6616800);
  }
  if (u_hdr.parms.z > 4.5 && u_hdr.parms.z < 5.5) {
    float p = pow(max(v, 0.0), 1.6);
    float q = pow(max(v, 0.0), 1.5632);
    return shoulder(v, (p / (q * 0.1674199 + 1.0730397)) * 1.2404596);
  }
  if (u_hdr.parms.z > 3.5 && u_hdr.parms.z < 4.5) {
    float n = ((v * (0.15 * v + 0.05) + 0.004) / (v * (0.15 * v + 0.50) + 0.06)) - 0.0666667;
    return shoulder(v, n * 4.5319149);
  }
  if (u_hdr.parms.z > 16.5 && u_hdr.parms.z < 17.5) {
    float x = max(v, 0.0);
    return shoulder(v, (x / (x + 0.3010864)) * 1.3010864);
  }
  if (u_hdr.parms.z > 15.5 && u_hdr.parms.z < 16.5) {
    float x = max(v, 0.0);
    return shoulder(v, 2.3 * x / (2.3 * x - x + 1.0));
  }
  if (u_hdr.parms.z > 14.5 && u_hdr.parms.z < 15.5) {
    return shoulder(v, max(v, 0.0));
  }
  if (u_hdr.parms.z > 13.5 && u_hdr.parms.z < 14.5) {
    return shoulder(v, pow(max(v, 0.0), 0.6075));
  }
  if (u_hdr.parms.z > 12.5 && u_hdr.parms.z < 13.5) {
    float x = max(v, 0.0);
    float f = 1.0471038 * pow(x / (x + 0.9198583), 1.15);
    return shoulder(v, (f * f / (f + 0.04)) * 2.1854781);
  }
  if (u_hdr.parms.z > 11.5 && u_hdr.parms.z < 12.5) {
    float x = max(v, 0.0);
    float f;
    if (x < 0.25) f = 1.1313708 * pow(x, 1.25);
    else if (x < 0.70) f = 0.20 + (x - 0.25);
    else f = 0.65 + 0.35 * (x - 0.70) / ((x - 0.70) + 0.35);
    return shoulder(v, f * 1.2322275);
  }
  if (u_hdr.parms.z > 10.5 && u_hdr.parms.z < 11.5) {
    return shoulder(v, (1.0 - exp(-max(v, 0.0))) * 1.5819767);
  }
  if (u_hdr.parms.z > 9.5 && u_hdr.parms.z < 10.5) {
    float x = max(v, 0.0);
    return shoulder(v, (x * (1.0 + x / 16.0) / (1.0 + x)) * 1.8823529);
  }
  if (u_hdr.parms.z > 8.5 && u_hdr.parms.z < 9.5) {
    float x = max(v, 0.0);
    return shoulder(v, (x / (1.0 + x)) * 2.0);
  }
  if (u_hdr.parms.z > 7.5 && u_hdr.parms.z < 8.5) {
    float x = max(v, 0.0);
    float va = 1.425 * x + 0.05;
    float f = ((x * va + 0.004) / ((x * va + 0.055) + 0.0491)) - 0.0821;
    return shoulder(v, pow(max(f, 0.0), 2.2) * 1.4132626);
  }
  if (u_hdr.parms.z > 2.5) {
    float t = clamp(v / u_hdr.gt.x, 0.0, 1.0);
    float w0 = 1.0 - (t * t * (3.0 - 2.0 * t));
    float w2 = step(u_hdr.gt.y, v);
    float w1 = 1.0 - w0 - w2;
    float T = u_hdr.gt.x * pow(max(v, 0.0) / u_hdr.gt.x, 1.33);
    float S = 1.0 - u_hdr.gt.z * exp(-u_hdr.gt.w * (v - u_hdr.gt.y));
    return (T * w0 + v * w1 + S * w2) * u_hdr.aces.z;
  }
  if (u_hdr.parms.z > 1.5) {
    float x = max(v - 0.004, 0.0);
    float n = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    return shoulder(v, pow(n, 2.2) * 1.4629683);
  }
  if (u_hdr.parms.z > 0.5) {
    float n = v * (2.51 * v + 0.03) / (v * (2.43 * v + 0.59) + 0.14);
    return shoulder(v, n * 1.2440945);
  }
  float K = u_hdr.parms.w;
  if (v <= K) return v;
  float e = v - K, A = H - K;
  return K + e * A / (e + A);
}
vec3 expandCurve(vec3 v, float E, float K) {
  vec3 t = clamp((v - K) / max(1.0 - K, 1e-4), 0.0, 1.0);
  vec3 hi = K + (1.0 - K) * t + (E - 1.0) * t * t + max(v - 1.0, 0.0);
  return mix(v, hi, step(K, v));
}
vec3 expand(vec3 c) {
  float E = u_hdr.parms.y;
  if (u_hdr.expandEncFrame.xy.x < 0.5 || E <= 1.0001) return c;
  float K = u_hdr.expandEncFrame.xy.y;
  if (u_hdr.expandEncFrame.xy.x < 1.5) return expandCurve(c, E, K);
  float p = max(c.r, max(c.g, c.b));
  if (p <= 1e-5) return c;
  return c * (expandCurve(vec3(p), E, K).x / p);
}
float ign(vec2 p) {
  return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}
float pq(float y) {
  float p = pow(max(y, 0.0), 0.1593017578125);
  return pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), 78.84375);
}
void main() {
  vec2 caD = var_TexCoord - 0.5;
  float caR = dot(caD, caD) * u_hdr.ssaaCa.w;
  vec3 base;
  if (u_hdr.ssaaCa.xyz.x > 0.0) {
    vec2 ssOff = u_hdr.ssaaCa.xyz.xy;
    vec3 acc = vec3(0.0); float wsum = 0.0;
    for (int ky = -1; ky <= 1; ky += 2) {
      for (int kx = -1; kx <= 1; kx += 2) {
        vec3 c = texture(u_scene, var_TexCoord + vec2(float(kx), float(ky)) * ssOff).rgb;
        if (u_hdr.ssaaCa.xyz.z > 2.5) {
          float pk = max(c.r, max(c.g, c.b));
          float w = min(1.0, u_hdr.gt.z * u_hdr.gt.w * exp2(-1.442695 * u_hdr.gt.w * max(pk - u_hdr.gt.y, 0.0)));
          w = (pk >= u_hdr.gt.y) ? w : 1.0;
          acc += c * w; wsum += w;
        } else if (u_hdr.ssaaCa.xyz.z > 1.5) {
          float pk = max(c.r, max(c.g, c.b));
          float w = 0.24 / max(pk - 0.52, 0.24); w *= w;
          acc += c * w; wsum += w;
        } else if (u_hdr.ssaaCa.xyz.z > 0.5) {
          acc += log2(c + 0.0001);
        } else {
          float w = 1.0 / (1.0 + max(c.r, max(c.g, c.b)));
          acc += c * w; wsum += w;
        }
      }
    }
    base = (u_hdr.ssaaCa.xyz.z > 0.5 && u_hdr.ssaaCa.xyz.z < 1.5)
        ? max(exp2(acc * 0.25) - 0.0001, vec3(0.0))
        : acc / wsum;
  } else {
    base = texture(u_scene, var_TexCoord).rgb;
  }
  vec3 scn = base;
  if (caR != 0.0) {
    scn.r = texture(u_scene, var_TexCoord - caD * caR).r;
    scn.b = texture(u_scene, var_TexCoord + caD * caR).b;
  }
  vec3 lin = srgbToLinear(scn * u_hdr.expandEncFrame.z);
  vec3 bl = u_hdr.outEncBandW.zw.x * texture(u_bloomT, var_TexCoord).rgb;
  if (u_hdr.outEncBandW.zw.y > 0.0)
    bl += u_hdr.outEncBandW.zw.y * texture(u_bloomW, var_TexCoord).rgb;
  lin += (u_hdr.halBloom.w * u_hdr.halBloom.xyz) * bl;
  lin = expand(lin);
  float hlP = max(lin.r, max(lin.g, lin.b));
  if (u_hdr.parms.z > 17.5) lin = rolloffRGB(lin);
  else lin = vec3(rolloff(lin.r), rolloff(lin.g), rolloff(lin.b));
  if (u_hdr.filmDesat.w > 0.5) {
    float hlq = max(lin.r, max(lin.g, lin.b));
    float hlg = 1.0 - 1.0 / (0.15 * max(hlP - hlq, 0.0) + 1.0);
    lin = mix(lin, vec3(hlq), hlg);
  }
  lin = u_hdr.gamut * lin;
  vec3 y = clamp(lin * u_hdr.parms.x, 0.0, 1.0);
  vec3 e = (u_hdr.outEncBandW.xy.x > 0.5)
      ? vec3(pq(y.r), pq(y.g), pq(y.b))
      : pow(y, vec3(1.0 / 2.4));
  float n = ign(gl_FragCoord.xy);
  float k = u_hdr.expandEncFrame.w * 3.0;
  vec3 d = vec3(fract(n + (k + 0.0) * 0.6180339887),
                fract(n + (k + 1.0) * 0.6180339887),
                fract(n + (k + 2.0) * 0.6180339887)) - 0.5;
  fragColor = vec4(clamp(e + d * u_hdr.outEncBandW.xy.y, 0.0, 1.0), 1.0);
}
