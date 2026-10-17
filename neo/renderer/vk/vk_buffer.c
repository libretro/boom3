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

static vkb_buffer_upload_t *uploads;
static unsigned numUploads, uploadCapacity;

int vkb_buffers_init(void)
{
   memset(vkb.statics, 0, sizeof(vkb.statics));
   vkb.deferred = NULL;
   vkb.deferredCount = vkb.deferredCapacity = 0;
   numUploads = 0;
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
   free(uploads);
   uploads = NULL;
   numUploads = uploadCapacity = 0;
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
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
   vkt_lock();
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
         vkt_unlock();
         return 1;
      }
   }
   vkt_unlock();
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
   /* through the staging arena, copied ahead of the next frame */
   {
      vkb_buffer_upload_t *u;
      unsigned char *p;
      int flush;
      vkt_lock();
      if (numUploads == uploadCapacity)
      {
         unsigned cap = uploadCapacity ? uploadCapacity * 2 : 256;
         vkb_buffer_upload_t *n = (vkb_buffer_upload_t *)realloc(uploads, cap * sizeof(*n));
         if (!n)
         {
            vkt_unlock();
            return;
         }
         uploads        = n;
         uploadCapacity = cap;
      }
      u = &uploads[numUploads];
      memset(u, 0, sizeof(*u));
      p = vkb_staging_alloc(bytes, &u->staging, &u->stagingOffset);
      if (!p)
      {
         vkt_unlock();
         return;
      }
      memcpy(p, data, bytes);
      u->buffer = buffer;
      u->offset = offset;
      u->bytes  = (unsigned)bytes;
      numUploads++;
      flush = !vkb.frameOpen && vkb_staging_should_flush();
      vkt_unlock();
      if (flush)
         vkb_transfer_now();
   }
}

void vkb_static_free(unsigned buffer, unsigned offset, size_t bytes)
{
   if (!buffer || buffer > VKB_MAX_STATIC_BUFFERS || !vkb.statics[buffer - 1].used)
      return;
   vkt_lock();
   if (vkb.deferredCount == vkb.deferredCapacity)
   {
      unsigned cap = vkb.deferredCapacity ? vkb.deferredCapacity * 2 : 256;
      vkb_deferred_free_t *n = (vkb_deferred_free_t *)realloc(vkb.deferred, cap * sizeof(*n));
      if (!n)
      {
         vk.vkDeviceWaitIdle(vkb.device);
         range_alloc_free(&vkb.statics[buffer - 1].ranges, offset, (unsigned)bytes);
         vkt_unlock();
         return;
      }
      vkb.deferred         = n;
      vkb.deferredCapacity = cap;
   }
   vkb.deferred[vkb.deferredCount].buffer = buffer;
   vkb.deferred[vkb.deferredCount].offset = offset;
   vkb.deferred[vkb.deferredCount].size   = (unsigned)bytes;
   vkb.deferred[vkb.deferredCount].frame  = vkb.frameCount;
   vkb.deferredCount++;
   vkt_unlock();
}

/* At a frame's start: the frees the frames in flight have let go, and
 * the staging chunks they are done with. */
void vkb_buffers_frame_begin(unsigned frameIndex)
{
   unsigned i;
   (void)frameIndex;
   vkt_lock();
   for (i = 0; i < vkb.deferredCount; )
   {
      if (vkb.frameCount >= vkb.deferred[i].frame + VKB_FRAMES)
      {
         vkb_deferred_free_t *d = &vkb.deferred[i];
         if (d->buffer && d->buffer <= VKB_MAX_STATIC_BUFFERS && vkb.statics[d->buffer - 1].used)
            range_alloc_free(&vkb.statics[d->buffer - 1].ranges, d->offset, d->size);
         vkb.deferred[i] = vkb.deferred[--vkb.deferredCount];
      }
      else
         i++;
   }
   vkb_staging_frame_done();
   vkt_unlock();
}

int vkb_buffers_pending(void)
{
   return numUploads != 0;
}

void vkb_buffers_flush(VkCommandBuffer cmd)
{
   vkt_lock();
   vkb_buffers_flush_locked(cmd);
   vkt_unlock();
}

void vkb_buffers_flush_locked(VkCommandBuffer cmd)
{
   unsigned i;
   if (!numUploads)
      return;
   vkb_staging_flush_memory();
   for (i = 0; i < numUploads; i++)
   {
      vkb_buffer_upload_t *u = &uploads[i];
      VkBufferCopy region;
      region.srcOffset = u->stagingOffset;
      region.dstOffset = u->offset;
      region.size      = u->bytes;
      vk.vkCmdCopyBuffer(cmd, u->staging, vkb.statics[u->buffer - 1].buffer, 1, &region);
   }
   numUploads = 0;
   vkb_staging_retire(0);
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
