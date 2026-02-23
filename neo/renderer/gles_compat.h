/*
===========================================================================
neo/renderer/gles_compat.h

GLES2 compatibility shim for boom3 / Doom 3.
===========================================================================
*/

#ifndef __GLES_COMPAT_H__
#define __GLES_COMPAT_H__

#ifdef HAVE_OPENGLES

// ---------------------------------------------------------------------------
// qgl* → gl* function mapping
// ---------------------------------------------------------------------------

#define qglEnable                   glEnable
#define qglDisable                  glDisable
#define qglIsEnabled                glIsEnabled

#define qglBlendFunc                glBlendFunc
#define qglBlendFuncSeparate        glBlendFuncSeparate
#define qglBlendEquation            glBlendEquation
#define qglDepthFunc                glDepthFunc
#define qglDepthMask                glDepthMask
#define qglDepthRangef              glDepthRangef
#define qglStencilFunc              glStencilFunc
#define qglStencilOp                glStencilOp
#define qglStencilMask              glStencilMask

#define qglCullFace                 glCullFace
#define qglFrontFace                glFrontFace
#define qglPolygonOffset            glPolygonOffset
#define qglLineWidth                glLineWidth
#define qglScissor                  glScissor
#define qglViewport                 glViewport
#define qglColorMask                glColorMask

#define qglClear                    glClear
#define qglClearColor               glClearColor
#define qglClearDepthf              glClearDepthf
#define qglClearStencil             glClearStencil

#define qglGenTextures              glGenTextures
#define qglDeleteTextures           glDeleteTextures
#define qglBindTexture              glBindTexture
#define qglActiveTexture            glActiveTexture
#define qglTexImage2D               glTexImage2D
#define qglTexSubImage2D            glTexSubImage2D
#define qglCopyTexImage2D           glCopyTexImage2D
#define qglCopyTexSubImage2D        glCopyTexSubImage2D
#define qglCompressedTexImage2D     glCompressedTexImage2D
#define qglTexParameterf            glTexParameterf
#define qglTexParameteri            glTexParameteri
#define qglTexParameterfv           glTexParameterfv
#define qglTexParameteriv           glTexParameteriv
#define qglGetTexParameterfv        glGetTexParameterfv
#define qglGetTexParameteriv        glGetTexParameteriv
#define qglPixelStorei              glPixelStorei

#define qglGenBuffers               glGenBuffers
#define qglDeleteBuffers            glDeleteBuffers
#define qglBindBuffer               glBindBuffer
#define qglBufferData               glBufferData
#define qglBufferSubData            glBufferSubData
// ARB suffix → core GLES2
#define qglBindBufferARB            glBindBuffer
#define qglDeleteBuffersARB         glDeleteBuffers
#define qglGenBuffersARB            glGenBuffers
#define qglBufferDataARB            glBufferData
#define qglBufferSubDataARB         glBufferSubData

#define qglGenFramebuffers          glGenFramebuffers
#define qglDeleteFramebuffers       glDeleteFramebuffers
#define qglBindFramebuffer          glBindFramebuffer
#define qglFramebufferTexture2D     glFramebufferTexture2D
#define qglGenRenderbuffers         glGenRenderbuffers
#define qglDeleteRenderbuffers      glDeleteRenderbuffers
#define qglBindRenderbuffer         glBindRenderbuffer
#define qglRenderbufferStorage      glRenderbufferStorage
#define qglFramebufferRenderbuffer  glFramebufferRenderbuffer
#define qglCheckFramebufferStatus   glCheckFramebufferStatus

#define qglCreateShader             glCreateShader
#define qglDeleteShader             glDeleteShader
#define qglShaderSource             glShaderSource
#define qglCompileShader            glCompileShader
#define qglGetShaderiv              glGetShaderiv
#define qglGetShaderInfoLog         glGetShaderInfoLog
#define qglCreateProgram            glCreateProgram
#define qglDeleteProgram            glDeleteProgram
#define qglAttachShader             glAttachShader
#define qglDetachShader             glDetachShader
#define qglLinkProgram              glLinkProgram
#define qglUseProgram               glUseProgram
#define qglGetProgramiv             glGetProgramiv
#define qglGetProgramInfoLog        glGetProgramInfoLog
#define qglGetUniformLocation       glGetUniformLocation
#define qglGetAttribLocation        glGetAttribLocation
#define qglBindAttribLocation       glBindAttribLocation
#define qglUniform1i                glUniform1i
#define qglUniform1f                glUniform1f
#define qglUniform2f                glUniform2f
#define qglUniform3f                glUniform3f
#define qglUniform4f                glUniform4f
#define qglUniform1fv               glUniform1fv
#define qglUniform2fv               glUniform2fv
#define qglUniform3fv               glUniform3fv
#define qglUniform4fv               glUniform4fv
#define qglUniformMatrix4fv         glUniformMatrix4fv

#define qglVertexAttribPointer          glVertexAttribPointer
#define qglEnableVertexAttribArray      glEnableVertexAttribArray
#define qglDisableVertexAttribArray     glDisableVertexAttribArray
#define qglVertexAttribPointerARB       glVertexAttribPointer
#define qglEnableVertexAttribArrayARB   glEnableVertexAttribArray
#define qglDisableVertexAttribArrayARB  glDisableVertexAttribArray

#define qglDrawArrays               glDrawArrays
#define qglDrawElements             glDrawElements

#define qglGetError                 glGetError
#define qglGetIntegerv              glGetIntegerv
#define qglGetFloatv                glGetFloatv
#define qglGetString                glGetString
#define qglFinish                   glFinish
#define qglFlush                    glFlush
#define qglReadPixels               glReadPixels

// Compressed textures are core in GLES2 (no ARB suffix needed)
#define qglCompressedTexImage2DARB  glCompressedTexImage2D

// Double → float wrappers
#ifndef qglDepthRange
#  define qglDepthRange(n,f)  glDepthRangef((GLclampf)(n), (GLclampf)(f))
#endif
#ifndef qglClearDepth
#  define qglClearDepth(d)    glClearDepthf((GLclampf)(d))
#endif

// ---------------------------------------------------------------------------
// Functions with NO GLES2 equivalent – empty stubs.
// Call sites in WritePrecompressedImage / Generate3DImage are wrapped in
// #ifndef HAVE_OPENGLES in Image_load.patch; these stubs catch anything
// that slips through.
// ---------------------------------------------------------------------------

// Texture readback – not available in GLES2
static inline void qglGetTexImage(GLenum, GLint, GLenum, GLenum, GLvoid*) {}
static inline void qglGetCompressedTexImageARB(GLenum, GLint, GLvoid*) {}

// Read-buffer selection – GLES2 implicitly reads the back buffer
static inline void qglReadBuffer(GLenum) {}

// Texture priority hint – driver hint, safe to ignore
static inline void qglPrioritizeTextures(GLsizei, const GLuint*, const GLclampf*) {}

// Palette colour table – no GLES2 equivalent
static inline void qglColorTableEXT(GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid*) {}

// 3D textures – OES extension; Generate3DImage is skipped on GLES2
static inline void qglTexImage3D(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei,
                                 GLint, GLenum, GLenum, const GLvoid*) {}

// ---------------------------------------------------------------------------
// Desktop-only GL enums – REAL GL numeric values, NOT GLES2 aliases.
//
// WHY: These appear as switch/case labels.  If we alias them to existing
// GLES2 tokens (e.g. GL_RGBA8 → GL_RGBA) two distinct desktop cases would
// map to the same integer, producing "duplicate case value" compile errors.
//
// We use the real values so each case label is unique.  The only place these
// values must be translated to GLES2-legal ones is at actual glTexImage2D /
// glCopyTexImage2D call sites – use GLES2_INTERNAL_FMT() there.
// ---------------------------------------------------------------------------

// Sized internal formats (absent from GLES2 headers)
#ifndef GL_ALPHA8
#  define GL_ALPHA8             0x803C
#endif
#ifndef GL_LUMINANCE8
#  define GL_LUMINANCE8         0x8040
#endif
#ifndef GL_INTENSITY8
#  define GL_INTENSITY8         0x804B
#endif
#ifndef GL_LUMINANCE8_ALPHA8
#  define GL_LUMINANCE8_ALPHA8  0x8045
#endif
#ifndef GL_RGB5
#  define GL_RGB5               0x8050
#endif
#ifndef GL_RGB8
#  define GL_RGB8               0x8051
#endif
#ifndef GL_RGBA8
#  define GL_RGBA8              0x8058
#endif

// Color-index formats
#ifndef GL_COLOR_INDEX
#  define GL_COLOR_INDEX        0x1900
#endif
#ifndef GL_COLOR_INDEX8_EXT
#  define GL_COLOR_INDEX8_EXT   0x80E5
#endif

// Generic ARB driver-chosen compression
#ifndef GL_COMPRESSED_RGB_ARB
#  define GL_COMPRESSED_RGB_ARB  0x84ED
#endif
#ifndef GL_COMPRESSED_RGBA_ARB
#  define GL_COMPRESSED_RGBA_ARB 0x84EE
#endif

// S3TC DXT formats (may exist via GL_EXT_texture_compression_s3tc on GLES2)
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#  define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#  define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#  define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#  define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// Cube map EXT suffix
#ifndef GL_TEXTURE_CUBE_MAP_EXT
#  define GL_TEXTURE_CUBE_MAP_EXT           GL_TEXTURE_CUBE_MAP
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT
#  define GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT GL_TEXTURE_CUBE_MAP_POSITIVE_X
#endif

// Border clamp: GLES2 has no border colour; fall back to edge clamp
#ifndef GL_CLAMP_TO_BORDER
#  define GL_CLAMP_TO_BORDER    GL_CLAMP_TO_EDGE
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#  define GL_TEXTURE_BORDER_COLOR 0x1004   // call site guarded; value never reaches driver
#endif

// LOD bias: not in core GLES2 (call site guarded)
#ifndef GL_TEXTURE_LOD_BIAS_EXT
#  define GL_TEXTURE_LOD_BIAS_EXT 0x8501
#endif

// Texture wrap R: only in GLES3 / OES_texture_3D (Generate3DImage is skipped)
#ifndef GL_TEXTURE_WRAP_R
#  define GL_TEXTURE_WRAP_R     0x8072
#endif

// Rectangle textures: NV extension, not used on GLES2 paths
#ifndef GL_TEXTURE_RECTANGLE_NV
#  define GL_TEXTURE_RECTANGLE_NV GL_TEXTURE_2D
#endif

// 3D textures: OES extension numeric value
#ifndef GL_TEXTURE_3D
#  define GL_TEXTURE_3D         0x806F
#endif

// Shared palette: absent in GLES2
#ifndef GL_SHARED_TEXTURE_PALETTE_EXT
#  define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB
#endif

// Anisotropy: GL_EXT_texture_filter_anisotropic (common on GLES2 targets)
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#  define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

// BGR/BGRA: not in core GLES2.  WritePrecompressedImage read-back is guarded.
#ifndef GL_BGR_EXT
#  define GL_BGR_EXT   0x80E0
#endif
#ifndef GL_BGRA_EXT
#  define GL_BGRA_EXT  0x80E1
#endif

// ---------------------------------------------------------------------------
// ARB vertex/fragment program enums
//
// GL_ARB_vertex_program and GL_ARB_fragment_program don't exist in GLES2.
// The GLES renderer uses GLSL via newShaderStage_t, not the ARB program path.
// Define the target enums with their real GL values so Material.cpp compiles.
// R_FindARBProgram() is stubbed below to return 0 on GLES2 builds, which
// means newStage.vertexProgram / fragmentProgram will be 0, and the ARB
// drawing path in RB_STD_T_RenderShaderPasses is skipped at runtime via
// the `if ( pStage->newStage )` null check.
// ---------------------------------------------------------------------------
#ifndef GL_VERTEX_PROGRAM_ARB
#  define GL_VERTEX_PROGRAM_ARB    0x8620
#endif
#ifndef GL_FRAGMENT_PROGRAM_ARB
#  define GL_FRAGMENT_PROGRAM_ARB  0x8804
#endif
#ifndef GL_VERTEX_PROGRAM_NV
#  define GL_VERTEX_PROGRAM_NV     0x8620
#endif
#ifndef GL_FRAGMENT_PROGRAM_NV
#  define GL_FRAGMENT_PROGRAM_NV   0x8870
#endif

// ---------------------------------------------------------------------------
// GL_STACK_OVERFLOW / GL_STACK_UNDERFLOW: removed from core in GL 3.2+
// and never present in GLES2. Use their real GL 1.x values so the
// switch/case labels in GL_CheckErrors() compile with unique integers.
// ---------------------------------------------------------------------------
#ifndef GL_STACK_OVERFLOW
#  define GL_STACK_OVERFLOW   0x0503
#endif
#ifndef GL_STACK_UNDERFLOW
#  define GL_STACK_UNDERFLOW  0x0504
#endif

// GL_STENCIL_INDEX: the base enum exists in GLES2 (0x1901) but is only
// valid as an attachment point, not as a glReadPixels format.
// R_StencilShot() is a debug/screenshot utility — stub the read to a no-op.
// qglReadPixels itself is already mapped to glReadPixels in gles_compat.h,
// but passing GL_STENCIL_INDEX as the format to a GLES2 driver would
// generate GL_INVALID_ENUM. Guard the whole function instead (see below).
#ifndef GL_STENCIL_INDEX
#  define GL_STENCIL_INDEX    0x1901
#endif

#  ifndef GL_MAX_TEXTURE_UNITS_ARB
#    define GL_MAX_TEXTURE_UNITS_ARB       GL_MAX_TEXTURE_UNITS
#  endif
#  ifndef GL_MAX_TEXTURE_COORDS_ARB
#    define GL_MAX_TEXTURE_COORDS_ARB      0x8871
#  endif
#  ifndef GL_MAX_TEXTURE_IMAGE_UNITS_ARB
#    define GL_MAX_TEXTURE_IMAGE_UNITS_ARB GL_MAX_TEXTURE_IMAGE_UNITS
#  endif
#  ifndef GL_INCR_WRAP_EXT
#    define GL_INCR_WRAP_EXT               GL_INCR_WRAP
#  endif
#  ifndef GL_DECR_WRAP_EXT
#    define GL_DECR_WRAP_EXT               GL_DECR_WRAP
#  endif

#ifndef GL_INCR_WRAP_EXT
#  define GL_INCR_WRAP_EXT  GL_INCR_WRAP
#endif
#ifndef GL_DECR_WRAP_EXT
#  define GL_DECR_WRAP_EXT  GL_DECR_WRAP
#endif

// ---------------------------------------------------------------------------
// ARB VBO enums — promoted to core in GL 1.5, always core in GLES2.
// Map ARB-suffixed names to their identical core values.
// ---------------------------------------------------------------------------
#ifndef GL_ARRAY_BUFFER_ARB
#  define GL_ARRAY_BUFFER_ARB           GL_ARRAY_BUFFER
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_ARB
#  define GL_ELEMENT_ARRAY_BUFFER_ARB   GL_ELEMENT_ARRAY_BUFFER
#endif
#ifndef GL_STATIC_DRAW_ARB
#  define GL_STATIC_DRAW_ARB            GL_STATIC_DRAW
#endif
#ifndef GL_STREAM_DRAW_ARB
#  define GL_STREAM_DRAW_ARB            GL_STREAM_DRAW
#endif
#ifndef GL_DYNAMIC_DRAW_ARB
#  define GL_DYNAMIC_DRAW_ARB           GL_DYNAMIC_DRAW
#endif
#ifndef GL_STATIC_READ_ARB
#  define GL_STATIC_READ_ARB            GL_STATIC_READ
#endif
#ifndef GL_DYNAMIC_READ_ARB
#  define GL_DYNAMIC_READ_ARB           GL_DYNAMIC_READ
#endif
#ifndef GL_STREAM_READ_ARB
#  define GL_STREAM_READ_ARB            GL_STREAM_READ
#endif
#ifndef GL_BUFFER_SIZE_ARB
#  define GL_BUFFER_SIZE_ARB            GL_BUFFER_SIZE
#endif
#ifndef GL_BUFFER_USAGE_ARB
#  define GL_BUFFER_USAGE_ARB           GL_BUFFER_USAGE
#endif

// GLsizeiptrARB: the ARB extension used its own typedef before promotion.
// In GLES2 headers it is simply GLsizeiptr.
#ifndef GLsizeiptrARB
   typedef GLsizeiptr GLsizeiptrARB;
#endif
#ifndef GLintptrARB
   typedef GLintptr   GLintptrARB;
#endif

#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif

// ---------------------------------------------------------------------------
// GLES2_INTERNAL_FMT(fmt)
//
// Converts a desktop GL sized/indexed internalFormat to the nearest GLES2
// unsized base format suitable for passing to glTexImage2D's internalformat
// parameter.  Use this at every glTexImage2D / glCopyTexImage2D call site
// that passes a desktop-only format constant.
//
// GLES2 rule: internalformat must equal the format argument AND must be an
// unsized base format (GL_RGBA, GL_RGB, GL_LUMINANCE_ALPHA, GL_LUMINANCE,
// GL_ALPHA, or GL_DEPTH_COMPONENT).
// ---------------------------------------------------------------------------
static inline GLenum GLES2_INTERNAL_FMT(int fmt)
{
    switch (fmt) {
    case 0x8058: /* GL_RGBA8              */ return GL_RGBA;
    case 0x8051: /* GL_RGB8               */ return GL_RGB;
    case 0x8050: /* GL_RGB5               */ return GL_RGB;  // no 16-bit RGB in base GLES2
    case 0x8040: /* GL_LUMINANCE8         */ return GL_LUMINANCE;
    case 0x8045: /* GL_LUMINANCE8_ALPHA8  */ return GL_LUMINANCE_ALPHA;
    case 0x803C: /* GL_ALPHA8             */ return GL_ALPHA;
    case 0x804B: /* GL_INTENSITY8         */ return GL_LUMINANCE; // closest semantic
    case 0x1900: /* GL_COLOR_INDEX        */ return GL_RGBA; // palette fallback
    case 0x80E5: /* GL_COLOR_INDEX8_EXT   */ return GL_RGBA; // palette fallback
    case 0x84ED: /* GL_COMPRESSED_RGB_ARB */ return GL_RGB;
    case 0x84EE: /* GL_COMPRESSED_RGBA_ARB*/ return GL_RGBA;
    case 4:  return GL_RGBA;
    case 3:  return GL_RGB;
    case 2:  return GL_LUMINANCE_ALPHA;
    case 1:  return GL_LUMINANCE;
    default: return (GLenum)fmt; // DXT/RGBA4/etc pass through unchanged
    }
}

// ---------------------------------------------------------------------------
// ARB program parameter functions — no equivalent in GLES2.
// MegaTexture and other ARB paths are skipped at runtime when
// newStage->vertexProgram == 0 (see R_FindARBProgram stub above).
// ---------------------------------------------------------------------------
static inline void qglProgramLocalParameter4fARB(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) {}
static inline void qglProgramLocalParameter4fvARB(GLenum, GLuint, const GLfloat*) {}
static inline void qglProgramEnvParameter4fARB(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) {}
static inline void qglProgramEnvParameter4fvARB(GLenum, GLuint, const GLfloat*) {}
static inline void qglBindProgramARB(GLenum, GLuint) {}
static inline void qglGenProgramsARB(GLsizei, GLuint*) {}
static inline void qglDeleteProgramsARB(GLsizei, const GLuint*) {}
static inline void qglProgramStringARB(GLenum, GLenum, GLsizei, const GLvoid*) {}
static inline void qglGetProgramivARB(GLenum, GLenum, GLint*) {}
static inline GLboolean qglIsProgramARB(GLuint) { return GL_FALSE; }

// qglActiveStencilFaceEXT: two-sided stencil extension, absent in GLES2
static inline void qglActiveStencilFaceEXT(GLenum) {}

// qglDepthBoundsEXT: EXT_depth_bounds_test, absent in GLES2
static inline void qglDepthBoundsEXT(GLclampf, GLclampf) {}

// ---------------------------------------------------------------------------
// OpenGL 1.x immediate-mode functions — completely absent in GLES2.
// These appear in debug/visualization paths (ShowPortals, etc.).
// Stub as no-ops so the files compile; the call sites should also be
// guarded with #ifndef HAVE_OPENGLES to avoid silent no-op renders.
// ---------------------------------------------------------------------------
static inline void qglBegin(GLenum) {}
static inline void qglEnd() {}
static inline void qglColor3f(GLfloat, GLfloat, GLfloat) {}
static inline void qglColor4f(GLfloat, GLfloat, GLfloat, GLfloat) {}
static inline void qglColor3fv(const GLfloat*) {}
static inline void qglColor4fv(const GLfloat*) {}
static inline void qglColor4ub(GLubyte, GLubyte, GLubyte, GLubyte) {}
static inline void qglVertex2f(GLfloat, GLfloat) {}
static inline void qglVertex3f(GLfloat, GLfloat, GLfloat) {}
static inline void qglVertex3fv(const GLfloat*) {}
static inline void qglNormal3fv(const GLfloat*) {}
static inline void qglTexCoord2f(GLfloat, GLfloat) {}
static inline void qglTexCoord2fv(const GLfloat*) {}
static inline void qglRasterPos2f(GLfloat, GLfloat) {}
static inline void qglRect(GLfloat, GLfloat, GLfloat, GLfloat) {}
static inline void qglMatrixMode(GLenum) {}
static inline void qglLoadIdentity() {}
static inline void qglLoadMatrixf(const GLfloat*) {}
static inline void qglLoadMatrixd(const GLdouble*) {}
static inline void qglMultMatrixf(const GLfloat*) {}
static inline void qglOrtho(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) {}
static inline void qglFrustum(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) {}
static inline void qglPushMatrix() {}
static inline void qglPopMatrix() {}
static inline void qglTranslatef(GLfloat, GLfloat, GLfloat) {}
static inline void qglRotatef(GLfloat, GLfloat, GLfloat, GLfloat) {}
static inline void qglScalef(GLfloat, GLfloat, GLfloat) {}
static inline void qglShadeModel(GLenum) {}
static inline void qglLineStipple(GLint, GLushort) {}
static inline void qglPolygonMode(GLenum, GLenum) {}
static inline void qglAlphaFunc(GLenum, GLclampf) {}
static inline void qglFogf(GLenum, GLfloat) {}
static inline void qglFogfv(GLenum, const GLfloat*) {}
static inline void qglFogi(GLenum, GLint) {}
static inline void qglEnableClientState(GLenum) {}
static inline void qglDisableClientState(GLenum) {}
static inline void qglVertexPointer(GLint, GLenum, GLsizei, const GLvoid*) {}
static inline void qglNormalPointer(GLenum, GLsizei, const GLvoid*) {}
static inline void qglColorPointer(GLint, GLenum, GLsizei, const GLvoid*) {}
static inline void qglTexCoordPointer(GLint, GLenum, GLsizei, const GLvoid*) {}
static inline void qglClientActiveTexture(GLenum) {}

// ---------------------------------------------------------------------------
// Fixed-function texture coordinate generation — absent in GLES2.
// Replaced by vertex shader computations in draw_gles2.cpp.
// ---------------------------------------------------------------------------
#ifndef GL_S
#  define GL_S                  0x2000
#endif
#ifndef GL_T
#  define GL_T                  0x2001
#endif
#ifndef GL_R
#  define GL_R                  0x2002
#endif
#ifndef GL_Q
#  define GL_Q                  0x2003
#endif
#ifndef GL_TEXTURE_GEN_S
#  define GL_TEXTURE_GEN_S      0x0C60
#endif
#ifndef GL_TEXTURE_GEN_T
#  define GL_TEXTURE_GEN_T      0x0C61
#endif
#ifndef GL_TEXTURE_GEN_R
#  define GL_TEXTURE_GEN_R      0x0C62
#endif
#ifndef GL_TEXTURE_GEN_Q
#  define GL_TEXTURE_GEN_Q      0x0C63
#endif
#ifndef GL_TEXTURE_GEN_MODE
#  define GL_TEXTURE_GEN_MODE   0x2500
#endif
#ifndef GL_OBJECT_PLANE
#  define GL_OBJECT_PLANE       0x2501
#endif
#ifndef GL_EYE_PLANE
#  define GL_EYE_PLANE          0x2502
#endif
#ifndef GL_OBJECT_LINEAR
#  define GL_OBJECT_LINEAR      0x2401
#endif
#ifndef GL_EYE_LINEAR
#  define GL_EYE_LINEAR         0x2400
#endif
#ifndef GL_SPHERE_MAP
#  define GL_SPHERE_MAP         0x2402
#endif
#ifndef GL_REFLECTION_MAP_EXT
#  define GL_REFLECTION_MAP_EXT 0x8512
#endif
#ifndef GL_NORMAL_MAP_EXT
#  define GL_NORMAL_MAP_EXT     0x8511
#endif

static inline void qglTexGenfv(GLenum, GLenum, const GLfloat*) {}
static inline void qglTexGenf(GLenum, GLenum, GLfloat) {}
static inline void qglTexGeni(GLenum, GLenum, GLint) {}

// ---------------------------------------------------------------------------
// Fixed-function texture environment — absent in GLES2.
// Replaced by fragment shader uniform parameters in draw_gles2.cpp.
// ---------------------------------------------------------------------------
#ifndef GL_TEXTURE_ENV
#  define GL_TEXTURE_ENV            0x2300
#endif
#ifndef GL_TEXTURE_ENV_MODE
#  define GL_TEXTURE_ENV_MODE       0x2200
#endif
#ifndef GL_TEXTURE_ENV_COLOR
#  define GL_TEXTURE_ENV_COLOR      0x2201
#endif
#ifndef GL_MODULATE
#  define GL_MODULATE               0x2100
#endif
#ifndef GL_DECAL
#  define GL_DECAL                  0x2101
#endif
#ifndef GL_COMBINE_ARB
#  define GL_COMBINE_ARB            0x8570
#endif
#ifndef GL_COMBINE_RGB_ARB
#  define GL_COMBINE_RGB_ARB        0x8571
#endif
#ifndef GL_COMBINE_ALPHA_ARB
#  define GL_COMBINE_ALPHA_ARB      0x8572
#endif
#ifndef GL_SOURCE0_RGB_ARB
#  define GL_SOURCE0_RGB_ARB        0x8580
#endif
#ifndef GL_SOURCE1_RGB_ARB
#  define GL_SOURCE1_RGB_ARB        0x8581
#endif
#ifndef GL_SOURCE2_RGB_ARB
#  define GL_SOURCE2_RGB_ARB        0x8582
#endif
#ifndef GL_SOURCE0_ALPHA_ARB
#  define GL_SOURCE0_ALPHA_ARB      0x8588
#endif
#ifndef GL_SOURCE1_ALPHA_ARB
#  define GL_SOURCE1_ALPHA_ARB      0x8589
#endif
#ifndef GL_OPERAND0_RGB_ARB
#  define GL_OPERAND0_RGB_ARB       0x8590
#endif
#ifndef GL_OPERAND1_RGB_ARB
#  define GL_OPERAND1_RGB_ARB       0x8591
#endif
#ifndef GL_OPERAND0_ALPHA_ARB
#  define GL_OPERAND0_ALPHA_ARB     0x8598
#endif
#ifndef GL_OPERAND1_ALPHA_ARB
#  define GL_OPERAND1_ALPHA_ARB     0x8599
#endif
#ifndef GL_RGB_SCALE_ARB
#  define GL_RGB_SCALE_ARB          0x8573
#endif
#ifndef GL_ALPHA_SCALE
#  define GL_ALPHA_SCALE            0x0D1C
#endif
#ifndef GL_PRIMARY_COLOR_ARB
#  define GL_PRIMARY_COLOR_ARB      0x8577
#endif
#ifndef GL_PREVIOUS_ARB
#  define GL_PREVIOUS_ARB           0x8578
#endif
#ifndef GL_CONSTANT_ARB
#  define GL_CONSTANT_ARB           0x8576
#endif

static inline void qglTexEnvi(GLenum, GLenum, GLint) {}
static inline void qglTexEnvf(GLenum, GLenum, GLfloat) {}
static inline void qglTexEnvfv(GLenum, GLenum, const GLfloat*) {}

// GL_TexEnv is a doom3 wrapper around qglTexEnvf — stub it too
static inline void GL_TexEnv(GLenum) {}

// ---------------------------------------------------------------------------
// Fixed-function client state arrays — absent in GLES2.
// Replaced by named vertex attributes in draw_gles2.cpp.
// ---------------------------------------------------------------------------
#ifndef GL_VERTEX_ARRAY
#  define GL_VERTEX_ARRAY           0x8074
#endif
#ifndef GL_NORMAL_ARRAY
#  define GL_NORMAL_ARRAY           0x8075
#endif
#ifndef GL_COLOR_ARRAY
#  define GL_COLOR_ARRAY            0x8076
#endif
#ifndef GL_TEXTURE_COORD_ARRAY
#  define GL_TEXTURE_COORD_ARRAY    0x8078
#endif

// ---------------------------------------------------------------------------
// Fixed-function matrix stack — absent in GLES2.
// ---------------------------------------------------------------------------
#ifndef GL_MODELVIEW
#  define GL_MODELVIEW   0x1700
#endif
#ifndef GL_PROJECTION
#  define GL_PROJECTION  0x1701
#endif
#ifndef GL_TEXTURE_MATRIX
#  define GL_TEXTURE_MATRIX 0x0BA8
#endif

// ---------------------------------------------------------------------------
// Alpha test — absent in GLES2 core (handled in fragment shader discard).
// ---------------------------------------------------------------------------
#ifndef GL_ALPHA_TEST
#  define GL_ALPHA_TEST  0x0BC0
#endif

// ---------------------------------------------------------------------------
// Primitives missing from GLES2.
// GL_QUADS: used in RB_STD_LightScale fullscreen pass — replaced by two
// triangles or a triangle strip in the GLES2 equivalent.
// ---------------------------------------------------------------------------
#ifndef GL_QUADS
#  define GL_QUADS  0x0007
#endif
#ifndef GL_POLYGON
#  define GL_POLYGON 0x0009
#endif
#ifndef GL_QUAD_STRIP
#  define GL_QUAD_STRIP 0x0008
#endif

// ---------------------------------------------------------------------------
// Depth bounds test — EXT extension, not available in GLES2.
// ---------------------------------------------------------------------------
#ifndef GL_DEPTH_BOUNDS_TEST_EXT
#  define GL_DEPTH_BOUNDS_TEST_EXT  0x8890
#endif

// ---------------------------------------------------------------------------
// Fixed-function lighting/rasterization state — absent in GLES2.
// ---------------------------------------------------------------------------
#ifndef GL_LIGHTING
#  define GL_LIGHTING           0x0B50
#endif
#ifndef GL_LINE_STIPPLE
#  define GL_LINE_STIPPLE       0x0B24
#endif
#ifndef GL_SMOOTH
#  define GL_SMOOTH             0x1D01
#endif
#ifndef GL_FLAT
#  define GL_FLAT               0x1D00
#endif

// glPolygonMode fill modes — absent in GLES2 (no wireframe support in core)
#ifndef GL_FILL
#  define GL_FILL               0x1B02
#endif
#ifndef GL_LINE
#  define GL_LINE               0x1B01
#endif
#ifndef GL_POINT
#  define GL_POINT              0x1B00
#endif

// ---------------------------------------------------------------------------
// Multitexture ARB suffix — core in GLES2 without suffix.
// ---------------------------------------------------------------------------
#ifndef GL_TEXTURE0_ARB
#  define GL_TEXTURE0_ARB       GL_TEXTURE0
#endif
#ifndef GL_TEXTURE1_ARB
#  define GL_TEXTURE1_ARB       GL_TEXTURE1
#endif
#ifndef GL_TEXTURE2_ARB
#  define GL_TEXTURE2_ARB       GL_TEXTURE2
#endif
#ifndef GL_TEXTURE3_ARB
#  define GL_TEXTURE3_ARB       GL_TEXTURE3
#endif
#define qglActiveTextureARB         glActiveTexture
#define qglClientActiveTextureARB   qglClientActiveTexture  // already a no-op stub

// ---------------------------------------------------------------------------
// GL_TexEnv mode enums used in tr_backend.cpp's GL_TexEnv() function.
// ---------------------------------------------------------------------------
#ifndef GL_COMBINE_EXT
#  define GL_COMBINE_EXT        0x8570   // same value as GL_COMBINE_ARB
#endif
#ifndef GL_ADD
#  define GL_ADD                0x0104
#endif
#ifndef GL_REPLACE
#  define GL_REPLACE            0x1E01
#endif

// ---------------------------------------------------------------------------
// glDrawBuffer — GLES2 always draws to the back buffer implicitly.
// RB_SetBuffer uses this to select front/back; no-op on GLES2.
// ---------------------------------------------------------------------------
static inline void qglDrawBuffer(GLenum) {}

#define qglUniform1iv            glUniform1iv
#define qglValidateProgram       glValidateProgram
#define qglStencilOpSeparate     glStencilOpSeparate

#endif // HAVE_OPENGLES
#endif // __GLES_COMPAT_H__
