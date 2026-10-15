/*
 * gl_caps: runtime GL entry points and capabilities; see gl_caps.h.
 *
 * C89.
 */
#include <stdlib.h>
#include <string.h>

#include "gl_caps.h"

#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_CONTEXT_PROFILE_MASK
#define GL_CONTEXT_PROFILE_MASK 0x9126
#endif
#ifndef GL_CONTEXT_CORE_PROFILE_BIT
#define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#endif
#ifndef GL_MAX_UNIFORM_BLOCK_SIZE
#define GL_MAX_UNIFORM_BLOCK_SIZE 0x8A30
#endif

gl_caps_t glCaps;

/* the loader the last gl_caps_init ran with, for gl_caps_has_extension */
static gl_caps_loader_t caps_loader;

static gl_caps_proc_t caps_load(const char *a, const char *b, const char *c)
{
   gl_caps_proc_t p = NULL;
   if (!caps_loader)
      return NULL;
   if (a)
      p = caps_loader(a);
   if (!p && b)
      p = caps_loader(b);
   if (!p && c)
      p = caps_loader(c);
   return p;
}

/* The context version from GL_VERSION: "OpenGL ES 3.2 ..." on ES,
 * "4.6.0 ..." on desktop. */
static void caps_parse_version(void)
{
   const char *v = (const char *)glGetString(GL_VERSION);
   const char *p;

   glCaps.major = 0;
   glCaps.minor = 0;
   glCaps.es    = 0;
   if (!v)
      return;
   p = strstr(v, "OpenGL ES");
   if (p)
   {
      glCaps.es = 1;
      p += 9;
      while (*p && (*p < '0' || *p > '9'))
         p++;
   }
   else
      p = v;
   glCaps.major = atoi(p);
   while (*p >= '0' && *p <= '9')
      p++;
   if (*p == '.')
      glCaps.minor = atoi(p + 1);
}

static int caps_at_least(int major, int minor)
{
   return glCaps.major > major
      || (glCaps.major == major && glCaps.minor >= minor);
}

int gl_caps_has_extension(const char *ext)
{
   size_t len;

   if (!ext)
      return 0;
   len = strlen(ext);

   if (glCaps.major >= 3 && glCaps.GetStringi)
   {
      GLint n = 0;
      GLint i;
      glGetIntegerv(GL_NUM_EXTENSIONS, &n);
      for (i = 0; i < n; i++)
      {
         const char *e = (const char *)glCaps.GetStringi(GL_EXTENSIONS, (GLuint)i);
         if (e && !strcmp(e, ext))
            return 1;
      }
      return 0;
   }
   else
   {
      const char *all = (const char *)glGetString(GL_EXTENSIONS);
      const char *p   = all;
      if (!all)
         return 0;
      while ((p = strstr(p, ext)) != NULL)
      {
         if ((p == all || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0'))
            return 1;
         p += len;
      }
      return 0;
   }
}

#define CAPS_GET(field, type, a, b, c) glCaps.field = (type)caps_load(a, b, c)

void gl_caps_init(gl_caps_loader_t load)
{
   GLint v;

   memset(&glCaps, 0, sizeof(glCaps));
   caps_loader = load;
   caps_parse_version();

   CAPS_GET(GetStringi, gl_caps_GetStringi_t, "glGetStringi", NULL, NULL);

   if (!glCaps.es && caps_at_least(3, 2))
   {
      v = 0;
      glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &v);
      while (glGetError() != GL_NO_ERROR) { }
      glCaps.core = (v & GL_CONTEXT_CORE_PROFILE_BIT) ? 1 : 0;
   }

   CAPS_GET(MapBufferRange, gl_caps_MapBufferRange_t, "glMapBufferRange", "glMapBufferRangeEXT", NULL);
   CAPS_GET(FlushMappedBufferRange, gl_caps_FlushMappedBufferRange_t, "glFlushMappedBufferRange", "glFlushMappedBufferRangeEXT", NULL);
   CAPS_GET(UnmapBuffer, gl_caps_UnmapBuffer_t, "glUnmapBuffer", "glUnmapBufferOES", NULL);
   CAPS_GET(BufferStorage, gl_caps_BufferStorage_t, "glBufferStorage", "glBufferStorageEXT", NULL);
   CAPS_GET(CopyBufferSubData, gl_caps_CopyBufferSubData_t, "glCopyBufferSubData", NULL, NULL);
   CAPS_GET(FenceSync, gl_caps_FenceSync_t, "glFenceSync", "glFenceSyncAPPLE", NULL);
   CAPS_GET(ClientWaitSync, gl_caps_ClientWaitSync_t, "glClientWaitSync", "glClientWaitSyncAPPLE", NULL);
   CAPS_GET(DeleteSync, gl_caps_DeleteSync_t, "glDeleteSync", "glDeleteSyncAPPLE", NULL);
   CAPS_GET(DrawElementsBaseVertex, gl_caps_DrawElementsBaseVertex_t, "glDrawElementsBaseVertex", "glDrawElementsBaseVertexOES", "glDrawElementsBaseVertexEXT");
   CAPS_GET(BindBufferRange, gl_caps_BindBufferRange_t, "glBindBufferRange", NULL, NULL);
   CAPS_GET(BindBufferBase, gl_caps_BindBufferBase_t, "glBindBufferBase", NULL, NULL);
   CAPS_GET(GetUniformBlockIndex, gl_caps_GetUniformBlockIndex_t, "glGetUniformBlockIndex", NULL, NULL);
   CAPS_GET(UniformBlockBinding, gl_caps_UniformBlockBinding_t, "glUniformBlockBinding", NULL, NULL);
   CAPS_GET(GetActiveUniformBlockiv, gl_caps_GetActiveUniformBlockiv_t, "glGetActiveUniformBlockiv", NULL, NULL);
   CAPS_GET(GetUniformIndices, gl_caps_GetUniformIndices_t, "glGetUniformIndices", NULL, NULL);
   CAPS_GET(GetActiveUniformsiv, gl_caps_GetActiveUniformsiv_t, "glGetActiveUniformsiv", NULL, NULL);
   CAPS_GET(GetProgramBinary, gl_caps_GetProgramBinary_t, "glGetProgramBinary", "glGetProgramBinaryOES", NULL);
   CAPS_GET(ProgramBinary, gl_caps_ProgramBinary_t, "glProgramBinary", "glProgramBinaryOES", NULL);
   CAPS_GET(ProgramParameteri, gl_caps_ProgramParameteri_t, "glProgramParameteri", NULL, NULL);
   CAPS_GET(TexStorage2D, gl_caps_TexStorage2D_t, "glTexStorage2D", "glTexStorage2DEXT", NULL);
   CAPS_GET(GenVertexArrays, gl_caps_GenVertexArrays_t, "glGenVertexArrays", "glGenVertexArraysOES", NULL);
   CAPS_GET(BindVertexArray, gl_caps_BindVertexArray_t, "glBindVertexArray", "glBindVertexArrayOES", NULL);
   CAPS_GET(DeleteVertexArrays, gl_caps_DeleteVertexArrays_t, "glDeleteVertexArrays", "glDeleteVertexArraysOES", NULL);
   CAPS_GET(BlitFramebuffer, gl_caps_BlitFramebuffer_t, "glBlitFramebuffer", NULL, NULL);
   CAPS_GET(MultiDrawElementsIndirect, gl_caps_MultiDrawElementsIndirect_t, "glMultiDrawElementsIndirect", "glMultiDrawElementsIndirectEXT", NULL);
   CAPS_GET(GetTextureHandle, gl_caps_GetTextureHandle_t, "glGetTextureHandleARB", NULL, NULL);
   CAPS_GET(GetTextureSamplerHandle, gl_caps_GetTextureSamplerHandle_t, "glGetTextureSamplerHandleARB", NULL, NULL);
   CAPS_GET(MakeTextureHandleResident, gl_caps_MakeTextureHandleResident_t, "glMakeTextureHandleResidentARB", NULL, NULL);
   CAPS_GET(MakeTextureHandleNonResident, gl_caps_MakeTextureHandleNonResident_t, "glMakeTextureHandleNonResidentARB", NULL, NULL);

   /* Each group counts only on a version or extension that promises it:
    * a loader may hand back a stub for a name the context does not
    * implement. */
   glCaps.mapRange = glCaps.MapBufferRange && glCaps.FlushMappedBufferRange && glCaps.UnmapBuffer
      && (glCaps.es ? caps_at_least(3, 0) : (caps_at_least(3, 0) || gl_caps_has_extension("GL_ARB_map_buffer_range")));
   glCaps.sync = glCaps.FenceSync && glCaps.ClientWaitSync && glCaps.DeleteSync
      && (caps_at_least(3, glCaps.es ? 0 : 2) || gl_caps_has_extension("GL_ARB_sync"));
   glCaps.bufferStorage = glCaps.BufferStorage && glCaps.mapRange
      && ((!glCaps.es && caps_at_least(4, 4))
         || gl_caps_has_extension("GL_ARB_buffer_storage")
         || gl_caps_has_extension("GL_EXT_buffer_storage"));
   glCaps.copyBuffer = glCaps.CopyBufferSubData
      && (caps_at_least(3, glCaps.es ? 0 : 1) || gl_caps_has_extension("GL_ARB_copy_buffer"));
   glCaps.baseVertex = glCaps.DrawElementsBaseVertex
      && ((glCaps.es ? caps_at_least(3, 2) : caps_at_least(3, 2))
         || gl_caps_has_extension("GL_ARB_draw_elements_base_vertex")
         || gl_caps_has_extension("GL_OES_draw_elements_base_vertex")
         || gl_caps_has_extension("GL_EXT_draw_elements_base_vertex"));
   glCaps.uniformBuffers = glCaps.BindBufferRange && glCaps.BindBufferBase && glCaps.GetUniformBlockIndex
      && glCaps.UniformBlockBinding && glCaps.GetActiveUniformBlockiv && glCaps.GetUniformIndices
      && glCaps.GetActiveUniformsiv
      && (caps_at_least(3, glCaps.es ? 0 : 1) || gl_caps_has_extension("GL_ARB_uniform_buffer_object"));
   glCaps.programBinary = glCaps.GetProgramBinary && glCaps.ProgramBinary && glCaps.ProgramParameteri
      && (glCaps.es ? caps_at_least(3, 0) : (caps_at_least(4, 1) || gl_caps_has_extension("GL_ARB_get_program_binary")));
   glCaps.texStorage = glCaps.TexStorage2D
      && (glCaps.es ? caps_at_least(3, 0) : (caps_at_least(4, 2) || gl_caps_has_extension("GL_ARB_texture_storage")));
   glCaps.vertexArrays = glCaps.GenVertexArrays && glCaps.BindVertexArray && glCaps.DeleteVertexArrays
      && (caps_at_least(3, 0) || gl_caps_has_extension("GL_ARB_vertex_array_object")
         || gl_caps_has_extension("GL_OES_vertex_array_object"));
   glCaps.blit = glCaps.BlitFramebuffer && caps_at_least(3, 0);
   glCaps.multiDrawIndirect = glCaps.MultiDrawElementsIndirect && !glCaps.es
      && (caps_at_least(4, 3) || gl_caps_has_extension("GL_ARB_multi_draw_indirect"))
      && (caps_at_least(4, 6) || gl_caps_has_extension("GL_ARB_shader_draw_parameters"));
   glCaps.bindless = glCaps.GetTextureHandle && glCaps.GetTextureSamplerHandle
      && glCaps.MakeTextureHandleResident && glCaps.MakeTextureHandleNonResident
      && !glCaps.es && gl_caps_has_extension("GL_ARB_bindless_texture");

   glCaps.uniformBufferAlign  = 256;
   glCaps.maxUniformBlockSize = 16384;
   if (glCaps.uniformBuffers)
   {
      v = 0;
      glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &v);
      if (v > 0)
         glCaps.uniformBufferAlign = v;
      v = 0;
      glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &v);
      if (v > 0)
         glCaps.maxUniformBlockSize = v;
   }
   while (glGetError() != GL_NO_ERROR) { }
}

const char *gl_caps_es31_missing(void)
{
   if (glCaps.es ? !caps_at_least(3, 1) : !caps_at_least(4, 3))
      return glCaps.es ? "an OpenGL ES 3.1 context" : "an OpenGL 4.3 context";
   if (!glCaps.sync)
      return "fences";
   if (!glCaps.mapRange)
      return "mapped buffer ranges";
   if (!glCaps.uniformBuffers)
      return "uniform buffers";
   if (!glCaps.copyBuffer)
      return "buffer copies";
   return NULL;
}
