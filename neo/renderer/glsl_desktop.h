/*
 * glsl_desktop: the GL 2.0+ entry points the GLSL renderers call through
 * qgl names, for a desktop core, where qgl.h only declares the GL 1.x and
 * ARB ones.  libretro-common's glsym resolves the GL names at context
 * reset; on GLES builds gles_compat.h maps the same names.
 *
 * C89.
 */
#ifndef BOOM3_GLSL_DESKTOP_H
#define BOOM3_GLSL_DESKTOP_H

#ifndef HAVE_OPENGLES

#define qglAttachShader             glAttachShader
#define qglBindAttribLocation       glBindAttribLocation
#define qglCompileShader            glCompileShader
#define qglCreateProgram            glCreateProgram
#define qglCreateShader             glCreateShader
#define qglDeleteProgram            glDeleteProgram
#define qglDeleteShader             glDeleteShader
#define qglGetAttribLocation        glGetAttribLocation
#define qglGetProgramiv             glGetProgramiv
#define qglGetShaderInfoLog         glGetShaderInfoLog
#define qglGetShaderiv              glGetShaderiv
#define qglGetUniformLocation       glGetUniformLocation
#define qglLinkProgram              glLinkProgram
#define qglShaderSource             glShaderSource
#define qglUniform1fv               glUniform1fv
#define qglUniform1i                glUniform1i
#define qglUniform4fv               glUniform4fv
#define qglUniformMatrix4fv         glUniformMatrix4fv
#define qglUseProgram               glUseProgram
#define qglValidateProgram          glValidateProgram
#define qglVertexAttribPointer      glVertexAttribPointer
#define qglEnableVertexAttribArray  glEnableVertexAttribArray
#define qglDisableVertexAttribArray glDisableVertexAttribArray
#define qglGenBuffers               glGenBuffers
/* glDepthRangef is GL 4.1; every desktop context has the double one */
#define qglDepthRangef( n, f )      qglDepthRange( (GLclampd)( n ), (GLclampd)( f ) )

#endif

#endif
