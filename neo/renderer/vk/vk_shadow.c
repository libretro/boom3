/*
 * vk_shadow: turbo shadow volumes built by compute at the start of the
 * frame that draws them, ahead of its render pass.
 *
 * Per volume, one pass over its triangles finds each one's facing and
 * writes the caps of the ones facing away; after a barrier, one pass over
 * its sil edges writes the quads between a facing and a back-facing
 * triangle.  The indexes are R_CreateVertexProgramTurboShadowVolume's,
 * appended by atomic counters in the volume's two indexed indirect draws,
 * so their order varies but not the set: the stencil counts it the same.
 *
 * Every volume of the frame is built in the same two dispatch rounds,
 * with three barriers in all.  Nothing is read back.
 *
 * C89.
 */
#include <stdlib.h>
#include <string.h>

#include "vk_internal.h"

#include "spirv/shadow_caps_comp.h"
#include "spirv/shadow_caps_ftz_comp.h"
#include "spirv/shadow_caps_preserve_comp.h"
#include "spirv/shadow_sil_comp.h"

#define VKB_SHADOW_BINDINGS 6
#define VKB_SHADOW_PARM_UINTS 50       /* shadow_build.glsl: floats 0-37, draws at 40 and 45 */
#define VKB_SHADOW_SIL_DRAW 40
#define VKB_SHADOW_CAP_DRAW 45
#define VKB_SHADOW_GROUP 64

typedef struct vkb_shadow_push
{
   uint32_t vertexBase;
   uint32_t indexBase;
   uint32_t shortIndexes;
   uint32_t numFaces;
   uint32_t numEdges;
   uint32_t silBase;
   uint32_t facingBase;
   uint32_t parmBase;
} vkb_shadow_push_t;

static VkPipeline vkb_shadow_pipeline(const unsigned int *code, size_t bytes)
{
   VkShaderModuleCreateInfo mci;
   VkComputePipelineCreateInfo cpi;
   VkShaderModule module = VK_NULL_HANDLE;
   VkPipeline pipeline   = VK_NULL_HANDLE;
   memset(&mci, 0, sizeof(mci));
   mci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   mci.codeSize = bytes;
   mci.pCode    = code;
   if (vk.vkCreateShaderModule(vkb.device, &mci, NULL, &module) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   memset(&cpi, 0, sizeof(cpi));
   cpi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
   cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
   cpi.stage.module = module;
   cpi.stage.pName  = "main";
   cpi.layout       = vkb.shadowLayout;
   if (vk.vkCreateComputePipelines(vkb.device, vkb.pipelineCache, 1, &cpi, NULL, &pipeline) != VK_SUCCESS)
      pipeline = VK_NULL_HANDLE;
   vk.vkDestroyShaderModule(vkb.device, module, NULL);
   return pipeline;
}

int vkb_shadows_init(void)
{
   VkDescriptorSetLayoutBinding bindings[VKB_SHADOW_BINDINGS];
   VkDescriptorSetLayoutCreateInfo dci;
   VkPushConstantRange range;
   VkPipelineLayoutCreateInfo pci;
   VkQueueFamilyProperties families[32];
   uint32_t numFamilies = 32;
   unsigned i;

   /* the queue the frontend handed over has to run compute */
   vk.vkGetPhysicalDeviceQueueFamilyProperties(vkb.gpu, &numFamilies, families);
   if (vkb.queueFamily >= numFamilies || !(families[vkb.queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT))
      return 1;

   memset(bindings, 0, sizeof(bindings));
   for (i = 0; i < VKB_SHADOW_BINDINGS; i++)
   {
      bindings[i].binding         = i;
      bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
   }
   memset(&dci, 0, sizeof(dci));
   dci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dci.bindingCount = VKB_SHADOW_BINDINGS;
   dci.pBindings    = bindings;
   if (vk.vkCreateDescriptorSetLayout(vkb.device, &dci, NULL, &vkb.shadowSetLayout) != VK_SUCCESS)
      return 0;
   range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
   range.offset     = 0;
   range.size       = sizeof(vkb_shadow_push_t);
   memset(&pci, 0, sizeof(pci));
   pci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pci.setLayoutCount         = 1;
   pci.pSetLayouts            = &vkb.shadowSetLayout;
   pci.pushConstantRangeCount = 1;
   pci.pPushConstantRanges    = &range;
   if (vk.vkCreatePipelineLayout(vkb.device, &pci, NULL, &vkb.shadowLayout) != VK_SUCCESS)
      return 0;

   if (vkb.denormMode == 1)
      vkb.shadowCaps = vkb_shadow_pipeline(spirv_shadow_caps_ftz_comp, sizeof(spirv_shadow_caps_ftz_comp));
   else if (vkb.denormMode == 2)
      vkb.shadowCaps = vkb_shadow_pipeline(spirv_shadow_caps_preserve_comp, sizeof(spirv_shadow_caps_preserve_comp));
   else
      vkb.shadowCaps = vkb_shadow_pipeline(spirv_shadow_caps_comp, sizeof(spirv_shadow_caps_comp));
   vkb.shadowSil = vkb_shadow_pipeline(spirv_shadow_sil_comp, sizeof(spirv_shadow_sil_comp));
   /* a device without them draws the CPU's volumes, as before */
   if (!vkb.shadowCaps || !vkb.shadowSil)
      vkb_shadows_shutdown();
   return 1;
}

void vkb_shadows_shutdown(void)
{
   unsigned i;
   for (i = 0; i < VKB_FRAMES; i++)
   {
      vkb_frame_t *f = &vkb.frames[i];
      if (f->shadowIndexes)
         vk.vkDestroyBuffer(vkb.device, f->shadowIndexes, NULL);
      vkb_mem_free(&f->shadowIndexMem);
      if (f->shadowFacing)
         vk.vkDestroyBuffer(vkb.device, f->shadowFacing, NULL);
      vkb_mem_free(&f->shadowFacingMem);
      free(f->shadowJobs);
      free(f->shadowSlots);
      f->shadowJobs        = NULL;
      f->shadowSlots       = NULL;
      f->shadowSlotCount   = 0;
      f->shadowCount       = f->shadowCapacity = 0;
      f->shadowIndexes     = f->shadowFacing   = VK_NULL_HANDLE;
      f->shadowIndexBytes  = f->shadowFacingBytes = 0;
      memset(&f->shadowIndexMem, 0, sizeof(f->shadowIndexMem));
      memset(&f->shadowFacingMem, 0, sizeof(f->shadowFacingMem));
   }
   if (vkb.shadowCaps)
      vk.vkDestroyPipeline(vkb.device, vkb.shadowCaps, NULL);
   if (vkb.shadowSil)
      vk.vkDestroyPipeline(vkb.device, vkb.shadowSil, NULL);
   if (vkb.shadowLayout)
      vk.vkDestroyPipelineLayout(vkb.device, vkb.shadowLayout, NULL);
   if (vkb.shadowSetLayout)
      vk.vkDestroyDescriptorSetLayout(vkb.device, vkb.shadowSetLayout, NULL);
   vkb.shadowCaps      = vkb.shadowSil = VK_NULL_HANDLE;
   vkb.shadowLayout    = VK_NULL_HANDLE;
   vkb.shadowSetLayout = VK_NULL_HANDLE;
}

int vkb_shadows_available(void)
{
   return vkb.ready && vkb.shadowCaps && vkb.shadowSil;
}

/* a slot taken for the front end: what it queues is for its next frame */
void vkb_shadows_frame_reset(vkb_frame_t *f)
{
   if (f->shadowSlots)
      memset(f->shadowSlots, 0, f->shadowSlotCount * sizeof(*f->shadowSlots));
   f->shadowCount = 0;
}

static unsigned vkb_shadow_home(const vkb_frame_t *f, const void *key)
{
   return (unsigned)(((size_t)key >> 4) * 2654435761u) & (f->shadowSlotCount - 1);
}

/* the slot holding key, or the empty one it goes in */
static unsigned vkb_shadow_slot(const vkb_frame_t *f, const void *key)
{
   unsigned i = vkb_shadow_home(f, key);
   while (f->shadowSlots[i] && f->shadowJobs[f->shadowSlots[i] - 1].key != key)
      i = (i + 1) & (f->shadowSlotCount - 1);
   return i;
}

static int vkb_shadow_grow(vkb_frame_t *f)
{
   unsigned cap = f->shadowCapacity ? f->shadowCapacity * 2 : 64;
   vkb_shadow_job_t *jobs = (vkb_shadow_job_t *)realloc(f->shadowJobs, cap * sizeof(*jobs));
   unsigned *slots;
   unsigned i;
   if (!jobs)
      return 0;
   f->shadowJobs = jobs;
   slots = (unsigned *)calloc(cap * 2, sizeof(*slots));
   if (!slots)
      return 0;
   free(f->shadowSlots);
   f->shadowSlots     = slots;
   f->shadowSlotCount = cap * 2;
   f->shadowCapacity  = cap;
   for (i = 0; i < f->shadowCount; i++)
      f->shadowSlots[vkb_shadow_slot(f, f->shadowJobs[i].key)] = i + 1;
   return 1;
}

unsigned vkb_shadow_queue(const void *key, const vkb_shadow_volume_t *volume)
{
   vkb_frame_t *f;
   vkb_shadow_job_t *job;
   unsigned slot;
   if (!vkb_shadows_available() || !volume->numSilEdges || volume->numIndexes < 3)
      return 0;
   f = &vkb.frames[vkb.allocIndex];
   if (f->shadowCount == f->shadowCapacity && !vkb_shadow_grow(f))
      return 0;
   slot = vkb_shadow_slot(f, key);
   if (f->shadowSlots[slot])
      return f->shadowSlots[slot];
   job = &f->shadowJobs[f->shadowCount];
   memset(job, 0, sizeof(*job));
   job->key    = key;
   job->volume = *volume;
   f->shadowSlots[slot] = ++f->shadowCount;
   return f->shadowCount;
}

unsigned vkb_shadow_find(const void *key)
{
   const vkb_frame_t *f = &vkb.frames[vkb.frameIndex];
   unsigned slot;
   if (!f->shadowCount)
      return 0;
   slot = vkb_shadow_slot(f, key);
   /* a volume the frame's build could not fit has a handle past the built ones */
   return f->shadowSlots[slot] <= f->shadowCount ? f->shadowSlots[slot] : 0;
}

/* a device-local storage buffer of at least bytes for the slot; its last
 * frame is off the GPU, so the old one goes now */
static int vkb_shadow_buffer(VkBuffer *buffer, vkb_mem_t *mem, VkDeviceSize *have,
      VkDeviceSize bytes, VkBufferUsageFlags usage)
{
   VkBufferCreateInfo bci;
   VkMemoryRequirements req;
   VkDeviceSize size;
   if (*have >= bytes)
      return 1;
   if (*buffer)
      vk.vkDestroyBuffer(vkb.device, *buffer, NULL);
   vkb_mem_free(mem);
   *buffer = VK_NULL_HANDLE;
   *have   = 0;
   size = 0x40000;
   while (size < bytes)
      size *= 2;
   if (size > vkb.properties.limits.maxStorageBufferRange)
      return 0;
   memset(&bci, 0, sizeof(bci));
   bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bci.size  = size;
   bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | usage;
   if (vk.vkCreateBuffer(vkb.device, &bci, NULL, buffer) != VK_SUCCESS)
      return 0;
   vk.vkGetBufferMemoryRequirements(vkb.device, *buffer, &req);
   if (!vkb_mem_alloc(&req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, mem, NULL)
         || vk.vkBindBufferMemory(vkb.device, *buffer, mem->memory, mem->offset) != VK_SUCCESS)
   {
      vk.vkDestroyBuffer(vkb.device, *buffer, NULL);
      vkb_mem_free(mem);
      *buffer = VK_NULL_HANDLE;
      return 0;
   }
   *have = size;
   return 1;
}

static VkDescriptorSet vkb_shadow_set(vkb_frame_t *f, const vkb_shadow_job_t *job)
{
   VkDescriptorSetAllocateInfo ai;
   VkDescriptorSet set = VK_NULL_HANDLE;
   VkWriteDescriptorSet writes[VKB_SHADOW_BINDINGS];
   VkDescriptorBufferInfo buffers[VKB_SHADOW_BINDINGS];
   unsigned i;

   buffers[0].buffer = vkb_static_vk_buffer(job->volume.vertexBuffer);
   buffers[1].buffer = vkb_static_vk_buffer(job->volume.indexBuffer);
   buffers[2].buffer = vkb_static_vk_buffer(job->volume.silBuffer);
   buffers[3].buffer = f->shadowFacing;
   buffers[4].buffer = f->shadowIndexes;
   buffers[5].buffer = vkb.ring;
   for (i = 0; i < VKB_SHADOW_BINDINGS; i++)
   {
      if (!buffers[i].buffer)
         return VK_NULL_HANDLE;
      buffers[i].offset = 0;
      buffers[i].range  = ~(VkDeviceSize)0;   /* VK_WHOLE_SIZE */
   }
   memset(&ai, 0, sizeof(ai));
   ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   ai.descriptorPool     = f->descriptors;
   ai.descriptorSetCount = 1;
   ai.pSetLayouts        = &vkb.shadowSetLayout;
   if (vk.vkAllocateDescriptorSets(vkb.device, &ai, &set) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   memset(writes, 0, sizeof(writes));
   for (i = 0; i < VKB_SHADOW_BINDINGS; i++)
   {
      writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet          = set;
      writes[i].dstBinding      = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].pBufferInfo     = &buffers[i];
   }
   vk.vkUpdateDescriptorSets(vkb.device, VKB_SHADOW_BINDINGS, writes, 0, NULL);
   return set;
}

static void vkb_shadow_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags srcAccess,
      VkPipelineStageFlags dst, VkAccessFlags dstAccess)
{
   VkMemoryBarrier mb;
   memset(&mb, 0, sizeof(mb));
   mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
   mb.srcAccessMask = srcAccess;
   mb.dstAccessMask = dstAccess;
   vk.vkCmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, NULL, 0, NULL);
}

static void vkb_shadow_push(VkCommandBuffer cmd, const vkb_shadow_job_t *job)
{
   const vkb_shadow_volume_t *v = &job->volume;
   vkb_shadow_push_t pc;
   pc.vertexBase   = v->vertexOffset / 16;
   pc.indexBase    = v->indexOffset / (v->shortIndexes ? 2 : 4);
   pc.shortIndexes = v->shortIndexes;
   pc.numFaces     = v->numIndexes / 3;
   pc.numEdges     = v->numSilEdges;
   pc.silBase      = v->silOffset / 16;
   pc.facingBase   = job->facingFirst;
   pc.parmBase     = job->parmOffset / 4;
   vk.vkCmdPushConstants(cmd, vkb.shadowLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
}

/* The frame's volumes, before its render pass opens. */
void vkb_shadows_record(VkCommandBuffer cmd)
{
   vkb_frame_t *f = &vkb.frames[vkb.frameIndex];
   VkDescriptorSet *sets;
   VkDeviceSize indexes = 0, faces = 0;
   unsigned i, built = 0;

   if (!f->shadowCount || !vkb_shadows_available())
      return;
   for (i = 0; i < f->shadowCount; i++)
   {
      const vkb_shadow_volume_t *v = &f->shadowJobs[i].volume;
      indexes += (VkDeviceSize)( v->numIndexes / 3 + v->numSilEdges ) * 6;
      faces   += v->numIndexes / 3;
   }
   sets = (VkDescriptorSet *)calloc(f->shadowCount, sizeof(*sets));
   if (!sets
         || !vkb_shadow_buffer(&f->shadowIndexes, &f->shadowIndexMem, &f->shadowIndexBytes,
               indexes * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
         || !vkb_shadow_buffer(&f->shadowFacing, &f->shadowFacingMem, &f->shadowFacingBytes,
               faces * 4, 0))
   {
      free(sets);
      f->shadowCount = 0;
      return;
   }

   /* each volume's parameters and two draws in the ring, its sets */
   indexes = 0;
   faces   = 0;
   for (i = 0; i < f->shadowCount; i++)
   {
      vkb_shadow_job_t *job = &f->shadowJobs[i];
      const vkb_shadow_volume_t *v = &job->volume;
      const unsigned numFaces = v->numIndexes / 3;
      unsigned buffer, offset, p;
      uint32_t *parm = (uint32_t *)vkb_ring_alloc_frame(VKB_SHADOW_PARM_UINTS * 4, 16, &buffer, &offset);
      float *fl = (float *)parm;
      if (!parm)
         break;
      memset(parm, 0, VKB_SHADOW_PARM_UINTS * 4);
      fl[0] = v->lightOrigin[0];
      fl[1] = v->lightOrigin[1];
      fl[2] = v->lightOrigin[2];
      for (p = 0; p < 6; p++)
      {
         const unsigned base = p < 4 ? 4 : 20;
         const unsigned lane = p < 4 ? p : p - 4;
         fl[base + lane]      = v->clipPlanes[p][0];
         fl[base + 4 + lane]  = v->clipPlanes[p][1];
         fl[base + 8 + lane]  = v->clipPlanes[p][2];
         fl[base + 12 + lane] = v->clipPlanes[p][3];
      }
      parm[36] = v->cullPlanes & 15u;
      parm[37] = ( v->cullPlanes >> 4 ) & 3u;
      job->silFirst    = (unsigned)indexes;
      job->capFirst    = (unsigned)indexes + v->numSilEdges * 6;
      job->facingFirst = (unsigned)faces;
      /* VkDrawIndexedIndirectCommand: count, instances, first index, base vertex, first instance */
      parm[VKB_SHADOW_SIL_DRAW + 1] = 1;
      parm[VKB_SHADOW_SIL_DRAW + 2] = job->silFirst;
      parm[VKB_SHADOW_SIL_DRAW + 3] = v->vertexOffset / 16;
      parm[VKB_SHADOW_CAP_DRAW + 1] = 1;
      parm[VKB_SHADOW_CAP_DRAW + 2] = job->capFirst;
      parm[VKB_SHADOW_CAP_DRAW + 3] = v->vertexOffset / 16;
      job->parmOffset = offset;
      indexes += (VkDeviceSize)( numFaces + v->numSilEdges ) * 6;
      faces   += numFaces;
      sets[i] = vkb_shadow_set(f, job);
      if (!sets[i])
         break;
   }
   built = i;
   f->shadowCount = built;
   if (!built)
   {
      free(sets);
      return;
   }

   /* the frame's static uploads are in before the reads */
   vkb_shadow_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
   vk.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkb.shadowCaps);
   for (i = 0; i < built; i++)
   {
      vk.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkb.shadowLayout, 0, 1, &sets[i], 0, NULL);
      vkb_shadow_push(cmd, &f->shadowJobs[i]);
      vk.vkCmdDispatch(cmd, ( f->shadowJobs[i].volume.numIndexes / 3 + VKB_SHADOW_GROUP - 1 ) / VKB_SHADOW_GROUP, 1, 1);
   }
   vkb_shadow_barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
   vk.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkb.shadowSil);
   for (i = 0; i < built; i++)
   {
      vk.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkb.shadowLayout, 0, 1, &sets[i], 0, NULL);
      vkb_shadow_push(cmd, &f->shadowJobs[i]);
      vk.vkCmdDispatch(cmd, ( f->shadowJobs[i].volume.numSilEdges + VKB_SHADOW_GROUP - 1 ) / VKB_SHADOW_GROUP, 1, 1);
   }
   vkb_shadow_barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
         VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT);
   free(sets);
}

/* The volume's sil planes, and its caps when asked for: the pipeline and
 * set are bound, the vertex buffer is the shadow cache's. */
void vkb_shadows_draw(const vkb_draw_t *draw)
{
   const vkb_frame_t *f = &vkb.frames[vkb.frameIndex];
   const vkb_shadow_job_t *job;
   if (draw->shadowVolume > f->shadowCount)
      return;
   job = &f->shadowJobs[draw->shadowVolume - 1];
   vk.vkCmdBindIndexBuffer(vkb.cmd, f->shadowIndexes, 0, VK_INDEX_TYPE_UINT32);
   vkb.boundIndexBuffer = ~0u;
   vk.vkCmdDrawIndexedIndirect(vkb.cmd, vkb.ring, job->parmOffset + VKB_SHADOW_SIL_DRAW * 4, 1, 20);
   if (draw->shadowCaps)
      vk.vkCmdDrawIndexedIndirect(vkb.cmd, vkb.ring, job->parmOffset + VKB_SHADOW_CAP_DRAW * 4, 1, 20);
   vkb.drawn = 1;
}
