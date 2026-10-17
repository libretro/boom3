/*
 * vk_host: a headless libretro Vulkan frontend for the core's Vulkan
 * renderer, the counterpart of egl_host for the GL ones.
 *
 * It answers SET_HW_RENDER for RETRO_HW_CONTEXT_VULKAN, lets the core
 * negotiate its device through the context negotiation interface, hands
 * it the retro_hw_render_interface_vulkan, and after each frame the core
 * presents copies the image it set into host memory to print a hash of
 * it, as egl_host does.  Three sync indices rotate as a frontend's
 * swapchain would, so the core's per-index fences and image ring are
 * exercised.  The frames are read back fully synchronously; the harness
 * measures correctness, not speed.
 *
 *   vk_host <core.so> <content> <frames> [key=value ...]
 *
 * key=value pairs answer the core's options; ppm=<file> writes the last
 * frame; from=<n> starts printing hashes at frame n; shots=<n>,<n>,...
 * with ppm=<prefix> writes those frames as <prefix>_<n>.ppm.  Alpha is
 * the frontend's to ignore and is left out of the hash.
 *
 * Linux: the Vulkan loader (libvulkan-dev); runs on lavapipe without a
 * GPU (mesa-vulkan-drivers, VK_ICD_FILENAMES=.../lvp_icd.json).
 *   cc -std=c89 -O1 -g -o vk_host vk_host.c -ldl -lvulkan -lm
 */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#include "../../neo/sys/libretro-common/include/libretro.h"
#include "../../neo/sys/libretro-common/include/libretro_vulkan.h"

#define MAX_OPTIONS 64
#define SYNC_COUNT 3

static const char *opt_key[MAX_OPTIONS];
static const char *opt_val[MAX_OPTIONS];
static int num_opts;

static struct retro_hw_render_callback hw;
static int have_hw;
static const struct retro_hw_render_context_negotiation_interface_vulkan *negotiate;
static struct retro_hw_render_interface_vulkan iface;

static VkInstance instance;
static VkPhysicalDevice gpu;
static VkDevice device;
static VkQueue queue;
static uint32_t queue_family;
static VkCommandPool pool;
static VkCommandBuffer cmd;
static VkFence fence;
static VkBuffer readback;
static VkDeviceMemory readback_memory;
static VkDeviceSize readback_size;
static void *readback_map;
static VkPhysicalDeviceMemoryProperties memory_properties;

/* what the core set for this frame */
static const struct retro_vulkan_image *frame_image;
static VkSemaphore frame_wait[8];
static uint32_t frame_num_wait;
static VkSemaphore frame_signal;
static uint32_t sync_index;

static unsigned fb_w = 640, fb_h = 480;
static unsigned frame_w, frame_h;
static int presented;
static int frame_index;
static int print_from;
static const char *ppm_path;
static const char *shots;
static unsigned char *pixels;

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

/* --- the render interface the core calls --------------------------------- */

static void iface_set_image(void *handle, const struct retro_vulkan_image *image,
      uint32_t num_semaphores, const VkSemaphore *semaphores, uint32_t src_queue_family)
{
   uint32_t i;
   (void)handle;
   (void)src_queue_family;
   frame_image    = image;
   frame_num_wait = num_semaphores > 8 ? 8 : num_semaphores;
   for (i = 0; i < frame_num_wait; i++)
      frame_wait[i] = semaphores[i];
}

static uint32_t iface_get_sync_index(void *handle) { (void)handle; return sync_index; }
static uint32_t iface_get_sync_index_mask(void *handle) { (void)handle; return (1u << SYNC_COUNT) - 1; }
static void iface_set_command_buffers(void *handle, uint32_t num_cmd, const VkCommandBuffer *cmds)
{ (void)handle; (void)num_cmd; (void)cmds; }
/* every frame is read back synchronously, so the index's work is done */
static void iface_wait_sync_index(void *handle) { (void)handle; }
static void iface_lock_queue(void *handle) { (void)handle; }
static void iface_unlock_queue(void *handle) { (void)handle; }
static void iface_set_signal_semaphore(void *handle, VkSemaphore semaphore)
{ (void)handle; frame_signal = semaphore; }

/* --- device creation ------------------------------------------------------- */

static VkInstance create_instance_wrapper(void *opaque, const VkInstanceCreateInfo *info)
{
   VkInstance inst = VK_NULL_HANDLE;
   (void)opaque;
   if (vkCreateInstance(info, NULL, &inst) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   return inst;
}

static VkDevice create_device_wrapper(VkPhysicalDevice phys, void *opaque, const VkDeviceCreateInfo *info)
{
   VkDevice dev = VK_NULL_HANDLE;
   (void)opaque;
   if (vkCreateDevice(phys, info, NULL, &dev) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   return dev;
}

static uint32_t find_memory_type(uint32_t bits, VkMemoryPropertyFlags want)
{
   uint32_t i;
   for (i = 0; i < memory_properties.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (memory_properties.memoryTypes[i].propertyFlags & want) == want)
         return i;
   return 0xffffffffu;
}

static int make_device(void)
{
   VkApplicationInfo app;
   VkInstanceCreateInfo ici;
   uint32_t count = 0;
   struct retro_vulkan_context ctx;

   memset(&app, 0, sizeof(app));
   app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   app.pApplicationName   = "vk_host";
   app.pEngineName        = "vk_host";
   app.apiVersion         = VK_API_VERSION_1_1;
   if (negotiate && negotiate->get_application_info)
   {
      const VkApplicationInfo *core_app = negotiate->get_application_info();
      if (core_app)
         app = *core_app;
   }

   if (negotiate && negotiate->interface_version >= 2 && negotiate->create_instance)
      instance = negotiate->create_instance(vkGetInstanceProcAddr, &app, create_instance_wrapper, NULL);
   else
   {
      memset(&ici, 0, sizeof(ici));
      ici.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
      ici.pApplicationInfo = &app;
      instance = create_instance_wrapper(NULL, &ici);
   }
   if (!instance)
   {
      fprintf(stderr, "vk_host: no Vulkan instance\n");
      return 0;
   }

   vkEnumeratePhysicalDevices(instance, &count, NULL);
   if (!count)
   {
      fprintf(stderr, "vk_host: no Vulkan device (VK_ICD_FILENAMES for lavapipe?)\n");
      return 0;
   }
   count = 1;
   vkEnumeratePhysicalDevices(instance, &count, &gpu);
   vkGetPhysicalDeviceMemoryProperties(gpu, &memory_properties);

   memset(&ctx, 0, sizeof(ctx));
   if (negotiate && negotiate->interface_version >= 2 && negotiate->create_device2)
   {
      if (!negotiate->create_device2(&ctx, instance, gpu, VK_NULL_HANDLE,
               vkGetInstanceProcAddr, create_device_wrapper, NULL))
      {
         fprintf(stderr, "vk_host: the core's create_device2 failed\n");
         return 0;
      }
   }
   else if (negotiate && negotiate->create_device)
   {
      VkPhysicalDeviceFeatures features;
      memset(&features, 0, sizeof(features));
      if (!negotiate->create_device(&ctx, instance, gpu, VK_NULL_HANDLE,
               vkGetInstanceProcAddr, NULL, 0, NULL, 0, &features))
      {
         fprintf(stderr, "vk_host: the core's create_device failed\n");
         return 0;
      }
   }
   else
   {
      /* a core without negotiation: one graphics queue, no features */
      VkDeviceQueueCreateInfo qci;
      VkDeviceCreateInfo dci;
      VkQueueFamilyProperties props[16];
      uint32_t n = 16, i;
      float priority = 1.0f;
      vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, props);
      queue_family = 0;
      for (i = 0; i < n; i++)
         if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
         {
            queue_family = i;
            break;
         }
      memset(&qci, 0, sizeof(qci));
      qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      qci.queueFamilyIndex = queue_family;
      qci.queueCount       = 1;
      qci.pQueuePriorities = &priority;
      memset(&dci, 0, sizeof(dci));
      dci.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
      dci.queueCreateInfoCount = 1;
      dci.pQueueCreateInfos    = &qci;
      ctx.device = create_device_wrapper(gpu, NULL, &dci);
      if (!ctx.device)
         return 0;
      vkGetDeviceQueue(ctx.device, queue_family, 0, &ctx.queue);
      ctx.queue_family_index = queue_family;
      ctx.gpu = gpu;
   }
   device       = ctx.device;
   queue        = ctx.queue;
   queue_family = ctx.queue_family_index;
   if (ctx.gpu)
      gpu = ctx.gpu;

   memset(&iface, 0, sizeof(iface));
   iface.interface_type        = RETRO_HW_RENDER_INTERFACE_VULKAN;
   iface.interface_version     = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
   iface.handle                = NULL;
   iface.instance              = instance;
   iface.gpu                   = gpu;
   iface.device                = device;
   iface.get_device_proc_addr  = vkGetDeviceProcAddr;
   iface.get_instance_proc_addr = vkGetInstanceProcAddr;
   iface.queue                 = queue;
   iface.queue_index           = queue_family;
   iface.set_image             = iface_set_image;
   iface.get_sync_index        = iface_get_sync_index;
   iface.get_sync_index_mask   = iface_get_sync_index_mask;
   iface.set_command_buffers   = iface_set_command_buffers;
   iface.wait_sync_index       = iface_wait_sync_index;
   iface.lock_queue            = iface_lock_queue;
   iface.unlock_queue          = iface_unlock_queue;
   iface.set_signal_semaphore  = iface_set_signal_semaphore;

   {
      VkCommandPoolCreateInfo pci;
      VkCommandBufferAllocateInfo cai;
      VkFenceCreateInfo fci;
      memset(&pci, 0, sizeof(pci));
      pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      pci.queueFamilyIndex = queue_family;
      if (vkCreateCommandPool(device, &pci, NULL, &pool) != VK_SUCCESS)
         return 0;
      memset(&cai, 0, sizeof(cai));
      cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      cai.commandPool        = pool;
      cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cai.commandBufferCount = 1;
      if (vkAllocateCommandBuffers(device, &cai, &cmd) != VK_SUCCESS)
         return 0;
      memset(&fci, 0, sizeof(fci));
      fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      if (vkCreateFence(device, &fci, NULL, &fence) != VK_SUCCESS)
         return 0;
   }
   return 1;
}

static int make_readback(VkDeviceSize size)
{
   VkBufferCreateInfo bci;
   VkMemoryRequirements req;
   VkMemoryAllocateInfo mai;
   if (readback && readback_size >= size)
      return 1;
   if (readback)
   {
      vkUnmapMemory(device, readback_memory);
      vkDestroyBuffer(device, readback, NULL);
      vkFreeMemory(device, readback_memory, NULL);
      readback = VK_NULL_HANDLE;
   }
   memset(&bci, 0, sizeof(bci));
   bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bci.size  = size;
   bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
   if (vkCreateBuffer(device, &bci, NULL, &readback) != VK_SUCCESS)
      return 0;
   vkGetBufferMemoryRequirements(device, readback, &req);
   memset(&mai, 0, sizeof(mai));
   mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mai.allocationSize  = req.size;
   mai.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   if (mai.memoryTypeIndex == 0xffffffffu
         || vkAllocateMemory(device, &mai, NULL, &readback_memory) != VK_SUCCESS)
      return 0;
   vkBindBufferMemory(device, readback, readback_memory, 0);
   if (vkMapMemory(device, readback_memory, 0, req.size, 0, &readback_map) != VK_SUCCESS)
      return 0;
   readback_size = size;
   return 1;
}

/* The frame's image into pixels: RGBA8 whatever the image holds, through
 * a blit into an 8-bit image would be the general case; this harness
 * reads 8-bit and 10-bit images, which the copy below converts. */
static int read_frame(unsigned w, unsigned h)
{
   const struct retro_vulkan_image *img = frame_image;
   VkCommandBufferBeginInfo bi;
   VkImageMemoryBarrier barrier;
   VkBufferImageCopy region;
   VkSubmitInfo si;
   VkPipelineStageFlags wait_stage[8];
   VkFormat format;
   unsigned bpp, i;
   if (!img || !img->create_info.image)
      return 0;
   format = img->create_info.format;
   bpp    = 4;
   if (format == VK_FORMAT_R16G16B16A16_SFLOAT)
      bpp = 8;
   if (!make_readback((VkDeviceSize)w * h * bpp))
      return 0;

   memset(&bi, 0, sizeof(bi));
   bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   vkResetCommandBuffer(cmd, 0);
   vkBeginCommandBuffer(cmd, &bi);

   memset(&barrier, 0, sizeof(barrier));
   barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   barrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
   barrier.oldLayout           = img->image_layout;
   barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   barrier.image               = img->create_info.image;
   barrier.subresourceRange    = img->create_info.subresourceRange;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = 1;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
         0, 0, NULL, 0, NULL, 1, &barrier);

   memset(&region, 0, sizeof(region));
   region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   region.imageSubresource.layerCount = 1;
   region.imageSubresource.baseArrayLayer = img->create_info.subresourceRange.baseArrayLayer;
   region.imageExtent.width  = w;
   region.imageExtent.height = h;
   region.imageExtent.depth  = 1;
   vkCmdCopyImageToBuffer(cmd, img->create_info.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         readback, 1, &region);

   /* back to the layout the core set, as a frontend leaves it */
   barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   barrier.newLayout     = img->image_layout;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
         0, 0, NULL, 0, NULL, 1, &barrier);
   vkEndCommandBuffer(cmd);

   for (i = 0; i < 8; i++)
      wait_stage[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
   memset(&si, 0, sizeof(si));
   si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.waitSemaphoreCount   = frame_num_wait;
   si.pWaitSemaphores      = frame_wait;
   si.pWaitDstStageMask    = wait_stage;
   si.commandBufferCount   = 1;
   si.pCommandBuffers      = &cmd;
   si.signalSemaphoreCount = frame_signal ? 1 : 0;
   si.pSignalSemaphores    = &frame_signal;
   vkResetFences(device, 1, &fence);
   if (vkQueueSubmit(queue, 1, &si, fence) != VK_SUCCESS)
      return 0;
   vkWaitForFences(device, 1, &fence, VK_TRUE, (uint64_t)-1);
   frame_num_wait = 0;
   frame_signal   = VK_NULL_HANDLE;

   /* into RGBA8, bottom row first as egl_host has it: the image's row 0
    * is the top, and the ppm writer walks rows from the end */
   {
      unsigned x, y;
      const unsigned char *src = (const unsigned char *)readback_map;
      for (y = 0; y < h; y++)
      {
         unsigned char *dst = pixels + (size_t)(h - 1 - y) * w * 4;
         for (x = 0; x < w; x++)
         {
            const unsigned char *s = src + ((size_t)y * w + x) * bpp;
            if (format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || format == VK_FORMAT_A2R10G10B10_UNORM_PACK32)
            {
               unsigned v = (unsigned)s[0] | ((unsigned)s[1] << 8) | ((unsigned)s[2] << 16) | ((unsigned)s[3] << 24);
               unsigned r = v & 1023, g = (v >> 10) & 1023, b = (v >> 20) & 1023;
               if (format == VK_FORMAT_A2R10G10B10_UNORM_PACK32)
               {
                  unsigned t = r;
                  r = b;
                  b = t;
               }
               dst[x * 4 + 0] = (unsigned char)(r >> 2);
               dst[x * 4 + 1] = (unsigned char)(g >> 2);
               dst[x * 4 + 2] = (unsigned char)(b >> 2);
               dst[x * 4 + 3] = 255;
            }
            else if (bpp == 8)
            {
               /* half floats, clamped: enough to see the picture */
               unsigned c;
               for (c = 0; c < 4; c++)
               {
                  unsigned hbits = (unsigned)s[c * 2] | ((unsigned)s[c * 2 + 1] << 8);
                  unsigned e = (hbits >> 10) & 31, m = hbits & 1023;
                  double f;
                  if (e == 0)
                     f = m / 1024.0 / 16384.0;
                  else
                     f = (1.0 + m / 1024.0) * (e >= 15 ? (double)(1u << (e - 15)) : 1.0 / (double)(1u << (15 - e)));
                  if (hbits & 0x8000)
                     f = 0.0;
                  if (f > 1.0)
                     f = 1.0;
                  dst[x * 4 + c] = (unsigned char)(f * 255.0 + 0.5);
               }
            }
            else
            {
               int bgr = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
               dst[x * 4 + 0] = s[bgr ? 2 : 0];
               dst[x * 4 + 1] = s[1];
               dst[x * 4 + 2] = s[bgr ? 0 : 2];
               dst[x * 4 + 3] = s[3];
            }
         }
      }
   }
   return 1;
}

/* Two 32-bit FNV-1a lanes over the colour bytes, as egl_host prints. */
static void hash_frame(char out[17])
{
   unsigned long a = 2166136261UL;
   unsigned long b = 0x811c9dc5UL ^ 0x5bd1e995UL;
   unsigned i;
   for (i = 0; i < frame_w * frame_h * 4; i++)
   {
      if ((i & 3) == 3)
         continue;
      a = ((a ^ pixels[i]) * 16777619UL) & 0xffffffffUL;
      b = ((b ^ pixels[i] ^ (i & 0xff)) * 16777619UL) & 0xffffffffUL;
   }
   sprintf(out, "%08lx%08lx", a, b);
}

static void write_ppm(const char *path, unsigned w, unsigned h)
{
   FILE *f = fopen(path, "wb");
   int y;
   unsigned x;
   if (!f)
      return;
   fprintf(f, "P6 %u %u 255\n", w, h);
   for (y = (int)h - 1; y >= 0; y--)
      for (x = 0; x < w; x++)
         fwrite(pixels + ((unsigned)y * w + x) * 4, 1, 3, f);
   fclose(f);
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
   char hash[17];
   (void)pitch;
   if (data != RETRO_HW_FRAME_BUFFER_VALID)
      return;
   presented++;
   if (w > fb_w || h > fb_h)
   {
      w = w > fb_w ? fb_w : w;
      h = h > fb_h ? fb_h : h;
   }
   frame_w = w;
   frame_h = h;
   if (!read_frame(w, h))
   {
      fprintf(stderr, "vk_host: frame %d: nothing to read back\n", frame_index);
      return;
   }
   if (frame_index >= print_from)
   {
      hash_frame(hash);
      printf("frame %d %ux%u %s\n", frame_index, w, h, hash);
   }
   if (shots && ppm_path)
   {
      const char *p = shots;
      while (p && *p)
      {
         if (atoi(p) == frame_index)
         {
            char name[512];
            sprintf(name, "%s_%d.ppm", ppm_path, frame_index);
            write_ppm(name, w, h);
            break;
         }
         p = strchr(p, ',');
         if (p)
            p++;
      }
   }
   sync_index = (sync_index + 1) % SYNC_COUNT;
}

static size_t audio_batch(const int16_t *d, size_t frames) { (void)d; return frames; }
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static void input_poll(void) { }
/* look=from,to,value: the right stick's vertical axis held between two
 * frames, as egl_host takes it */
static int look_from = -1, look_to = -1, look_val = 0;
static int16_t input_state(unsigned a, unsigned b, unsigned c, unsigned d)
{
   (void)a;
   if (b == RETRO_DEVICE_ANALOG && c == RETRO_DEVICE_INDEX_ANALOG_RIGHT && d == RETRO_DEVICE_ID_ANALOG_Y
         && frame_index >= look_from && frame_index < look_to)
      return (int16_t)look_val;
   return 0;
}

static bool env_cb(unsigned cmd_id, void *data)
{
   int i;
   switch (cmd_id)
   {
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return true;
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback *)data)->log = log_cb;
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/tmp/vk_host_sys";
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
   {
      struct retro_variable *v = (struct retro_variable *)data;
      for (i = 0; i < num_opts; i++)
         if (!strcmp(v->key, opt_key[i]))
         {
            v->value = opt_val[i];
            return true;
         }
      v->value = NULL;
      return false;
   }
   case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
      *(unsigned *)data = RETRO_HW_CONTEXT_VULKAN;
      return true;
   case RETRO_ENVIRONMENT_SET_HW_RENDER:
   {
      struct retro_hw_render_callback *cb = (struct retro_hw_render_callback *)data;
      if (cb->context_type != RETRO_HW_CONTEXT_VULKAN)
      {
         fprintf(stderr, "vk_host: the core asked for context type %d, not Vulkan\n", (int)cb->context_type);
         return false;
      }
      hw = *cb;
      have_hw = 1;
      return true;
   }
   case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
      negotiate = (const struct retro_hw_render_context_negotiation_interface_vulkan *)data;
      return true;
   case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT:
      *(unsigned *)data = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION;
      return true;
   case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
      if (!device)
         return false;
      *(const struct retro_hw_render_interface **)data = (const struct retro_hw_render_interface *)&iface;
      return true;
   case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *(bool *)data = true;
      return true;
   case RETRO_ENVIRONMENT_SET_GEOMETRY:
   case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      return true;
   }
   return false;
}

int main(int argc, char **argv)
{
   void *so;
   int frames, i;
   struct retro_game_info gi;
   struct retro_system_av_info av;
   void (*set_env)(retro_environment_t);
   void (*set_vid)(retro_video_refresh_t);
   void (*set_as)(retro_audio_sample_t);
   void (*set_ab)(retro_audio_sample_batch_t);
   void (*set_ip)(retro_input_poll_t);
   void (*set_is)(retro_input_state_t);
   void (*r_init)(void);
   bool (*r_load)(const struct retro_game_info *);
   void (*r_av)(struct retro_system_av_info *);
   void (*r_run)(void);
   void (*r_unload)(void);
   void (*r_deinit)(void);

   if (argc < 4)
   {
      fprintf(stderr, "usage: %s <core.so> <content> <frames> [key=value ...]\n", argv[0]);
      return 2;
   }
   frames = atoi(argv[3]);
   for (i = 4; i < argc && num_opts < MAX_OPTIONS; i++)
   {
      char *eq = strchr(argv[i], '=');
      if (!eq)
         continue;
      *eq = '\0';
      if (!strcmp(argv[i], "ppm"))
         ppm_path = eq + 1;
      else if (!strcmp(argv[i], "shots"))
         shots = eq + 1;
      else if (!strcmp(argv[i], "from"))
         print_from = atoi(eq + 1);
      else if (!strcmp(argv[i], "look"))
         sscanf(eq + 1, "%d,%d,%d", &look_from, &look_to, &look_val);
      else
      {
         opt_key[num_opts] = argv[i];
         opt_val[num_opts] = eq + 1;
         num_opts++;
      }
   }

   so = dlopen(argv[1], RTLD_NOW);
   if (!so)
   {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 2;
   }
   *(void **)&set_env  = dlsym(so, "retro_set_environment");
   *(void **)&set_vid  = dlsym(so, "retro_set_video_refresh");
   *(void **)&set_as   = dlsym(so, "retro_set_audio_sample");
   *(void **)&set_ab   = dlsym(so, "retro_set_audio_sample_batch");
   *(void **)&set_ip   = dlsym(so, "retro_set_input_poll");
   *(void **)&set_is   = dlsym(so, "retro_set_input_state");
   *(void **)&r_init   = dlsym(so, "retro_init");
   *(void **)&r_load   = dlsym(so, "retro_load_game");
   *(void **)&r_av     = dlsym(so, "retro_get_system_av_info");
   *(void **)&r_run    = dlsym(so, "retro_run");
   *(void **)&r_unload = dlsym(so, "retro_unload_game");
   *(void **)&r_deinit = dlsym(so, "retro_deinit");

   set_env(env_cb);
   set_vid(video_refresh);
   set_as(audio_sample);
   set_ab(audio_batch);
   set_ip(input_poll);
   set_is(input_state);
   r_init();

   memset(&gi, 0, sizeof(gi));
   gi.path = argv[2];
   if (!r_load(&gi))
   {
      fprintf(stderr, "vk_host: load failed\n");
      return 2;
   }
   if (!have_hw)
   {
      fprintf(stderr, "vk_host: the core asked for no hardware context\n");
      return 2;
   }
   memset(&av, 0, sizeof(av));
   r_av(&av);
   if (av.geometry.max_width && av.geometry.max_height)
   {
      fb_w = av.geometry.max_width;
      fb_h = av.geometry.max_height;
   }
   pixels = (unsigned char *)malloc((size_t)fb_w * fb_h * 4);
   if (!make_device())
   {
      fprintf(stderr, "vk_host: no Vulkan device for what the core asked\n");
      return 2;
   }
   if (hw.context_reset)
      hw.context_reset();

   for (frame_index = 0; frame_index < frames; frame_index++)
      r_run();

   if (ppm_path && presented && !shots)
      write_ppm(ppm_path, frame_w, frame_h);
   fprintf(stderr, "vk_host: %d frames run, %d presented\n", frames, presented);
   if (hw.context_destroy)
      hw.context_destroy();
   r_unload();
   r_deinit();
   vkDeviceWaitIdle(device);
   return 0;
}
