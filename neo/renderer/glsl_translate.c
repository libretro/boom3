/*
 * glsl_translate: see glsl_translate.h.
 *
 * C89.
 */
#include <stdlib.h>
#include <string.h>

#include "glsl_translate.h"

typedef struct glsl_buf_s {
   char  *data;
   size_t len;
   size_t cap;
} glsl_buf_t;

static int glsl_buf_put(glsl_buf_t *b, const char *s, size_t n)
{
   if (b->len + n + 1 > b->cap)
   {
      size_t cap  = (b->len + n + 1) * 2;
      char  *data = (char *)realloc(b->data, cap);
      if (!data)
         return 0;
      b->data = data;
      b->cap  = cap;
   }
   memcpy(b->data + b->len, s, n);
   b->len         += n;
   b->data[b->len] = '\0';
   return 1;
}

static int glsl_ident_char(char c)
{
   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
      || (c >= '0' && c <= '9') || c == '_';
}

typedef struct glsl_rename_s {
   const char *from;
   const char *to;
} glsl_rename_t;

/* The identifier at p that renames lists, or NULL. */
static const glsl_rename_t *glsl_find(const char *p, const glsl_rename_t *renames, int count)
{
   int i;
   for (i = 0; i < count; i++)
   {
      size_t n = strlen(renames[i].from);
      if (!strncmp(p, renames[i].from, n) && !glsl_ident_char(p[n]))
         return &renames[i];
   }
   return NULL;
}

/* Whether the line [p, end) starts with word, after blanks. */
static int glsl_line_starts(const char *p, const char *end, const char *word)
{
   size_t n = strlen(word);
   while (p < end && (*p == ' ' || *p == '\t'))
      p++;
   return (size_t)(end - p) >= n && !strncmp(p, word, n);
}

char *glsl_translate(const char *src, int stage, int target)
{
   static const glsl_rename_t es100_vs[] = {
      { "attribute", "in" }, { "varying", "out" },
      { "texture2DProj", "textureProj" }, { "texture2D", "texture" }, { "textureCube", "texture" }
   };
   static const glsl_rename_t es100_fs[] = {
      { "varying", "in" }, { "gl_FragColor", "boom3_FragColor" },
      { "texture2DProj", "textureProj" }, { "texture2D", "texture" }, { "textureCube", "texture" }
   };
   static const glsl_rename_t precision[] = {
      { "lowp", "" }, { "mediump", "" }, { "highp", "" }
   };
   glsl_buf_t          out;
   const char         *p;
   const glsl_rename_t *renames = NULL;
   int                 count    = 0;
   int                 es300;
   int                 versioned = 0;
   const char         *version;

   memset(&out, 0, sizeof(out));
   while (*src == '\n')
      src++;
   if (target == GLSL_TARGET_ES)
   {
      glsl_buf_put(&out, src, strlen(src));
      return out.data;
   }

   es300 = strstr(src, "#version 300 es") != NULL;
   if (es300 && target == GLSL_TARGET_COMPAT)
      return NULL;
   version = target == GLSL_TARGET_CORE ? "#version 330 core\n" : "#version 120\n";

   if (target == GLSL_TARGET_CORE && !es300)
   {
      renames = stage == GLSL_STAGE_VERTEX ? es100_vs : es100_fs;
      count   = 5;
   }
   else if (target == GLSL_TARGET_COMPAT)
   {
      renames = precision;
      count   = 3;
   }

   /* the shaders without a #version line get one first */
   if (!strstr(src, "#version"))
   {
      if (!glsl_buf_put(&out, version, strlen(version)))
         return NULL;
      versioned = 1;
      if (target == GLSL_TARGET_CORE && !es300 && stage == GLSL_STAGE_FRAGMENT
            && !glsl_buf_put(&out, "out vec4 boom3_FragColor;\n", 26))
         return NULL;
   }

   p = src;
   while (*p)
   {
      const char *eol = strchr(p, '\n');
      const char *end = eol ? eol : p + strlen(p);
      const char *q;

      if (glsl_line_starts(p, end, "#version"))
      {
         if (!versioned)
         {
            if (!glsl_buf_put(&out, version, strlen(version)))
               return NULL;
            versioned = 1;
            if (target == GLSL_TARGET_CORE && !es300 && stage == GLSL_STAGE_FRAGMENT
                  && !glsl_buf_put(&out, "out vec4 boom3_FragColor;\n", 26))
               return NULL;
         }
         p = eol ? eol + 1 : end;
         continue;
      }
      if (glsl_line_starts(p, end, "#extension GL_OES_standard_derivatives")
            || (target == GLSL_TARGET_COMPAT && glsl_line_starts(p, end, "precision ")))
      {
         p = eol ? eol + 1 : end;
         continue;
      }

      /* the line, its identifiers renamed; comments and the preprocessor
       * lines pass through the same way, which is harmless for these */
      q = p;
      while (q < end)
      {
         const glsl_rename_t *r = NULL;
         if (glsl_ident_char(*q) && (q == p || !glsl_ident_char(q[-1])))
            r = glsl_find(q, renames, count);
         if (r)
         {
            if (!glsl_buf_put(&out, r->to, strlen(r->to)))
               return NULL;
            q += strlen(r->from);
            /* a dropped precision qualifier takes its blank with it */
            if (!r->to[0] && (*q == ' ' || *q == '\t'))
               q++;
         }
         else
         {
            if (!glsl_buf_put(&out, q, 1))
               return NULL;
            q++;
         }
      }
      if (eol && !glsl_buf_put(&out, "\n", 1))
         return NULL;
      p = eol ? eol + 1 : end;
   }
   return out.data;
}
