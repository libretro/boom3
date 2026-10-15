/*
 * gl_caps: the GL entry points and capabilities above the floor the
 * renderers are built against, resolved at runtime.
 *
 * A GLES 3 build links only GLES 3.0; the ES 3.1 / GL 4.x renderer's
 * entry points (buffer storage, base-vertex draws, indirect draws,
 * bindless textures) are looked up through the frontend's
 * get_proc_address, so the core still loads where they are absent and
 * the renderer that needs them is refused instead.
 *
 * C89.
 */
#ifndef BOOM3_GL_CAPS_H
#define BOOM3_GL_CAPS_H

#include <stddef.h>
#include <glsym/glsym.h>

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gl_caps_proc_t)(void);
typedef gl_caps_proc_t (*gl_caps_loader_t)(const char *name);

typedef struct __GLsync *gl_caps_sync_t;

typedef void *(APIENTRYP gl_caps_MapBufferRange_t)(GLenum target, ptrdiff_t offset, ptrdiff_t length, GLbitfield access);
typedef void (APIENTRYP gl_caps_FlushMappedBufferRange_t)(GLenum target, ptrdiff_t offset, ptrdiff_t length);
typedef GLboolean (APIENTRYP gl_caps_UnmapBuffer_t)(GLenum target);
typedef void (APIENTRYP gl_caps_BufferStorage_t)(GLenum target, ptrdiff_t size, const void *data, GLbitfield flags);
typedef void (APIENTRYP gl_caps_CopyBufferSubData_t)(GLenum readTarget, GLenum writeTarget, ptrdiff_t readOffset, ptrdiff_t writeOffset, ptrdiff_t size);
typedef gl_caps_sync_t (APIENTRYP gl_caps_FenceSync_t)(GLenum condition, GLbitfield flags);
typedef GLenum (APIENTRYP gl_caps_ClientWaitSync_t)(gl_caps_sync_t sync, GLbitfield flags, unsigned long long timeout);
typedef void (APIENTRYP gl_caps_DeleteSync_t)(gl_caps_sync_t sync);
typedef void (APIENTRYP gl_caps_DrawElementsBaseVertex_t)(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex);
typedef void (APIENTRYP gl_caps_BindBufferRange_t)(GLenum target, GLuint index, GLuint buffer, ptrdiff_t offset, ptrdiff_t size);
typedef void (APIENTRYP gl_caps_BindBufferBase_t)(GLenum target, GLuint index, GLuint buffer);
typedef GLuint (APIENTRYP gl_caps_GetUniformBlockIndex_t)(GLuint program, const GLchar *name);
typedef void (APIENTRYP gl_caps_UniformBlockBinding_t)(GLuint program, GLuint block, GLuint binding);
typedef void (APIENTRYP gl_caps_GetActiveUniformBlockiv_t)(GLuint program, GLuint block, GLenum pname, GLint *params);
typedef void (APIENTRYP gl_caps_GetUniformIndices_t)(GLuint program, GLsizei count, const GLchar *const *names, GLuint *indices);
typedef void (APIENTRYP gl_caps_GetActiveUniformsiv_t)(GLuint program, GLsizei count, const GLuint *indices, GLenum pname, GLint *params);
typedef void (APIENTRYP gl_caps_GetProgramBinary_t)(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat, void *binary);
typedef void (APIENTRYP gl_caps_ProgramBinary_t)(GLuint program, GLenum binaryFormat, const void *binary, GLsizei length);
typedef void (APIENTRYP gl_caps_ProgramParameteri_t)(GLuint program, GLenum pname, GLint value);
typedef void (APIENTRYP gl_caps_TexStorage2D_t)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRYP gl_caps_GenVertexArrays_t)(GLsizei n, GLuint *arrays);
typedef void (APIENTRYP gl_caps_BindVertexArray_t)(GLuint array);
typedef void (APIENTRYP gl_caps_DeleteVertexArrays_t)(GLsizei n, const GLuint *arrays);
typedef const GLubyte *(APIENTRYP gl_caps_GetStringi_t)(GLenum name, GLuint index);
typedef void (APIENTRYP gl_caps_BlitFramebuffer_t)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
typedef void (APIENTRYP gl_caps_MultiDrawElementsIndirect_t)(GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride);
typedef unsigned long long (APIENTRYP gl_caps_GetTextureHandle_t)(GLuint texture);
typedef unsigned long long (APIENTRYP gl_caps_GetTextureSamplerHandle_t)(GLuint texture, GLuint sampler);
typedef void (APIENTRYP gl_caps_MakeTextureHandleResident_t)(unsigned long long handle);
typedef void (APIENTRYP gl_caps_MakeTextureHandleNonResident_t)(unsigned long long handle);

/* Entry points are NULL when the context lacks them; the flags say which
 * groups are complete.  Members by alignment: pointers first. */
typedef struct gl_caps_s {
	gl_caps_MapBufferRange_t              MapBufferRange;
	gl_caps_FlushMappedBufferRange_t      FlushMappedBufferRange;
	gl_caps_UnmapBuffer_t                 UnmapBuffer;
	gl_caps_BufferStorage_t               BufferStorage;
	gl_caps_CopyBufferSubData_t           CopyBufferSubData;
	gl_caps_FenceSync_t                   FenceSync;
	gl_caps_ClientWaitSync_t              ClientWaitSync;
	gl_caps_DeleteSync_t                  DeleteSync;
	gl_caps_DrawElementsBaseVertex_t      DrawElementsBaseVertex;
	gl_caps_BindBufferRange_t             BindBufferRange;
	gl_caps_BindBufferBase_t              BindBufferBase;
	gl_caps_GetUniformBlockIndex_t        GetUniformBlockIndex;
	gl_caps_UniformBlockBinding_t         UniformBlockBinding;
	gl_caps_GetActiveUniformBlockiv_t     GetActiveUniformBlockiv;
	gl_caps_GetUniformIndices_t           GetUniformIndices;
	gl_caps_GetActiveUniformsiv_t         GetActiveUniformsiv;
	gl_caps_GetProgramBinary_t            GetProgramBinary;
	gl_caps_ProgramBinary_t               ProgramBinary;
	gl_caps_ProgramParameteri_t           ProgramParameteri;
	gl_caps_TexStorage2D_t                TexStorage2D;
	gl_caps_GenVertexArrays_t             GenVertexArrays;
	gl_caps_BindVertexArray_t             BindVertexArray;
	gl_caps_DeleteVertexArrays_t          DeleteVertexArrays;
	gl_caps_GetStringi_t                  GetStringi;
	gl_caps_BlitFramebuffer_t             BlitFramebuffer;
	gl_caps_MultiDrawElementsIndirect_t   MultiDrawElementsIndirect;
	gl_caps_GetTextureHandle_t            GetTextureHandle;
	gl_caps_GetTextureSamplerHandle_t     GetTextureSamplerHandle;
	gl_caps_MakeTextureHandleResident_t   MakeTextureHandleResident;
	gl_caps_MakeTextureHandleNonResident_t MakeTextureHandleNonResident;

	int  major, minor;            /* context version, from GL_VERSION */
	int  uniformBufferAlign;      /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT */
	int  maxUniformBlockSize;     /* GL_MAX_UNIFORM_BLOCK_SIZE */

	unsigned char es;             /* an OpenGL ES context */
	unsigned char core;           /* a desktop core-profile context */
	unsigned char mapRange;       /* MapBufferRange, FlushMappedBufferRange, UnmapBuffer */
	unsigned char bufferStorage;  /* persistent coherent mapping */
	unsigned char sync;           /* fences */
	unsigned char copyBuffer;     /* CopyBufferSubData */
	unsigned char baseVertex;     /* DrawElementsBaseVertex */
	unsigned char uniformBuffers; /* the uniform block entry points */
	unsigned char programBinary;  /* GetProgramBinary, ProgramBinary, ProgramParameteri */
	unsigned char texStorage;     /* TexStorage2D */
	unsigned char vertexArrays;   /* vertex array objects */
	unsigned char blit;           /* BlitFramebuffer */
	unsigned char multiDrawIndirect; /* MultiDrawElementsIndirect, with draw parameters in shaders */
	unsigned char bindless;       /* ARB_bindless_texture */
} gl_caps_t;

extern gl_caps_t glCaps;

/* Fills glCaps for the current context: version and extensions are read
 * from it, entry points through load.  Call with the context current,
 * after every context reset. */
void gl_caps_init(gl_caps_loader_t load);

/* Whether the current context names ext, searched the way the context
 * allows (GL_EXTENSIONS by index on GL 3+ and ES 3+). */
int gl_caps_has_extension(const char *ext);

/* What the ES 3.1 / GL 4.3 renderer needs and the context lacks, for the
 * log, or NULL when it can run: fences, mapped ranges, uniform buffers
 * and a context of at least ES 3.1 or GL 4.3. */
const char *gl_caps_es31_missing(void);

#ifdef __cplusplus
}
#endif

#endif
