/*
 * glsl_blocks: moves a shader's plain uniform declarations into a std140
 * uniform block, for the ES 3.1 renderer's per-draw blocks.
 *
 * C89.
 */
#ifndef BOOM3_GLSL_BLOCKS_H
#define BOOM3_GLSL_BLOCKS_H

#ifdef __cplusplus
extern "C" {
#endif

/* src with the declarations of the named uniforms moved into
 * "layout(std140) uniform <block> { ... };", put where the first of them
 * was.  Each declaration line moves as it is - precision, comment and all -
 * so the code that reads them is unchanged.  A uniform is found on a line
 * of its own that starts with "uniform ".  Returns a malloc'd string (src
 * copied when none of the names is declared), NULL when out of memory. */
char *glsl_uniforms_to_block(const char *src, const char *block,
      const char *const *names, int count);

#ifdef __cplusplus
}
#endif

#endif
