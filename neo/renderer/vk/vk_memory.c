/*
 * vk_memory: device memory in large blocks the images and buffers are
 * carved from (a device allows a few thousand allocations in all, far
 * fewer than the engine's images), the staging arena the uploads are
 * written into, and the transfer submit that runs the queued uploads
 * between frames.
 */
#include <stdlib.h>
#include <string.h>

#include "vk_internal.h"

#define VKB_MEM_BLOCK_BYTES (64u * 1024u * 1024u)
#define VKB_MEM_MAX_BLOCKS 256
#define VKB_STAGING_FLUSH_BYTES (64u * 1024u * 1024u)
#define VKB_STAGING_SPARE_CHUNKS 2

typedef struct vkb_mem_range
{
   VkDeviceSize offset;
   VkDeviceSize size;
} vkb_mem_range_t;

typedef struct vkb_mem_block
{
   VkDeviceMemory memory;
   unsigned char *mapped;
   VkDeviceSize size;
   VkDeviceSize free;
   vkb_mem_range_t *ranges;      /* the free ranges, by offset */
   unsigned numRanges;
   unsigned rangeCapacity;
   unsigned type;
   unsigned char linear;         /* buffers; images take blocks of their own */
   unsigned char dedicated;      /* one resource, freed with it */
   unsigned char used;
} vkb_mem_block_t;

static vkb_mem_block_t blocks[VKB_MEM_MAX_BLOCKS];

/* --- blocks ------------------------------------------------------------------ */

static int vkb_mem_block_make(unsigned i, unsigned type, VkDeviceSize size, int linear, int dedicated)
{
   vkb_mem_block_t *b = &blocks[i];
   VkMemoryAllocateInfo mai;
   memset(b, 0, sizeof(*b));
   memset(&mai, 0, sizeof(mai));
   mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mai.allocationSize  = size;
   mai.memoryTypeIndex = type;
   if (vk.vkAllocateMemory(vkb.device, &mai, NULL, &b->memory) != VK_SUCCESS)
      return 0;
   if (vkb.memoryProperties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
   {
      void *p = NULL;
      if (vk.vkMapMemory(vkb.device, b->memory, 0, size, 0, &p) != VK_SUCCESS)
      {
         vk.vkFreeMemory(vkb.device, b->memory, NULL);
         b->memory = VK_NULL_HANDLE;
         return 0;
      }
      b->mapped = (unsigned char *)p;
   }
   b->ranges = (vkb_mem_range_t *)malloc(16 * sizeof(*b->ranges));
   if (!b->ranges)
   {
      if (b->mapped)
         vk.vkUnmapMemory(vkb.device, b->memory);
      vk.vkFreeMemory(vkb.device, b->memory, NULL);
      b->memory = VK_NULL_HANDLE;
      return 0;
   }
   b->rangeCapacity    = 16;
   b->numRanges        = 1;
   b->ranges[0].offset = 0;
   b->ranges[0].size   = size;
   b->size             = size;
   b->free             = size;
   b->type             = type;
   b->linear           = (unsigned char)(linear ? 1 : 0);
   b->dedicated        = (unsigned char)(dedicated ? 1 : 0);
   b->used             = 1;
   return 1;
}

static void vkb_mem_block_release(unsigned i)
{
   vkb_mem_block_t *b = &blocks[i];
   if (!b->used)
      return;
   if (b->mapped)
      vk.vkUnmapMemory(vkb.device, b->memory);
   vk.vkFreeMemory(vkb.device, b->memory, NULL);
   free(b->ranges);
   memset(b, 0, sizeof(*b));
}

static int vkb_mem_range_insert(vkb_mem_block_t *b, unsigned at, VkDeviceSize offset, VkDeviceSize size)
{
   if (b->numRanges == b->rangeCapacity)
   {
      unsigned cap = b->rangeCapacity * 2;
      vkb_mem_range_t *n = (vkb_mem_range_t *)realloc(b->ranges, cap * sizeof(*n));
      if (!n)
         return 0;
      b->ranges        = n;
      b->rangeCapacity = cap;
   }
   memmove(&b->ranges[at + 1], &b->ranges[at], (b->numRanges - at) * sizeof(*b->ranges));
   b->ranges[at].offset = offset;
   b->ranges[at].size   = size;
   b->numRanges++;
   return 1;
}

/* first fit in the block's free ranges */
static int vkb_mem_block_alloc(vkb_mem_block_t *b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *offset)
{
   unsigned i;
   if (b->free < size)
      return 0;
   for (i = 0; i < b->numRanges; i++)
   {
      vkb_mem_range_t *r = &b->ranges[i];
      VkDeviceSize at = (r->offset + align - 1) / align * align;
      VkDeviceSize end = r->offset + r->size;
      VkDeviceSize before, after;
      if (at + size > end)
         continue;
      before = at - r->offset;
      after  = end - (at + size);
      if (before && after)
      {
         if (!vkb_mem_range_insert(b, i + 1, at + size, after))
            return 0;
         b->ranges[i].size = before;
      }
      else if (before)
         r->size = before;
      else if (after)
      {
         r->offset = at + size;
         r->size   = after;
      }
      else
      {
         memmove(&b->ranges[i], &b->ranges[i + 1], (b->numRanges - i - 1) * sizeof(*b->ranges));
         b->numRanges--;
      }
      b->free -= size;
      *offset = at;
      return 1;
   }
   return 0;
}

static void vkb_mem_block_free(vkb_mem_block_t *b, VkDeviceSize offset, VkDeviceSize size)
{
   unsigned i;
   for (i = 0; i < b->numRanges && b->ranges[i].offset < offset; i++)
      ;
   /* merge with the range before, the range after, or both */
   if (i > 0 && b->ranges[i - 1].offset + b->ranges[i - 1].size == offset)
   {
      b->ranges[i - 1].size += size;
      if (i < b->numRanges && b->ranges[i].offset == offset + size)
      {
         b->ranges[i - 1].size += b->ranges[i].size;
         memmove(&b->ranges[i], &b->ranges[i + 1], (b->numRanges - i - 1) * sizeof(*b->ranges));
         b->numRanges--;
      }
   }
   else if (i < b->numRanges && b->ranges[i].offset == offset + size)
   {
      b->ranges[i].offset = offset;
      b->ranges[i].size  += size;
   }
   else if (!vkb_mem_range_insert(b, i, offset, size))
      return;    /* the range is lost to the block until it goes */
   b->free += size;
}

static int vkb_mem_type_for(const VkMemoryRequirements *req, VkMemoryPropertyFlags want)
{
   int type = vkb_memory_type(req->memoryTypeBits, want, 0);
   if (type < 0 && (want & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      type = vkb_memory_type(req->memoryTypeBits, want & ~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
   if (type < 0 && (want & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (want & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
      type = vkb_memory_type(req->memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
   if (type < 0)
      type = vkb_memory_type(req->memoryTypeBits, 0, 0);
   return type;
}

int vkb_mem_alloc(const VkMemoryRequirements *req, VkMemoryPropertyFlags want, int linear,
      vkb_mem_t *out, void **mapped)
{
   int type = vkb_mem_type_for(req, want);
   VkDeviceSize align = req->alignment ? req->alignment : 1;
   unsigned i, slot = VKB_MEM_MAX_BLOCKS;
   int dedicated;
   memset(out, 0, sizeof(*out));
   if (mapped)
      *mapped = NULL;
   if (type < 0)
      return 0;
   if ((want & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
         && !(vkb.memoryProperties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
      want &= ~VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
   dedicated = req->size > VKB_MEM_BLOCK_BYTES / 4;
   if (!dedicated)
      for (i = 0; i < VKB_MEM_MAX_BLOCKS; i++)
      {
         vkb_mem_block_t *b = &blocks[i];
         VkDeviceSize offset;
         if (!b->used)
         {
            if (slot == VKB_MEM_MAX_BLOCKS)
               slot = i;
            continue;
         }
         if (b->dedicated || b->type != (unsigned)type || (int)b->linear != (linear ? 1 : 0))
            continue;
         if (vkb_mem_block_alloc(b, req->size, align, &offset))
         {
            out->memory = b->memory;
            out->offset = offset;
            out->size   = req->size;
            out->block  = (unsigned short)i;
            out->valid  = 1;
            if (mapped && b->mapped)
               *mapped = b->mapped + offset;
            return 1;
         }
      }
   for (i = 0; slot == VKB_MEM_MAX_BLOCKS && i < VKB_MEM_MAX_BLOCKS; i++)
      if (!blocks[i].used)
         slot = i;
   if (slot == VKB_MEM_MAX_BLOCKS)
      return 0;
   if (!vkb_mem_block_make(slot, (unsigned)type, dedicated ? req->size : VKB_MEM_BLOCK_BYTES, linear, dedicated))
   {
      /* a type without room: the next type that fits */
      if (!(want & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (want & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      {
         int other = vkb_memory_type(req->memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
         if (other < 0 || other == type
               || !vkb_mem_block_make(slot, (unsigned)other, dedicated ? req->size : VKB_MEM_BLOCK_BYTES,
                  linear, dedicated))
            return 0;
      }
      else
         return 0;
   }
   {
      vkb_mem_block_t *b = &blocks[slot];
      VkDeviceSize offset;
      if (!vkb_mem_block_alloc(b, req->size, align, &offset))
      {
         vkb_mem_block_release(slot);
         return 0;
      }
      out->memory = b->memory;
      out->offset = offset;
      out->size   = req->size;
      out->block  = (unsigned short)slot;
      out->valid  = 1;
      if (mapped && b->mapped)
         *mapped = b->mapped + offset;
   }
   return 1;
}

void vkb_mem_free(const vkb_mem_t *m)
{
   vkb_mem_block_t *b;
   if (!m->valid || m->block >= VKB_MEM_MAX_BLOCKS || !blocks[m->block].used)
      return;
   b = &blocks[m->block];
   vkb_mem_block_free(b, m->offset, m->size);
   if (b->free != b->size)
      return;
   if (b->dedicated)
   {
      vkb_mem_block_release(m->block);
      return;
   }
   /* one empty block of a kind stays for the next map's textures */
   {
      unsigned i;
      for (i = 0; i < VKB_MEM_MAX_BLOCKS; i++)
         if (i != m->block && blocks[i].used && !blocks[i].dedicated && blocks[i].type == b->type
               && blocks[i].linear == b->linear && blocks[i].free == blocks[i].size)
         {
            vkb_mem_block_release(m->block);
            return;
         }
   }
}

int vkb_mem_coherent(const vkb_mem_t *m)
{
   if (!m->valid || m->block >= VKB_MEM_MAX_BLOCKS)
      return 1;
   return (vkb.memoryProperties.memoryTypes[blocks[m->block].type].propertyFlags
         & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

void vkb_mem_flush(const vkb_mem_t *m, VkDeviceSize offset, VkDeviceSize bytes)
{
   VkMappedMemoryRange range;
   VkDeviceSize atom = vkb.properties.limits.nonCoherentAtomSize;
   VkDeviceSize from, to;
   if (!m->valid || vkb_mem_coherent(m))
      return;
   if (!atom)
      atom = 1;
   from = (m->offset + offset) / atom * atom;
   to   = (m->offset + offset + bytes + atom - 1) / atom * atom;
   if (to > blocks[m->block].size)
      to = blocks[m->block].size;
   memset(&range, 0, sizeof(range));
   range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
   range.memory = m->memory;
   range.offset = from;
   range.size   = to - from;
   vk.vkFlushMappedMemoryRanges(vkb.device, 1, &range);
}

/* --- the staging arena ----------------------------------------------------------- */

typedef struct vkb_staging_chunk
{
   VkBuffer buffer;
   vkb_mem_t mem;
   unsigned char *mapped;
   VkDeviceSize size;
   VkDeviceSize used;
   unsigned retireFrame;
   unsigned char pending;        /* holds data a flush has yet to copy */
   unsigned char retired;        /* copied; free after retireFrame's frames */
   unsigned char immediate;      /* copied by a waited submit: free now */
} vkb_staging_chunk_t;

static vkb_staging_chunk_t *chunks;
static unsigned numChunks, chunkCapacity;
static VkDeviceSize stagingPending;

static void vkb_staging_chunk_release(unsigned i)
{
   vkb_staging_chunk_t *c = &chunks[i];
   if (c->buffer)
      vk.vkDestroyBuffer(vkb.device, c->buffer, NULL);
   vkb_mem_free(&c->mem);
   chunks[i] = chunks[--numChunks];
}

static int vkb_staging_chunk_make(VkDeviceSize size)
{
   vkb_staging_chunk_t *c;
   VkBufferCreateInfo bci;
   VkMemoryRequirements req;
   void *mapped = NULL;
   if (numChunks == chunkCapacity)
   {
      unsigned cap = chunkCapacity ? chunkCapacity * 2 : 8;
      vkb_staging_chunk_t *n = (vkb_staging_chunk_t *)realloc(chunks, cap * sizeof(*n));
      if (!n)
         return 0;
      chunks        = n;
      chunkCapacity = cap;
   }
   c = &chunks[numChunks];
   memset(c, 0, sizeof(*c));
   memset(&bci, 0, sizeof(bci));
   bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bci.size  = size;
   bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
   if (vk.vkCreateBuffer(vkb.device, &bci, NULL, &c->buffer) != VK_SUCCESS)
      return 0;
   vk.vkGetBufferMemoryRequirements(vkb.device, c->buffer, &req);
   if (!vkb_mem_alloc(&req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1,
            &c->mem, &mapped) || !mapped
         || vk.vkBindBufferMemory(vkb.device, c->buffer, c->mem.memory, c->mem.offset) != VK_SUCCESS)
   {
      vk.vkDestroyBuffer(vkb.device, c->buffer, NULL);
      vkb_mem_free(&c->mem);
      return 0;
   }
   c->mapped = (unsigned char *)mapped;
   c->size   = size;
   numChunks++;
   return 1;
}

unsigned char *vkb_staging_alloc(size_t bytes, VkBuffer *buffer, VkDeviceSize *offset)
{
   unsigned i;
   VkDeviceSize need = (bytes + 15) / 16 * 16;
   for (i = 0; i < numChunks; i++)
   {
      vkb_staging_chunk_t *c = &chunks[i];
      if (c->retired)
         continue;
      if (c->used + need > c->size)
         continue;
      *buffer = c->buffer;
      *offset = c->used;
      c->used += need;
      c->pending = 1;
      stagingPending += need;
      return c->mapped + *offset;
   }
   if (!vkb_staging_chunk_make(need > VKB_STAGING_BYTES ? need : VKB_STAGING_BYTES))
      return NULL;
   {
      vkb_staging_chunk_t *c = &chunks[numChunks - 1];
      *buffer = c->buffer;
      *offset = 0;
      c->used = need;
      c->pending = 1;
      stagingPending += need;
      return c->mapped;
   }
}

void vkb_staging_flush_memory(void)
{
   unsigned i;
   for (i = 0; i < numChunks; i++)
      if (chunks[i].pending && !chunks[i].retired)
         vkb_mem_flush(&chunks[i].mem, 0, chunks[i].used);
}

/* the chunks a flush copied from, out of use once its frame is done, or
 * at once after a waited submit */
void vkb_staging_retire(int immediate)
{
   unsigned i;
   for (i = 0; i < numChunks; i++)
   {
      vkb_staging_chunk_t *c = &chunks[i];
      if (!c->pending || c->retired)
         continue;
      c->retired     = 1;
      c->immediate   = (unsigned char)(immediate ? 1 : 0);
      c->retireFrame = vkb.frameCount;
   }
   stagingPending = 0;
}

void vkb_staging_frame_done(void)
{
   unsigned i, spare = 0;
   for (i = 0; i < numChunks; )
   {
      vkb_staging_chunk_t *c = &chunks[i];
      if (c->retired && (c->immediate || vkb.frameCount >= c->retireFrame + VKB_FRAMES))
      {
         c->retired = c->pending = c->immediate = 0;
         c->used = 0;
      }
      if (!c->pending && !c->retired)
      {
         if (spare == VKB_STAGING_SPARE_CHUNKS || c->size != VKB_STAGING_BYTES)
         {
            vkb_staging_chunk_release(i);
            continue;
         }
         spare++;
      }
      i++;
   }
}

size_t vkb_staging_pending(void)
{
   return (size_t)stagingPending;
}

int vkb_staging_should_flush(void)
{
   return stagingPending >= VKB_STAGING_FLUSH_BYTES;
}

/* --- the transfer submit between frames ----------------------------------------- */

/* The queued uploads through a command buffer of their own, waited for:
 * what a load between frames needs when the staging arena has grown
 * past its limit, and what the shutdown needs. */
void vkb_transfer_now(void)
{
   VkCommandBufferBeginInfo bi;
   VkSubmitInfo si;
   if (!vkb.ready || !vkb.uploadPool)
      return;
   vkt_lock();
   if (!vkb.uploadCount && !vkb_buffers_pending())
   {
      vkt_unlock();
      return;
   }
   vk.vkResetCommandPool(vkb.device, vkb.uploadPool, 0);
   memset(&bi, 0, sizeof(bi));
   bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   vk.vkBeginCommandBuffer(vkb.uploadCmd, &bi);
   vkb_images_flush_uploads_locked(vkb.uploadCmd);
   vkb_buffers_flush_locked(vkb.uploadCmd);
   vk.vkEndCommandBuffer(vkb.uploadCmd);
   memset(&si, 0, sizeof(si));
   si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.commandBufferCount = 1;
   si.pCommandBuffers    = &vkb.uploadCmd;
   vk.vkResetFences(vkb.device, 1, &vkb.uploadFence);
   vkb.iface->lock_queue(vkb.iface->handle);
   vk.vkQueueSubmit(vkb.queue, 1, &si, vkb.uploadFence);
   vkb.iface->unlock_queue(vkb.iface->handle);
   vk.vkWaitForFences(vkb.device, 1, &vkb.uploadFence, VK_TRUE, (uint64_t)-1);
   vkb_staging_retire(1);
   vkb_staging_frame_done();
   vkt_unlock();
}

int vkb_memory_init(void)
{
   VkCommandPoolCreateInfo pci;
   VkCommandBufferAllocateInfo cai;
   VkFenceCreateInfo fci;
   memset(blocks, 0, sizeof(blocks));
   chunks = NULL;
   numChunks = chunkCapacity = 0;
   stagingPending = 0;
   memset(&pci, 0, sizeof(pci));
   pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
   pci.queueFamilyIndex = vkb.queueFamily;
   if (vk.vkCreateCommandPool(vkb.device, &pci, NULL, &vkb.uploadPool) != VK_SUCCESS)
      return 0;
   memset(&cai, 0, sizeof(cai));
   cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cai.commandPool        = vkb.uploadPool;
   cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cai.commandBufferCount = 1;
   if (vk.vkAllocateCommandBuffers(vkb.device, &cai, &vkb.uploadCmd) != VK_SUCCESS)
      return 0;
   memset(&fci, 0, sizeof(fci));
   fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
   if (vk.vkCreateFence(vkb.device, &fci, NULL, &vkb.uploadFence) != VK_SUCCESS)
      return 0;
   return 1;
}

/* after everything that held memory is gone */
void vkb_memory_shutdown(void)
{
   unsigned i;
   while (numChunks)
      vkb_staging_chunk_release(numChunks - 1);
   free(chunks);
   chunks = NULL;
   chunkCapacity = 0;
   stagingPending = 0;
   for (i = 0; i < VKB_MEM_MAX_BLOCKS; i++)
      vkb_mem_block_release(i);
   if (vkb.uploadFence)
      vk.vkDestroyFence(vkb.device, vkb.uploadFence, NULL);
   if (vkb.uploadPool)
      vk.vkDestroyCommandPool(vkb.device, vkb.uploadPool, NULL);
   vkb.uploadFence = VK_NULL_HANDLE;
   vkb.uploadPool  = VK_NULL_HANDLE;
   vkb.uploadCmd   = VK_NULL_HANDLE;
}
