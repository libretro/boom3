/*
 * glsl_translate: the GLSL renderers' GLSL ES shaders for desktop GL
 * contexts.
 *
 * C89.
 */
#ifndef BOOM3_GLSL_TRANSLATE_H
#define BOOM3_GLSL_TRANSLATE_H

#ifdef __cplusplus
extern "C" {
#endif

enum glsl_target {
   GLSL_TARGET_ES = 0,      /* a GLES context: the source unchanged */
   GLSL_TARGET_COMPAT,      /* desktop GL 2.1 and later compatibility: GLSL 1.20 */
   GLSL_TARGET_CORE         /* desktop GL 3.3 and later core: GLSL 3.30 */
};

enum glsl_stage {
   GLSL_STAGE_VERTEX = 0,
   GLSL_STAGE_FRAGMENT
};

/* src, GLSL ES 1.00 (with or without "#version 100") or GLSL ES 3.00
 * ("#version 300 es"), as GLSL the target compiles to the same program:
 *
 *  - the #version line becomes "#version 120" or "#version 330 core";
 *  - for GLSL 1.20, precision statements and qualifiers are dropped
 *    (1.20 has neither; desktop float is at least highp);
 *  - an ES 1.00 shader for GLSL 3.30 takes the 3.30 names: attribute and
 *    varying become in and out, texture2D, textureCube and texture2DProj
 *    become texture and textureProj, and gl_FragColor becomes an output
 *    declared after the #version line;
 *  - #extension GL_OES_standard_derivatives lines go (core in both).
 *
 * Returns a malloc'd string, NULL when out of memory or when an ES 3.00
 * shader is asked for GLSL 1.20, which cannot express it. */
char *glsl_translate(const char *src, int stage, int target);

#ifdef __cplusplus
}
#endif

#endif
