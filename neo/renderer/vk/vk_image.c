/*
 * vk_image: textures, samplers, their uploads, and the render targets.
 *
 * A texture is created in device memory with optimal tiling; its data
 * takes the one copy through a staging buffer that tiling makes
 * unavoidable, recorded ahead of the next frame's drawing.  Image ids 1
 * and 2 wrap the scene target's colour and the open target's depth for
 * the composite and the stages that read the scene; 3 the scene's depth
 * while a side target draws.
 */
#include <stdlib.h>
#include <string.h>

#include "vk_internal.h"

#define VKB_IMAGE_SCENE 1
#define VKB_IMAGE_TARGET_DEPTH 2
#define VKB_IMAGE_SCENE_DEPTH 3
#define VKB_IMAGE_FIRST 4

VkFormat vkb_vk_format(enum vkb_format format)
{
   switch (format)
   {
      case VKB_FORMAT_RGBA8:      return VK_FORMAT_R8G8B8A8_UNORM;
      case VKB_FORMAT_BGRA8:      return VK_FORMAT_B8G8R8A8_UNORM;
      case VKB_FORMAT_A8:         return VK_FORMAT_R8_UNORM;
      case VKB_FORMAT_I16:        return VK_FORMAT_R16_UNORM;
      case VKB_FORMAT_LA16:       return VK_FORMAT_R16G16_UNORM;
      case VKB_FORMAT_BC1:        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
      case VKB_FORMAT_BC2:        return VK_FORMAT_BC2_UNORM_BLOCK;
      case VKB_FORMAT_BC3:        return VK_FORMAT_BC3_UNORM_BLOCK;
      case VKB_FORMAT_BC7:        return VK_FORMAT_BC7_UNORM_BLOCK;
      case VKB_FORMAT_RGBA16F:    return VK_FORMAT_R16G16B16A16_SFLOAT;
      case VKB_FORMAT_RGB10A2:    return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
      case VKB_FORMAT_RGBA32F:    return VK_FORMAT_R32G32B32A32_SFLOAT;
      case VKB_FORMAT_RGBA16:     return VK_FORMAT_R16G16B16A16_UNORM;
      case VKB_FORMAT_DEPTH:      return vkb_depth_stencil_format();
      case VKB_FORMAT_RG32F:      return VK_FORMAT_R32G32_SFLOAT;
      default:                    return VK_FORMAT_UNDEFINED;
   }
}

static VkFormat depthFormat;

/* the depth-stencil format the device takes, D24S8 first */
VkFormat vkb_depth_stencil_format(void)
{
   static const VkFormat candidates[3] = {
      VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT
   };
   unsigned i;
   if (depthFormat)
      return depthFormat;
   for (i = 0; i < 3; i++)
   {
      VkFormatProperties fp;
      vk.vkGetPhysicalDeviceFormatProperties(vkb.gpu, candidates[i], &fp);
      if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
      {
         depthFormat = candidates[i];
         return depthFormat;
      }
   }
   depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
   return depthFormat;
}

/* --- samplers ---------------------------------------------------------------- */

VkSampler vkb_sampler(enum vkb_filter filter, enum vkb_wrap wrap, int anisotropy, int mips)
{
   VkSampler *slot;
   VkSamplerCreateInfo sci;
   int f = (int)filter, w = (int)wrap, a = anisotropy && vkb.features.samplerAnisotropy ? 1 : 0;
   if (!mips && (filter == VKB_FILTER_LINEAR || filter == VKB_FILTER_LINEAR_MIP_NEAREST))
      f = VKB_FILTER_LINEAR_NOMIP;
   slot = &vkb.samplers[f][w][a];
   if (*slot)
      return *slot;
   memset(&sci, 0, sizeof(sci));
   sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   sci.magFilter    = f == VKB_FILTER_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
   sci.minFilter    = sci.magFilter;
   sci.mipmapMode   = f == VKB_FILTER_NEAREST || f == VKB_FILTER_LINEAR_MIP_NEAREST
      ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
   sci.addressModeU = w == VKB_WRAP_REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT
      : w == VKB_WRAP_CLAMP ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
   sci.addressModeV = sci.addressModeU;
   sci.addressModeW = sci.addressModeU;
   sci.anisotropyEnable = a ? VK_TRUE : VK_FALSE;
   sci.maxAnisotropy    = a ? (vkb.properties.limits.maxSamplerAnisotropy < 8.0f
         ? vkb.properties.limits.maxSamplerAnisotropy : 8.0f) : 1.0f;
   sci.maxLod       = f == VKB_FILTER_LINEAR_NOMIP ? 0.0f : VK_LOD_CLAMP_NONE;
   sci.borderColor  = w == VKB_WRAP_CLAMP_TO_ZERO_ALPHA
      ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
   if (vk.vkCreateSampler(vkb.device, &sci, NULL, slot) != VK_SUCCESS)
      *slot = VK_NULL_HANDLE;
   return *slot;
}

/* --- textures ------------------------------------------------------------------ */

int vkb_images_init(void)
{
   memset(vkb.images, 0, sizeof(vkb.images));
   vkb.images[VKB_IMAGE_SCENE].used       = 1;
   vkb.images[VKB_IMAGE_TARGET_DEPTH].used = 1;
   vkb.images[VKB_IMAGE_SCENE_DEPTH].used = 1;
   memset(vkb.uploads, 0, sizeof(vkb.uploads));
   depthFormat = 0;
   return 1;
}

static void vkb_image_release(vkb_image_t *im)
{
   if (im->framebuffer)
      vk.vkDestroyFramebuffer(vkb.device, im->framebuffer, NULL);
   if (im->view)
      vk.vkDestroyImageView(vkb.device, im->view, NULL);
   if (im->image)
      vk.vkDestroyImage(vkb.device, im->image, NULL);
   vkb_mem_free(&im->mem);
   memset(im, 0, sizeof(*im));
}

static void vkb_images_release_graves(void);

void vkb_images_shutdown(void)
{
   unsigned i, f, w, a;
   for (i = 0; i < VKB_SIDE_COUNT; i++)
      free(vkb.uploads[i].uploads);
   memset(vkb.uploads, 0, sizeof(vkb.uploads));
   vkb_images_release_graves();
   for (i = VKB_IMAGE_FIRST; i < VKB_MAX_IMAGES; i++)
      if (vkb.images[i].used)
         vkb_image_release(&vkb.images[i]);
   for (f = 0; f < VKB_FILTER_COUNT; f++)
      for (w = 0; w < 4; w++)
         for (a = 0; a < 2; a++)
            if (vkb.samplers[f][w][a])
            {
               vk.vkDestroySampler(vkb.device, vkb.samplers[f][w][a], NULL);
               vkb.samplers[f][w][a] = VK_NULL_HANDLE;
            }
}

const vkb_image_t *vkb_image_get(unsigned image)
{
   if (image >= VKB_MAX_IMAGES || !vkb.images[image].used)
      return NULL;
   if (image == VKB_IMAGE_SCENE)
   {
      vkb_image_t *im = &vkb.images[image];
      const vkb_target_t *t = vkb.sceneTarget ? vkb.sceneTarget : &vkb.scene;
      im->image   = t->color;
      im->view    = t->colorView;
      im->width   = t->width;
      im->height  = t->height;
      im->levels  = 1;
      im->sampler = vkb_sampler(VKB_FILTER_LINEAR_NOMIP, VKB_WRAP_CLAMP, 0, 0);
   }
   else if (image == VKB_IMAGE_TARGET_DEPTH)
   {
      /* the open target's depth, sampled in the read-only-depth pass */
      vkb_image_t *im = &vkb.images[image];
      const vkb_target_t *t = vkb.target ? vkb.target : &vkb.scene;
      im->image   = t->depth;
      im->view    = t->depthView;
      im->width   = t->width;
      im->height  = t->height;
      im->levels  = 1;
      im->sampler = vkb_sampler(VKB_FILTER_NEAREST, VKB_WRAP_CLAMP, 0, 0);
   }
   else if (image == VKB_IMAGE_SCENE_DEPTH)
   {
      /* the scene's depth, read-only while a side target draws */
      vkb_image_t *im = &vkb.images[image];
      const vkb_target_t *t = vkb.sceneTarget ? vkb.sceneTarget : &vkb.scene;
      im->image   = t->depth;
      im->view    = t->depthView;
      im->width   = t->width;
      im->height  = t->height;
      im->levels  = 1;
      im->sampler = vkb_sampler(VKB_FILTER_NEAREST, VKB_WRAP_CLAMP, 0, 0);
   }
   return &vkb.images[image];
}

unsigned vkb_image_scene_ref(void) { return VKB_IMAGE_SCENE; }
unsigned vkb_image_target_depth_ref(void) { return VKB_IMAGE_TARGET_DEPTH; }
unsigned vkb_image_scene_depth_ref(void) { return VKB_IMAGE_SCENE_DEPTH; }

/* a free slot: the main thread's from the bottom of the table, the
 * render thread's (a cinematic's texture, made in its frame) from the
 * top, so the two never pick the same one */
static unsigned vkb_image_slot(void)
{
   unsigned i;
   if (vkb_side() == VKB_SIDE_RENDER)
   {
      for (i = VKB_MAX_IMAGES; i > VKB_IMAGE_FIRST; i--)
         if (!vkb.images[i - 1].used)
            return i - 1;
      return 0;
   }
   for (i = VKB_IMAGE_FIRST; i < VKB_MAX_IMAGES; i++)
      if (!vkb.images[i].used)
         return i;
   return 0;
}

static int vkb_image_make(vkb_image_t *im, unsigned width, unsigned height, int levels, int cube,
      enum vkb_format format, VkImageUsageFlags usage, VkImageCreateFlags flags)
{
   VkImageCreateInfo ici;
   VkImageViewCreateInfo vci;
   VkMemoryRequirements req;
   VkFormat vkformat = vkb_vk_format(format);

   memset(&ici, 0, sizeof(ici));
   ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   ici.flags         = flags | (cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0);
   ici.imageType     = VK_IMAGE_TYPE_2D;
   ici.format        = vkformat;
   ici.extent.width  = width;
   ici.extent.height = height;
   ici.extent.depth  = 1;
   ici.mipLevels     = (uint32_t)(levels > 0 ? levels : 1);
   ici.arrayLayers   = cube ? 6 : 1;
   ici.samples       = VK_SAMPLE_COUNT_1_BIT;
   ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
   ici.usage         = usage;
   ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
   ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   if (vk.vkCreateImage(vkb.device, &ici, NULL, &im->image) != VK_SUCCESS)
      return 0;
   vk.vkGetImageMemoryRequirements(vkb.device, im->image, &req);
   if (!vkb_mem_alloc(&req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &im->mem, NULL))
      return 0;
   if (vk.vkBindImageMemory(vkb.device, im->image, im->mem.memory, im->mem.offset) != VK_SUCCESS)
      return 0;

   memset(&vci, 0, sizeof(vci));
   vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   vci.image                           = im->image;
   vci.viewType                        = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
   vci.format                          = vkformat;
   vci.subresourceRange.aspectMask     = format == VKB_FORMAT_DEPTH
      ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
   /* the channels the engine's formats read back */
   if (format == VKB_FORMAT_A8)
   {
      vci.components.r = VK_COMPONENT_SWIZZLE_ONE;
      vci.components.g = VK_COMPONENT_SWIZZLE_ONE;
      vci.components.b = VK_COMPONENT_SWIZZLE_ONE;
      vci.components.a = VK_COMPONENT_SWIZZLE_R;
   }
   else if (format == VKB_FORMAT_I16)
   {
      vci.components.r = VK_COMPONENT_SWIZZLE_R;
      vci.components.g = VK_COMPONENT_SWIZZLE_R;
      vci.components.b = VK_COMPONENT_SWIZZLE_R;
      vci.components.a = VK_COMPONENT_SWIZZLE_R;
   }
   else if (format == VKB_FORMAT_LA16)
   {
      vci.components.r = VK_COMPONENT_SWIZZLE_R;
      vci.components.g = VK_COMPONENT_SWIZZLE_R;
      vci.components.b = VK_COMPONENT_SWIZZLE_R;
      vci.components.a = VK_COMPONENT_SWIZZLE_G;
   }
   vci.subresourceRange.levelCount     = ici.mipLevels;
   vci.subresourceRange.layerCount     = ici.arrayLayers;
   if (vk.vkCreateImageView(vkb.device, &vci, NULL, &im->view) != VK_SUCCESS)
      return 0;
   im->width  = width;
   im->height = height;
   im->levels = (unsigned char)ici.mipLevels;
   im->cube   = (unsigned char)(cube ? 1 : 0);
   im->format = (unsigned char)format;
   return 1;
}

unsigned vkb_image_create(unsigned width, unsigned height, int levels, int cube,
      enum vkb_format format, enum vkb_filter filter, enum vkb_wrap wrap, int anisotropy)
{
   unsigned id;
   vkb_image_t *im;
   if (!width || !height)
      return 0;
   id = vkb_image_slot();
   if (!id)
   {
      return 0;
   }
   im = &vkb.images[id];
   memset(im, 0, sizeof(*im));
   im->used = 1;
   if (!vkb_image_make(im, width, height, levels, cube, format,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0))
   {
      vkb_image_release(im);
      return 0;
   }
   im->sampler = vkb_sampler(filter, wrap, anisotropy, im->levels > 1);
   return id;
}

unsigned vkb_image_create_target(unsigned width, unsigned height, enum vkb_format format)
{
   unsigned id;
   vkb_image_t *im;
   if (!width || !height)
      return 0;
   id = vkb_image_slot();
   if (!id)
   {
      return 0;
   }
   im = &vkb.images[id];
   memset(im, 0, sizeof(*im));
   im->used   = 1;
   im->target = 1;
   if (!vkb_image_make(im, width, height, 1, 0, format,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | (format == VKB_FORMAT_DEPTH ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
               : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT), 0))
   {
      vkb_image_release(im);
      return 0;
   }
   im->sampler = vkb_sampler(format == VKB_FORMAT_DEPTH ? VKB_FILTER_NEAREST : VKB_FILTER_LINEAR_NOMIP,
         VKB_WRAP_CLAMP, 0, 0);
   return id;
}

void vkb_image_filter(unsigned image, enum vkb_filter filter, enum vkb_wrap wrap, int anisotropy)
{
   vkb_image_t *im;
   if (image < VKB_IMAGE_FIRST || image >= VKB_MAX_IMAGES || !vkb.images[image].used)
      return;
   im = &vkb.images[image];
   im->sampler = vkb_sampler(filter, wrap, anisotropy, im->levels > 1);
}

/* An image goes when the frames that may sample it are off the GPU: the
 * slot is held until then.  A grave list a side: the render thread's
 * (a cinematic's texture outgrown) joins the main thread's at the
 * handoff, where the frames that are done are released. */
typedef struct vkb_image_grave
{
   vkb_image_t image;
   unsigned frame;
} vkb_image_grave_t;

typedef struct vkb_graves
{
   vkb_image_grave_t *graves;
   unsigned count, capacity;
} vkb_graves_t;

static vkb_graves_t graves[VKB_SIDE_COUNT];

static int vkb_grave_push(vkb_graves_t *g, const vkb_image_t *im)
{
   if (g->count == g->capacity)
   {
      unsigned cap = g->capacity ? g->capacity * 2 : 64;
      vkb_image_grave_t *n = (vkb_image_grave_t *)realloc(g->graves, cap * sizeof(*n));
      if (!n)
         return 0;
      g->graves   = n;
      g->capacity = cap;
   }
   g->graves[g->count].image = *im;
   g->graves[g->count].frame = (unsigned)retro_atomic_load_relaxed_int(&vkb.frameCount);
   g->count++;
   return 1;
}

void vkb_image_destroy(unsigned image)
{
   const enum vkb_side side = vkb_side();
   vkb_upload_set_t *set = &vkb.uploads[side];
   vkb_image_t *im;
   unsigned i;
   if (image < VKB_IMAGE_FIRST || image >= VKB_MAX_IMAGES || !vkb.images[image].used)
      return;
   im = &vkb.images[image];
   /* an upload this side still has queued for it is dropped; one the
    * other side holds finds the slot empty and skips it */
   for (i = 0; i < set->count; )
      if (set->uploads[i].image == image)
         set->uploads[i] = set->uploads[--set->count];
      else
         i++;
   retro_atomic_store_release_int(&im->uploadPending, 0);
   if (!vkb_grave_push(&graves[side], im))
   {
      if (side == VKB_SIDE_RENDER)
      {
         vkb.ownerViolations++;
         return;     /* the slot stays until the device goes */
      }
      vk.vkDeviceWaitIdle(vkb.device);
      vkb_image_release(im);
      return;
   }
   /* the slot is empty from here; used last, so a reader of the other
    * thread sees a whole record or an empty one */
   im->image = VK_NULL_HANDLE;
   im->view  = VK_NULL_HANDLE;
   im->used  = 0;
   memset(im, 0, sizeof(*im));
}

/* at shutdown, the device idle */
static void vkb_images_release_graves(void)
{
   unsigned side, i;
   for (side = 0; side < VKB_SIDE_COUNT; side++)
   {
      vkb_graves_t *g = &graves[side];
      for (i = 0; i < g->count; i++)
         vkb_image_release(&g->graves[i].image);
      free(g->graves);
      memset(g, 0, sizeof(*g));
   }
}

/* the main thread between frames */
void vkb_images_handoff(void)
{
   vkb_graves_t *main = &graves[VKB_SIDE_MAIN];
   vkb_graves_t *render = &graves[VKB_SIDE_RENDER];
   unsigned frameCount = (unsigned)retro_atomic_load_relaxed_int(&vkb.frameCount);
   unsigned i;
   /* the render thread's graves, with the frame they were dug in */
   for (i = 0; i < render->count; i++)
   {
      if (!vkb_grave_push(main, &render->graves[i].image))
         break;
      main->graves[main->count - 1].frame = render->graves[i].frame;
   }
   render->count = 0;
   for (i = 0; i < main->count; )
   {
      if (frameCount >= main->graves[i].frame + VKB_FRAMES)
      {
         vkb_image_release(&main->graves[i].image);
         main->graves[i] = main->graves[--main->count];
      }
      else
         i++;
   }
   if (!main->count && main->graves)
   {
      free(main->graves);
      main->graves = NULL;
      main->capacity = 0;
   }
   /* the main side's queued uploads to the render side, which flushes
    * them at the frame's start; their chunks retire with that frame */
   if (vkb.uploads[VKB_SIDE_MAIN].count)
   {
      vkb_upload_set_t *from = &vkb.uploads[VKB_SIDE_MAIN];
      vkb_upload_set_t *to   = &vkb.uploads[VKB_SIDE_RENDER];
      if (!to->count)
      {
         vkb_upload_set_t t = *to;
         *to   = *from;
         *from = t;
      }
      else
      {
         for (i = 0; i < from->count; i++)
         {
            if (to->count == to->capacity)
            {
               unsigned cap = to->capacity ? to->capacity * 2 : 1024;
               vkb_upload_t *n = (vkb_upload_t *)realloc(to->uploads, cap * sizeof(*n));
               if (!n)
                  break;
               to->uploads  = n;
               to->capacity = cap;
            }
            to->uploads[to->count++] = from->uploads[i];
         }
         from->count = 0;
      }
   }
}

/* --- uploads ------------------------------------------------------------------- */

int vkb_image_upload(unsigned image, int level, int face, const void *data, size_t bytes)
{
   const enum vkb_side side = vkb_side();
   vkb_upload_set_t *set = &vkb.uploads[side];
   vkb_upload_t *u;
   unsigned char *p;
   if (image < VKB_IMAGE_FIRST || image >= VKB_MAX_IMAGES || !vkb.images[image].used || !data || !bytes)
      return 0;
   if (set->count == set->capacity)
   {
      unsigned cap = set->capacity ? set->capacity * 2 : 1024;
      vkb_upload_t *n = (vkb_upload_t *)realloc(set->uploads, cap * sizeof(*n));
      if (!n)
         return 0;
      set->uploads  = n;
      set->capacity = cap;
   }
   u = &set->uploads[set->count];
   memset(u, 0, sizeof(*u));
   p = vkb_staging_alloc(side, bytes, &u->staging, &u->stagingOffset);
   if (!p)
      return 0;
   memcpy(p, data, bytes);
   u->image = image;
   u->bytes = (unsigned)bytes;
   u->level = (unsigned char)level;
   u->face  = (unsigned char)face;
   set->count++;
   retro_atomic_store_release_int(&vkb.images[image].uploadPending, 1);
   /* a load between frames that has filled the arena goes to the device
    * now rather than growing it */
   if (side == VKB_SIDE_MAIN && !retro_atomic_load_acquire_int(&vkb.frameOpen)
         && vkb_staging_should_flush(side))
      vkb_transfer_now();
   return 1;
}

static void vkb_whole_image_barrier(VkCommandBuffer cmd, vkb_image_t *im, VkImageLayout from,
      VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
      VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
   VkImageMemoryBarrier b;
   memset(&b, 0, sizeof(b));
   b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   b.srcAccessMask               = srcAccess;
   b.dstAccessMask               = dstAccess;
   b.oldLayout                   = from;
   b.newLayout                   = to;
   b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
   b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
   b.image                       = im->image;
   b.subresourceRange.aspectMask = im->format == VKB_FORMAT_DEPTH
      ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
   b.subresourceRange.levelCount = im->levels;
   b.subresourceRange.layerCount = im->cube ? 6 : 1;
   vk.vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b);
}

/* The side's queued uploads into a command buffer: every touched image
 * goes to transfer, takes its copies, and comes back readable.  The
 * render side's chunks retire with the frame; the main side's did at the
 * handoff, or do at once after a waited submit.  An upload for a slot
 * emptied since is skipped. */
void vkb_images_flush_uploads(VkCommandBuffer cmd, enum vkb_side side)
{
   vkb_upload_set_t *set = &vkb.uploads[side];
   unsigned i;
   if (!set->count)
      return;
   if (side == VKB_SIDE_RENDER)
      vkb_staging_flush_memory(side);
   for (i = 0; i < set->count; i++)
   {
      vkb_upload_t *u = &set->uploads[i];
      vkb_image_t *im = &vkb.images[u->image];
      VkBufferImageCopy region;
      if (!im->used || !im->image)
         continue;
      if (!im->inTransfer)
      {
         vkb_whole_image_barrier(cmd, im,
               im->layoutReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               im->layoutReady ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT,
               im->layoutReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
         im->inTransfer = 1;
      }
      memset(&region, 0, sizeof(region));
      region.bufferOffset                    = u->stagingOffset;
      region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel       = u->level;
      region.imageSubresource.baseArrayLayer = u->face;
      region.imageSubresource.layerCount     = 1;
      region.imageExtent.width  = im->width >> u->level ? im->width >> u->level : 1;
      region.imageExtent.height = im->height >> u->level ? im->height >> u->level : 1;
      region.imageExtent.depth  = 1;
      vk.vkCmdCopyBufferToImage(cmd, u->staging, im->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);
   }
   for (i = 0; i < set->count; i++)
   {
      vkb_image_t *im = &vkb.images[set->uploads[i].image];
      if (!im->inTransfer)
         continue;
      vkb_whole_image_barrier(cmd, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
      im->layoutReady   = 1;
      im->inTransfer    = 0;
      retro_atomic_store_release_int(&im->uploadPending, 0);
   }
   set->count = 0;
   if (side == VKB_SIDE_RENDER)
      vkb_staging_retire(side, 0);
}

/* a target image's framebuffer for the post chain, made on first use */
int vkb_image_post_framebuffer(unsigned image, VkFramebuffer *framebuffer, vkb_image_t **out)
{
   vkb_image_t *im;
   VkFramebufferCreateInfo fci;
   VkRenderPass pass;
   if (image < VKB_IMAGE_FIRST || image >= VKB_MAX_IMAGES || !vkb.images[image].used || !vkb.images[image].target)
      return 0;
   im = &vkb.images[image];
   if (im->format == VKB_FORMAT_DEPTH)
      return 0;
   if (!im->framebuffer)
   {
      pass = vkb_post_render_pass((enum vkb_format)im->format);
      if (!pass)
         return 0;
      memset(&fci, 0, sizeof(fci));
      fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fci.renderPass      = pass;
      fci.attachmentCount = 1;
      fci.pAttachments    = &im->view;
      fci.width           = im->width;
      fci.height          = im->height;
      fci.layers          = 1;
      if (vk.vkCreateFramebuffer(vkb.device, &fci, NULL, &im->framebuffer) != VK_SUCCESS)
      {
         im->framebuffer = VK_NULL_HANDLE;
         return 0;
      }
   }
   *framebuffer = im->framebuffer;
   *out = im;
   return 1;
}

/* A side target's framebuffer: the colour image's, with the one depth
 * image it is drawn with */
int vkb_image_side_framebuffer(unsigned color, unsigned depth, VkFramebuffer *framebuffer,
      vkb_image_t **colorOut, vkb_image_t **depthOut)
{
   vkb_image_t *ci, *di;
   VkFramebufferCreateInfo fci;
   VkImageView views[2];
   VkRenderPass pass;
   if (color < VKB_IMAGE_FIRST || color >= VKB_MAX_IMAGES || !vkb.images[color].used || !vkb.images[color].target
         || depth < VKB_IMAGE_FIRST || depth >= VKB_MAX_IMAGES || !vkb.images[depth].used
         || vkb.images[depth].format != VKB_FORMAT_DEPTH || vkb.images[color].format == VKB_FORMAT_DEPTH)
      return 0;
   ci = &vkb.images[color];
   di = &vkb.images[depth];
   if (ci->width != di->width || ci->height != di->height)
      return 0;
   /* one depth image per colour image: a framebuffer in use is not
    * remade under the frames in flight */
   if (ci->framebuffer && ci->sideDepth != depth)
      return 0;
   if (!ci->framebuffer)
   {
      pass = vkb_render_pass((enum vkb_format)ci->format, 0);
      if (!pass)
         return 0;
      views[0] = ci->view;
      views[1] = di->view;
      memset(&fci, 0, sizeof(fci));
      fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fci.renderPass      = pass;
      fci.attachmentCount = 2;
      fci.pAttachments    = views;
      fci.width           = ci->width;
      fci.height          = ci->height;
      fci.layers          = 1;
      if (vk.vkCreateFramebuffer(vkb.device, &fci, NULL, &ci->framebuffer) != VK_SUCCESS)
      {
         ci->framebuffer = VK_NULL_HANDLE;
         return 0;
      }
      ci->sideDepth = depth;
   }
   *framebuffer = ci->framebuffer;
   *colorOut = ci;
   *depthOut = di;
   return 1;
}

/* --- render targets ------------------------------------------------------------- */

int vkb_target_create(vkb_target_t *t, unsigned width, unsigned height, enum vkb_format format,
      int frontend)
{
   vkb_image_t color, depth;
   VkFramebufferCreateInfo fci;
   VkImageView views[2];
   memset(t, 0, sizeof(*t));
   memset(&color, 0, sizeof(color));
   memset(&depth, 0, sizeof(depth));
   /* the frontend may read an 8-bit frame image through an sRGB view */
   if (!vkb_image_make(&color, width, height, 1, 0, format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | (frontend ? VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT : 0),
            frontend ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0))
   {
      vkb_image_release(&color);
      return 0;
   }
   if (!vkb_image_make(&depth, width, height, 1, 0, VKB_FORMAT_DEPTH,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0))
   {
      vkb_image_release(&color);
      vkb_image_release(&depth);
      return 0;
   }
   t->color     = color.image;
   t->colorView = color.view;
   t->colorMem  = color.mem;
   t->depth     = depth.image;
   t->depthView = depth.view;
   t->depthMem  = depth.mem;
   t->width       = width;
   t->height      = height;
   t->format      = (unsigned char)format;
   t->renderPass  = vkb_render_pass(format, frontend);
   if (!t->renderPass)
   {
      vkb_target_destroy(t);
      return 0;
   }
   views[0] = t->colorView;
   views[1] = t->depthView;
   memset(&fci, 0, sizeof(fci));
   fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fci.renderPass      = t->renderPass;
   fci.attachmentCount = 2;
   fci.pAttachments    = views;
   fci.width           = width;
   fci.height          = height;
   fci.layers          = 1;
   if (vk.vkCreateFramebuffer(vkb.device, &fci, NULL, &t->framebuffer) != VK_SUCCESS)
   {
      vkb_target_destroy(t);
      return 0;
   }
   /* the frame image's in-place pass: the colour alone */
   if (frontend)
   {
      VkRenderPass pass = vkb_inplace_render_pass(format);
      if (!pass)
      {
         vkb_target_destroy(t);
         return 0;
      }
      fci.renderPass      = pass;
      fci.attachmentCount = 1;
      if (vk.vkCreateFramebuffer(vkb.device, &fci, NULL, &t->inplaceFramebuffer) != VK_SUCCESS)
      {
         vkb_target_destroy(t);
         return 0;
      }
   }
   return 1;
}

void vkb_target_destroy(vkb_target_t *t)
{
   if (t->inplaceFramebuffer)
      vk.vkDestroyFramebuffer(vkb.device, t->inplaceFramebuffer, NULL);
   if (t->framebuffer)
      vk.vkDestroyFramebuffer(vkb.device, t->framebuffer, NULL);
   if (t->colorView)
      vk.vkDestroyImageView(vkb.device, t->colorView, NULL);
   if (t->color)
      vk.vkDestroyImage(vkb.device, t->color, NULL);
   vkb_mem_free(&t->colorMem);
   if (t->depthView)
      vk.vkDestroyImageView(vkb.device, t->depthView, NULL);
   if (t->depth)
      vk.vkDestroyImage(vkb.device, t->depth, NULL);
   vkb_mem_free(&t->depthMem);
   memset(t, 0, sizeof(*t));
}

/* the frame image's view, as set_image wants it described */
void vkb_target_view_info(const vkb_target_t *t, VkImageViewCreateInfo *info)
{
   memset(info, 0, sizeof(*info));
   info->sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   info->image                       = t->color;
   info->viewType                    = VK_IMAGE_VIEW_TYPE_2D;
   info->format                      = vkb_vk_format((enum vkb_format)t->format);
   info->components.r                = VK_COMPONENT_SWIZZLE_R;
   info->components.g                = VK_COMPONENT_SWIZZLE_G;
   info->components.b                = VK_COMPONENT_SWIZZLE_B;
   info->components.a                = VK_COMPONENT_SWIZZLE_A;
   info->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   info->subresourceRange.levelCount = 1;
   info->subresourceRange.layerCount = 1;
}

/* --- the scene copies --------------------------------------------------------- */

/* A rectangle of the current target into an image the stages sample,
 * outside the render pass. */
/* The rectangle x, y, w, h of the target (from its bottom left, as the
 * engine counts) into the image's bottom left; part, from that corner,
 * narrows it to what will be read, where the image already holds the
 * rest.  A part of no width copies nothing. */
static void vkb_copy_target(unsigned image, int depth, int x, int y, int w, int h, const int *part)
{
   vkb_image_t *im;
   VkImageCopy region;
   VkImage src;
   VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
   int th, px = 0, py = 0;
   if (image < VKB_IMAGE_FIRST || image >= VKB_MAX_IMAGES || !vkb.images[image].used || !vkb.frameOpen)
      return;
   im = &vkb.images[image];
   th = (int)vkb.target->height;
   if (w > (int)im->width) w = (int)im->width;
   if (h > (int)im->height) h = (int)im->height;
   if (w <= 0 || h <= 0)
      return;
   /* an image that holds nothing yet takes all of it */
   if (part && im->layoutReady && part[0] >= 0 && part[1] >= 0
         && part[0] + part[2] <= w && part[1] + part[3] <= h)
   {
      if (part[2] <= 0 || part[3] <= 0)
         return;
      px = part[0];
      py = part[1];
      x += px;
      y += py;
      w  = part[2];
      h  = part[3];
   }
   vkb_leave_render_pass();
   src = depth ? vkb.target->depth : vkb.target->color;
   /* the target to transfer source: colour was left readable by the
    * pass's end, depth is written by the pass */
   {
      VkImageMemoryBarrier b;
      memset(&b, 0, sizeof(b));
      b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.srcAccessMask               = depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
      b.dstAccessMask               = VK_ACCESS_TRANSFER_READ_BIT;
      b.oldLayout                   = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
         : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      b.image                       = src;
      b.subresourceRange.aspectMask = depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
      b.subresourceRange.levelCount = 1;
      b.subresourceRange.layerCount = 1;
      vk.vkCmdPipelineBarrier(vkb.cmd, depth ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   }
   vkb_whole_image_barrier(vkb.cmd, im,
         im->layoutReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         im->layoutReady ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT,
         im->layoutReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
   memset(&region, 0, sizeof(region));
   region.srcSubresource.aspectMask = aspect;
   region.srcSubresource.layerCount = 1;
   region.dstSubresource.aspectMask = aspect;
   region.dstSubresource.layerCount = 1;
   /* the engine's rectangle is from the bottom; the image's rows from the top */
   region.srcOffset.x      = x;
   region.srcOffset.y      = th - y - h;
   region.dstOffset.x      = px;
   region.dstOffset.y      = (int)im->height - py - h;
   region.extent.width     = (uint32_t)w;
   region.extent.height    = (uint32_t)h;
   region.extent.depth     = 1;
   if (depth)
   {
      region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
      region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
   }
   vk.vkCmdCopyImage(vkb.cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, im->image,
         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
   vkb_whole_image_barrier(vkb.cmd, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
         VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
   im->layoutReady = 1;
   {
      VkImageMemoryBarrier b;
      memset(&b, 0, sizeof(b));
      b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.srcAccessMask               = VK_ACCESS_TRANSFER_READ_BIT;
      b.dstAccessMask               = depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
      b.oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      b.newLayout                   = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
         : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      b.image                       = src;
      b.subresourceRange.aspectMask = depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
      b.subresourceRange.levelCount = 1;
      b.subresourceRange.layerCount = 1;
      vk.vkCmdPipelineBarrier(vkb.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            depth ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &b);
   }
   vkb_enter_render_pass();
}

void vkb_copy_color(unsigned image, int x, int y, int w, int h) { vkb_copy_target(image, 0, x, y, w, h, NULL); }
void vkb_copy_depth(unsigned image, int x, int y, int w, int h) { vkb_copy_target(image, 1, x, y, w, h, NULL); }

void vkb_copy_color_part(unsigned image, int x, int y, int w, int h, int px, int py, int pw, int ph)
{
   int part[4];
   part[0] = px;
   part[1] = py;
   part[2] = pw;
   part[3] = ph;
   vkb_copy_target(image, 0, x, y, w, h, part);
}
