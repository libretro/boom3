/*
 * vk_buffer: the static vertex and index data, packed into a few large
 * device-local buffers.  Where the GPU offers device-local memory the CPU
 * can map (ReBAR, UMA) a block is written in place; elsewhere it goes
 * through a staging copy recorded ahead of the next frame.
 */
#include <stdlib.h>
#include <string.h>

#include "vk_internal.h"

typedef struct vkb_buffer_upload
{
   VkBuffer staging;            /* in the staging arena */
   VkDeviceSize stagingOffset;
   unsigned buffer;
   unsigned offset;
   unsigned bytes;
} vkb_buffer_upload_t;

/* a side's queued copies into the static buffers */
typedef struct vkb_buffer_upload_set
{
   vkb_buffer_upload_t *uploads;
   unsigned count, capacity;
} vkb_buffer_upload_set_t;

static vkb_buffer_upload_set_t uploads[VKB_SIDE_COUNT];

/* the render thread's frees of blocks, kept apart from the main thread's
 * deferred list until the handoff */
static vkb_deferred_free_t renderDeferred[256];
static unsigned numRenderDeferred;

int vkb_buffers_init(void)
{
   memset(vkb.statics, 0, sizeof(vkb.statics));
   vkb.deferred = NULL;
   vkb.deferredCount = vkb.deferredCapacity = 0;
   memset(uploads, 0, sizeof(uploads));
   numRenderDeferred = 0;
   return 1;
}

static int vkb_buffer_upload_push(vkb_buffer_upload_set_t *set, const vkb_buffer_upload_t *u)
{
   if (set->count == set->capacity)
   {
      unsigned cap = set->capacity ? set->capacity * 2 : 256;
      vkb_buffer_upload_t *n = (vkb_buffer_upload_t *)realloc(set->uploads, cap * sizeof(*n));
      if (!n)
         return 0;
      set->uploads  = n;
      set->capacity = cap;
   }
   set->uploads[set->count++] = *u;
   return 1;
}

static int vkb_deferred_push(unsigned buffer, unsigned offset, size_t bytes)
{
   if (vkb.deferredCount == vkb.deferredCapacity)
   {
      unsigned cap = vkb.deferredCapacity ? vkb.deferredCapacity * 2 : 256;
      vkb_deferred_free_t *n = (vkb_deferred_free_t *)realloc(vkb.deferred, cap * sizeof(*n));
      if (!n)
         return 0;
      vkb.deferred         = n;
      vkb.deferredCapacity = cap;
   }
   vkb.deferred[vkb.deferredCount].buffer = buffer;
   vkb.deferred[vkb.deferredCount].offset = offset;
   vkb.deferred[vkb.deferredCount].size   = (unsigned)bytes;
   vkb.deferred[vkb.deferredCount].frame  = (unsigned)retro_atomic_load_relaxed_int(&vkb.frameCount);
   vkb.deferredCount++;
   return 1;
}

static void vkb_static_release(unsigned i)
{
   vkb_static_buffer_t *b = &vkb.statics[i];
   if (!b->used)
      return;
   if (b->buffer)
      vk.vkDestroyBuffer(vkb.device, b->buffer, NULL);
   vkb_mem_free(&b->mem);
   range_alloc_deinit(&b->ranges);
   memset(b, 0, sizeof(*b));
}

void vkb_buffers_shutdown(void)
{
   unsigned i;
   for (i = 0; i < VKB_SIDE_COUNT; i++)
      free(uploads[i].uploads);
   memset(uploads, 0, sizeof(uploads));
   numRenderDeferred = 0;
   for (i = 0; i < VKB_MAX_STATIC_BUFFERS; i++)
      vkb_static_release(i);
   free(vkb.deferred);
   vkb.deferred = NULL;
   vkb.deferredCount = vkb.deferredCapacity = 0;
}

VkBuffer vkb_static_vk_buffer(unsigned buffer)
{
   if (buffer == 0)
      return vkb.ring;
   if (buffer > VKB_MAX_STATIC_BUFFERS || !vkb.statics[buffer - 1].used)
      return VK_NULL_HANDLE;
   return vkb.statics[buffer - 1].buffer;
}

/* a new shared buffer, mapped when the device lets the CPU at its memory */
static int vkb_static_make(unsigned i, int indexes, size_t atLeast)
{
   vkb_static_buffer_t *b = &vkb.statics[i];
   VkBufferCreateInfo bci;
   VkMemoryRequirements req;
   void *mapped = NULL;
   size_t size = VKB_STATIC_BUFFER_BYTES;
   while (size < atLeast)
      size *= 2;
   memset(b, 0, sizeof(*b));
   memset(&bci, 0, sizeof(bci));
   bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bci.size  = size;
   bci.usage = (indexes ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
   if (vk.vkCreateBuffer(vkb.device, &bci, NULL, &b->buffer) != VK_SUCCESS)
      return 0;
   vk.vkGetBufferMemoryRequirements(vkb.device, b->buffer, &req);
   if (!vkb_mem_alloc(&req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1, &b->mem, &mapped)
         || vk.vkBindBufferMemory(vkb.device, b->buffer, b->mem.memory, b->mem.offset) != VK_SUCCESS)
   {
      vkb_mem_free(&b->mem);
      vk.vkDestroyBuffer(vkb.device, b->buffer, NULL);
      memset(b, 0, sizeof(*b));
      return 0;
   }
   b->mapped = (unsigned char *)mapped;
   if (!range_alloc_init(&b->ranges, (unsigned)size))
   {
      vkb_static_release(i);
      return 0;
   }
   b->indexes = (unsigned char)(indexes ? 1 : 0);
   b->used    = 1;
   return 1;
}

int vkb_static_alloc(size_t bytes, int indexes, unsigned *buffer, unsigned *offset)
{
   unsigned i;
   unsigned at;
   if (!vkb.ready || !bytes)
      return 0;
   if (vkb_side() == VKB_SIDE_RENDER)
   {
      /* the ranges are the main thread's */
      vkb.ownerViolations++;
      return 0;
   }
   for (i = 0; i < VKB_MAX_STATIC_BUFFERS; i++)
   {
      vkb_static_buffer_t *b = &vkb.statics[i];
      if (!b->used)
      {
         if (!vkb_static_make(i, indexes, bytes))
            break;
      }
      else if ((int)b->indexes != (indexes ? 1 : 0))
         continue;
      /* a vertex block starts at a multiple of every stride drawn: 240
       * is a multiple of idDrawVert's 60 and the shadow vertex's 16 */
      if (range_alloc_alloc(&b->ranges, (unsigned)bytes, indexes ? 4 : 240, &at))
      {
         *buffer = i + 1;
         *offset = at;
         return 1;
      }
   }
   return 0;
}

void vkb_static_upload(unsigned buffer, unsigned offset, const void *data, size_t bytes)
{
   vkb_static_buffer_t *b;
   if (!buffer || buffer > VKB_MAX_STATIC_BUFFERS || !vkb.statics[buffer - 1].used || !data || !bytes)
      return;
   b = &vkb.statics[buffer - 1];
   if (b->mapped)
   {
      memcpy(b->mapped + offset, data, bytes);
      vkb_mem_flush(&b->mem, offset, bytes);
      return;
   }
   /* through the caller's side's staging arena, copied by the frame's
    * flush: the render thread's at once in its frame, the main thread's
    * at the next frame's start */
   {
      const enum vkb_side side = vkb_side();
      vkb_buffer_upload_t u;
      unsigned char *p;
      memset(&u, 0, sizeof(u));
      p = vkb_staging_alloc(side, bytes, &u.staging, &u.stagingOffset);
      if (!p)
         return;
      memcpy(p, data, bytes);
      u.buffer = buffer;
      u.offset = offset;
      u.bytes  = (unsigned)bytes;
      if (!vkb_buffer_upload_push(&uploads[side], &u))
         return;
      /* a load between frames that has filled the arena goes to the
       * device now rather than growing it */
      if (side == VKB_SIDE_MAIN && !retro_atomic_load_acquire_int(&vkb.frameOpen)
            && vkb_staging_should_flush(side))
         vkb_transfer_now();
   }
}

void vkb_static_free(unsigned buffer, unsigned offset, size_t bytes)
{
   if (!buffer || buffer > VKB_MAX_STATIC_BUFFERS || !vkb.statics[buffer - 1].used)
      return;
   if (vkb_side() == VKB_SIDE_RENDER)
   {
      if (numRenderDeferred == sizeof(renderDeferred) / sizeof(renderDeferred[0]))
      {
         vkb.ownerViolations++;
         return;    /* the range is lost until the buffer goes */
      }
      renderDeferred[numRenderDeferred].buffer = buffer;
      renderDeferred[numRenderDeferred].offset = offset;
      renderDeferred[numRenderDeferred].size   = (unsigned)bytes;
      renderDeferred[numRenderDeferred].frame  = (unsigned)retro_atomic_load_relaxed_int(&vkb.frameCount);
      numRenderDeferred++;
      return;
   }
   if (!vkb_deferred_push(buffer, offset, bytes))
   {
      vk.vkDeviceWaitIdle(vkb.device);
      range_alloc_free(&vkb.statics[buffer - 1].ranges, offset, (unsigned)bytes);
   }
}

/* The main thread between frames: the render thread's frees join the
 * deferred list, the frees the frames in flight have let go are given
 * back, the main side's queued copies and the chunks holding them go
 * to the render side for the frame about to record. */
void vkb_buffers_handoff(void)
{
   unsigned i;
   unsigned frameCount = (unsigned)retro_atomic_load_relaxed_int(&vkb.frameCount);
   for (i = 0; i < numRenderDeferred; i++)
      if (!vkb_deferred_push(renderDeferred[i].buffer, renderDeferred[i].offset, renderDeferred[i].size))
         break;
   numRenderDeferred = 0;
   for (i = 0; i < vkb.deferredCount; )
   {
      if (frameCount >= vkb.deferred[i].frame + VKB_FRAMES)
      {
         vkb_deferred_free_t *d = &vkb.deferred[i];
         if (d->buffer && d->buffer <= VKB_MAX_STATIC_BUFFERS && vkb.statics[d->buffer - 1].used)
            range_alloc_free(&vkb.statics[d->buffer - 1].ranges, d->offset, d->size);
         vkb.deferred[i] = vkb.deferred[--vkb.deferredCount];
      }
      else
         i++;
   }
   if (uploads[VKB_SIDE_MAIN].count)
   {
      vkb_buffer_upload_set_t *from = &uploads[VKB_SIDE_MAIN];
      vkb_buffer_upload_set_t *to   = &uploads[VKB_SIDE_RENDER];
      if (!to->count)
      {
         vkb_buffer_upload_set_t t = *to;
         *to   = *from;
         *from = t;
      }
      else
      {
         for (i = 0; i < from->count; i++)
            vkb_buffer_upload_push(to, &from->uploads[i]);
         from->count = 0;
      }
      /* their chunks are copied from in the frame about to record */
      vkb_staging_flush_memory(VKB_SIDE_MAIN);
      vkb_staging_retire(VKB_SIDE_MAIN, 0);
   }
   vkb_staging_frame_done(VKB_SIDE_MAIN);
}

int vkb_buffers_pending(enum vkb_side side)
{
   return uploads[side].count != 0;
}

/* the side's queued copies into a command buffer; the render side's
 * chunks retire with the frame, the main side's already did at the
 * handoff or do at once after a waited submit */
void vkb_buffers_flush(VkCommandBuffer cmd, enum vkb_side side)
{
   vkb_buffer_upload_set_t *set = &uploads[side];
   unsigned i;
   if (!set->count)
      return;
   if (side == VKB_SIDE_RENDER)
      vkb_staging_flush_memory(side);
   for (i = 0; i < set->count; i++)
   {
      vkb_buffer_upload_t *u = &set->uploads[i];
      VkBufferCopy region;
      region.srcOffset = u->stagingOffset;
      region.dstOffset = u->offset;
      region.size      = u->bytes;
      vk.vkCmdCopyBuffer(cmd, u->staging, vkb.statics[u->buffer - 1].buffer, 1, &region);
   }
   set->count = 0;
   if (side == VKB_SIDE_RENDER)
      vkb_staging_retire(side, 0);
   {
      /* the copies land before the vertex fetch */
      VkMemoryBarrier mb;
      memset(&mb, 0, sizeof(mb));
      mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
      mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
      vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            0, 1, &mb, 0, NULL, 0, NULL);
   }
}
