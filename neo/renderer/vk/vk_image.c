/*
 * vk_image: textures, samplers, their uploads, and the render targets.
 *
 * A texture is created in device memory with optimal tiling; its data
 * takes the one copy through a staging buffer that tiling makes
 * unavoidable, recorded ahead of the next frame's drawing.  Image ids 1
 * and 2 wrap the scene target's colour and depth for the composite and
 * the stages that read the scene.
 */
#include <stdlib.h>
#include <string.h>

#include "vk_internal.h"

#define VKB_IMAGE_SCENE 1
#define VKB_IMAGE_SCENE_DEPTH 2
#define VKB_IMAGE_FIRST 3

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
      case VKB_FORMAT_DEPTH:      return vkb_depth_stencil_format();
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
   vkb.images[VKB_IMAGE_SCENE_DEPTH].used = 1;
   vkb.uploadCount = 0;
   depthFormat = 0;
   return 1;
}

static void vkb_image_release(vkb_image_t *im)
{
   if (im->view)
      vk.vkDestroyImageView(vkb.device, im->view, NULL);
   if (im->image)
      vk.vkDestroyImage(vkb.device, im->image, NULL);
   vkb_mem_free(&im->mem);
   memset(im, 0, sizeof(*im));
}

void vkb_images_shutdown(void)
{
   unsigned i, f, w, a;
   free(vkb.uploads);
   vkb.uploads = NULL;
   vkb.uploadCount = vkb.uploadCapacity = 0;
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
      im->image   = vkb.scene.color;
      im->view    = vkb.scene.colorView;
      im->width   = vkb.scene.width;
      im->height  = vkb.scene.height;
      im->levels  = 1;
      im->sampler = vkb_sampler(VKB_FILTER_LINEAR_NOMIP, VKB_WRAP_CLAMP, 0, 0);
   }
   else if (image == VKB_IMAGE_SCENE_DEPTH)
   {
      vkb_image_t *im = &vkb.images[image];
      im->image   = vkb.scene.depth;
      im->view    = vkb.scene.depthView;
      im->width   = vkb.scene.width;
      im->height  = vkb.scene.height;
      im->levels  = 1;
      im->sampler = vkb_sampler(VKB_FILTER_NEAREST, VKB_WRAP_CLAMP, 0, 0);
   }
   return &vkb.images[image];
}

unsigned vkb_image_scene_ref(void) { return VKB_IMAGE_SCENE; }
unsigned vkb_image_scene_depth_ref(void) { return VKB_IMAGE_SCENE_DEPTH; }

static unsigned vkb_image_slot(void)
{
   unsigned i;
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
   unsigned id = vkb_image_slot();
   vkb_image_t *im;
   if (!id || !width || !height)
      return 0;
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
   unsigned id = vkb_image_slot();
   vkb_image_t *im;
   if (!id || !width || !height)
      return 0;
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
 * slot is held until then. */
typedef struct vkb_image_grave
{
   vkb_image_t image;
   unsigned frame;
} vkb_image_grave_t;

static vkb_image_grave_t *graves;
static unsigned numGraves, graveCapacity;

void vkb_image_destroy(unsigned image)
{
   vkb_image_t *im;
   unsigned i;
   if (image < VKB_IMAGE_FIRST || image >= VKB_MAX_IMAGES || !vkb.images[image].used)
      return;
   im = &vkb.images[image];
   /* an upload still queued for it is dropped */
   for (i = 0; i < vkb.uploadCount; )
      if (vkb.uploads[i].image == image)
         vkb.uploads[i] = vkb.uploads[--vkb.uploadCount];
      else
         i++;
   im->uploadPending = 0;
   if (numGraves == graveCapacity)
   {
      unsigned cap = graveCapacity ? graveCapacity * 2 : 64;
      vkb_image_grave_t *n = (vkb_image_grave_t *)realloc(graves, cap * sizeof(*n));
      if (!n)
      {
         vk.vkDeviceWaitIdle(vkb.device);
         vkb_image_release(im);
         return;
      }
      graves        = n;
      graveCapacity = cap;
   }
   graves[numGraves].image = *im;
   graves[numGraves].frame = vkb.frameCount;
   numGraves++;
   memset(im, 0, sizeof(*im));
}

/* at the start of a frame: what the frames in flight have let go */
void vkb_images_frame_done(void)
{
   unsigned i;
   for (i = 0; i < numGraves; )
   {
      if (vkb.frameCount >= graves[i].frame + VKB_FRAMES)
      {
         vkb_image_release(&graves[i].image);
         graves[i] = graves[--numGraves];
      }
      else
         i++;
   }
   if (!numGraves && graves)
   {
      free(graves);
      graves = NULL;
      graveCapacity = 0;
   }
}

/* --- uploads ------------------------------------------------------------------- */

int vkb_image_upload(unsigned image, int level, int face, const void *data, size_t bytes)
{
   vkb_upload_t *u;
   unsigned char *p;
   if (image < VKB_IMAGE_FIRST || image >= VKB_MAX_IMAGES || !vkb.images[image].used || !data || !bytes)
      return 0;
   if (vkb.uploadCount == vkb.uploadCapacity)
   {
      unsigned cap = vkb.uploadCapacity ? vkb.uploadCapacity * 2 : 1024;
      vkb_upload_t *n = (vkb_upload_t *)realloc(vkb.uploads, cap * sizeof(*n));
      if (!n)
         return 0;
      vkb.uploads        = n;
      vkb.uploadCapacity = cap;
   }
   u = &vkb.uploads[vkb.uploadCount];
   memset(u, 0, sizeof(*u));
   p = vkb_staging_alloc(bytes, &u->staging, &u->stagingOffset);
   if (!p)
      return 0;
   memcpy(p, data, bytes);
   u->image = image;
   u->bytes = (unsigned)bytes;
   u->level = (unsigned char)level;
   u->face  = (unsigned char)face;
   vkb.uploadCount++;
   vkb.images[image].uploadPending = 1;
   /* a load between frames that has filled the arena goes to the device
    * now rather than growing it */
   if (!vkb.frameOpen && vkb_staging_should_flush())
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

/* The queued uploads into a command buffer: every touched image goes to
 * transfer, takes its copies, and comes back readable.  The staging
 * chunks retire with the frame. */
void vkb_images_flush_uploads(VkCommandBuffer cmd)
{
   unsigned i;
   if (!vkb.uploadCount)
      return;
   vkb_staging_flush_memory();
   for (i = 0; i < vkb.uploadCount; i++)
   {
      vkb_upload_t *u = &vkb.uploads[i];
      vkb_image_t *im = &vkb.images[u->image];
      VkBufferImageCopy region;
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
   for (i = 0; i < vkb.uploadCount; i++)
   {
      vkb_image_t *im = &vkb.images[vkb.uploads[i].image];
      if (!im->inTransfer)
         continue;
      vkb_whole_image_barrier(cmd, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
      im->layoutReady   = 1;
      im->inTransfer    = 0;
      im->uploadPending = 0;
   }
   vkb.uploadCount = 0;
   vkb_staging_retire(0);
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
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
   return 1;
}

void vkb_target_destroy(vkb_target_t *t)
{
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
static void vkb_copy_target(unsigned image, int depth, int x, int y, int w, int h)
{
   vkb_image_t *im;
   VkImageCopy region;
   VkImage src;
   VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
   int th;
   if (image < VKB_IMAGE_FIRST || image >= VKB_MAX_IMAGES || !vkb.images[image].used || !vkb.frameOpen)
      return;
   im = &vkb.images[image];
   th = (int)vkb.target->height;
   if (w > (int)im->width) w = (int)im->width;
   if (h > (int)im->height) h = (int)im->height;
   if (w <= 0 || h <= 0)
      return;
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
   region.dstOffset.x      = 0;
   region.dstOffset.y      = (int)im->height - h;
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

void vkb_copy_color(unsigned image, int x, int y, int w, int h) { vkb_copy_target(image, 0, x, y, w, h); }
void vkb_copy_depth(unsigned image, int x, int y, int w, int h) { vkb_copy_target(image, 1, x, y, w, h); }
