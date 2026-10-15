/*
 * glsl_blocks: see glsl_blocks.h.
 *
 * C89.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glsl_blocks.h"

/* Whether the line [line, end) declares one of the names. */
static int glsl_blocks_declares(const char *line, const char *end,
      const char *const *names, int count)
{
   int n;

   while (line < end && (*line == ' ' || *line == '\t'))
      line++;
   if ((size_t)(end - line) < 8 || strncmp(line, "uniform ", 8) != 0)
      return 0;
   for (n = 0; n < count; n++)
   {
      size_t      len = strlen(names[n]);
      const char *p   = line;
      while (p + len <= end)
      {
         if (!strncmp(p, names[n], len) && p > line && p[-1] == ' '
               && p + len < end && (p[len] == ';' || p[len] == ' ' || p[len] == '['))
            return 1;
         p++;
      }
   }
   return 0;
}

char *glsl_uniforms_to_block(const char *src, const char *block,
      const char *const *names, int count)
{
   size_t      src_len = strlen(src);
   /* the source, the header and closing, and an indent per line */
   char       *rest    = (char *)malloc(src_len + 1);
   char       *moved   = (char *)malloc(src_len * 2 + 1);
   char       *out;
   size_t      rest_len  = 0;
   size_t      moved_len = 0;
   size_t      insert_at = (size_t)-1;
   const char *p         = src;

   if (!rest || !moved)
   {
      free(rest);
      free(moved);
      return NULL;
   }
   while (*p)
   {
      const char *eol  = strchr(p, '\n');
      const char *end  = eol ? eol : p + strlen(p);
      size_t      len  = (size_t)(end - p) + (eol ? 1 : 0);

      if (glsl_blocks_declares(p, end, names, count))
      {
         const char *t = p;
         if (insert_at == (size_t)-1)
            insert_at = rest_len;
         while (*t == ' ' || *t == '\t')
            t++;
         memcpy(moved + moved_len, "  ", 2);
         moved_len += 2;
         memcpy(moved + moved_len, t, (size_t)(end - t));
         moved_len += (size_t)(end - t);
         moved[moved_len++] = '\n';
      }
      else
      {
         memcpy(rest + rest_len, p, len);
         rest_len += len;
      }
      p += len;
   }
   rest[rest_len] = '\0';

   if (insert_at == (size_t)-1)
   {
      free(moved);
      return rest;
   }
   out = (char *)malloc(rest_len + moved_len + strlen(block) + 64);
   if (out)
   {
      size_t o = 0;
      memcpy(out, rest, insert_at);
      o = insert_at;
      o += (size_t)sprintf(out + o, "layout(std140) uniform %s\n{\n", block);
      memcpy(out + o, moved, moved_len);
      o += moved_len;
      memcpy(out + o, "};\n", 3);
      o += 3;
      strcpy(out + o, rest + insert_at);
   }
   free(rest);
   free(moved);
   return out;
}
