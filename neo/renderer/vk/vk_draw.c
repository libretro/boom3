/*
 * vk_draw: a draw as the engine describes it into the command buffer.
 *
 * The per-draw block is written into the ring and bound by dynamic
 * offset; the descriptor set carries the view block, the draw block and
 * the draw's textures, and a set is only written when the textures
 * change from the last draw's.  Pipeline, buffers and dynamic state are
 * bound only on a change.
 */
#include <string.h>

#include "vk_internal.h"

static unsigned ringOffsetOf(unsigned buffer, unsigned offset)
{
   (void)buffer;
   return offset;
}

static size_t vkb_block_bytes(const vkb_draw_t *draw)
{
   size_t bytes = draw->blockBytes ? draw->blockBytes : sizeof(vkb_draw_block_t);
   return bytes > VKB_BLOCK_RANGE ? VKB_BLOCK_RANGE : bytes;
}

/* the view block into the ring; every draw of the view binds it */
void vkb_set_view(const vkb_view_block_t *view)
{
   unsigned buffer, offset;
   void *p = vkb_ring_alloc_frame(sizeof(*view), VKB_DRAW_BLOCK_ALIGN, &buffer, &offset);
   if (!p)
      return;
   memcpy(p, view, sizeof(*view));
   vkb.viewOffset = ringOffsetOf(buffer, offset);
   vkb.boundSet   = VK_NULL_HANDLE;    /* the set carries the view offset */
}

/* A descriptor set for the draw's textures, or the previous draw's when
 * they match.  The uniform bindings point at the ring; their offsets are
 * dynamic. */
/* An image uploaded during the frame, for this draw - loaded on demand,
 * or a cinematic's frame: its upload is queued for the next frame's
 * start, which is too late.  The render pass closes for the copies and
 * reopens. */
static void vkb_uploads_mid_frame(const vkb_draw_t *draw)
{
   unsigned i;
   int pending = 0;
   vkt_lock();
   for (i = 0; i < VKB_MAX_TEXTURE_UNITS; i++)
   {
      const vkb_image_t *im = draw->image[i] ? vkb_image_get(draw->image[i]) : NULL;
      if (im && im->uploadPending)
         pending = 1;
   }
   vkt_unlock();
   if (!pending || !vkb.frameOpen)
      return;
   vkb_render_pass_end();
   vkb_images_flush_uploads(vkb.cmd);
   vkb_buffers_flush(vkb.cmd);
   vkb_enter_render_pass();
}

static VkDescriptorSet vkb_descriptor_set(const vkb_draw_t *draw)
{
   VkDescriptorSetAllocateInfo ai;
   VkDescriptorSet set = VK_NULL_HANDLE;
   VkWriteDescriptorSet writes[2 + VKB_MAX_TEXTURE_UNITS];
   VkDescriptorBufferInfo buffers[2];
   VkDescriptorImageInfo images[VKB_MAX_TEXTURE_UNITS];
   unsigned i, same = 1;

   /* the image table is the main thread's to change while this records */
   vkt_lock();
   for (i = 0; i < VKB_MAX_TEXTURE_UNITS; i++)
   {
      const vkb_image_t *im = draw->image[i] ? vkb_image_get(draw->image[i]) : NULL;
      unsigned sampler = im ? (unsigned)(size_t)im->sampler : 0;
      if (vkb.boundImages[i] != draw->image[i] || vkb.boundSamplers[i] != sampler)
         same = 0;
   }
   if (same && vkb.boundSet)
   {
      vkt_unlock();
      return vkb.boundSet;
   }

   memset(&ai, 0, sizeof(ai));
   ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   ai.descriptorPool     = vkb.frames[vkb.frameIndex].descriptors;
   ai.descriptorSetCount = 1;
   ai.pSetLayouts        = &vkb.setLayout;
   if (vk.vkAllocateDescriptorSets(vkb.device, &ai, &set) != VK_SUCCESS)
   {
      vkt_unlock();
      return VK_NULL_HANDLE;
   }

   memset(writes, 0, sizeof(writes));
   buffers[0].buffer = vkb.ring;
   buffers[0].offset = 0;
   buffers[0].range  = sizeof(vkb_view_block_t);
   buffers[1].buffer = vkb.ring;
   buffers[1].offset = 0;
   buffers[1].range  = VKB_BLOCK_RANGE;
   for (i = 0; i < 2; i++)
   {
      writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet          = set;
      writes[i].dstBinding      = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      writes[i].pBufferInfo     = &buffers[i];
   }
   for (i = 0; i < VKB_MAX_TEXTURE_UNITS; i++)
   {
      /* an unbound unit samples the first bound image, so every binding
       * the layout declares holds something */
      const vkb_image_t *im = draw->image[i] ? vkb_image_get(draw->image[i]) : NULL;
      if (!im || !im->view)
      {
         unsigned j;
         for (j = 0; j < VKB_MAX_TEXTURE_UNITS && (!im || !im->view); j++)
            im = draw->image[j] ? vkb_image_get(draw->image[j]) : NULL;
      }
      if (!im || !im->view)
         im = vkb_image_get(vkb.whiteImage);
      if (!im || !im->view)
      {
         vkt_unlock();
         return VK_NULL_HANDLE;
      }
      images[i].sampler     = im->sampler;
      images[i].imageView   = im->view;
      images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      writes[2 + i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[2 + i].dstSet          = set;
      writes[2 + i].dstBinding      = 2 + i;
      writes[2 + i].descriptorCount = 1;
      writes[2 + i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[2 + i].pImageInfo      = &images[i];
      vkb.boundImages[i]   = draw->image[i];
      vkb.boundSamplers[i] = (unsigned)(size_t)im->sampler;
   }
   vkt_unlock();
   vk.vkUpdateDescriptorSets(vkb.device, 2 + VKB_MAX_TEXTURE_UNITS, writes, 0, NULL);
   vkb.boundSet = set;
   return set;
}

static void vkb_bind(const vkb_draw_t *draw, VkDescriptorSet set, unsigned blockOffset)
{
   vkb_pipeline_key_t key;
   VkPipeline pipeline;
   uint32_t offsets[2];

   memset(&key, 0, sizeof(key));
   key.stateBits     = draw->stateBits;
   key.program       = draw->program;
   key.layout        = draw->layout;
   key.cull          = draw->cull;
   key.stencil       = draw->stencil;
   key.target        = vkb.target->format | (vkb.targetKind == VKB_TARGET_POST ? VKB_TARGET_POST : 0);
   key.lineMode      = draw->lineMode;
   key.polygonOffset = (draw->depthBias[0] != 0.0f || draw->depthBias[1] != 0.0f) ? 1 : 0;
   key.mirror        = draw->mirror;
   pipeline = vkb_pipeline(&key);
   if (!pipeline)
      return;
   if (pipeline != vkb.boundPipeline)
   {
      vk.vkCmdBindPipeline(vkb.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      vkb.boundPipeline = pipeline;
   }
   offsets[0] = vkb.viewOffset;
   offsets[1] = blockOffset;
   vk.vkCmdBindDescriptorSets(vkb.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkb.pipelineLayout, 0, 1, &set,
         2, offsets);
   if (key.polygonOffset)
      vk.vkCmdSetDepthBias(vkb.cmd, draw->depthBias[0], 0.0f, draw->depthBias[1]);
   if (draw->stencil != VKB_STENCIL_OFF)
   {
      vk.vkCmdSetStencilReference(vkb.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, draw->stencilRef);
      vk.vkCmdSetStencilCompareMask(vkb.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, draw->stencilMask);
      vk.vkCmdSetStencilWriteMask(vkb.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xff);
   }
}

void vkb_draw(const vkb_draw_t *draw)
{
   unsigned buffer, offset, stride;
   void *p;
   VkDescriptorSet set;
   VkBuffer vb, ib;
   if (!vkb.inRenderPass || !draw->block || !draw->indexCount)
      return;
   p = vkb_ring_alloc_frame(vkb_block_bytes(draw), VKB_DRAW_BLOCK_ALIGN, &buffer, &offset);
   if (!p)
      return;
   memcpy(p, draw->block, vkb_block_bytes(draw));
   vkb_uploads_mid_frame(draw);
   set = vkb_descriptor_set(draw);
   if (!set)
      return;
   vkb_bind(draw, set, ringOffsetOf(buffer, offset));

   vb = vkb_static_vk_buffer(draw->vertexBuffer);
   ib = vkb_static_vk_buffer(draw->indexBuffer);
   if (!vb || !ib)
      return;
   stride = draw->layout == VKB_VERTEX_SHADOW ? 16 : 60;
   if (draw->vertexBuffer != vkb.boundVertexBuffer)
   {
      VkDeviceSize zero = 0;
      vk.vkCmdBindVertexBuffers(vkb.cmd, 0, 1, &vb, &zero);
      vkb.boundVertexBuffer = draw->vertexBuffer;
   }
   if (draw->indexBuffer != vkb.boundIndexBuffer || draw->shortIndexes != vkb.boundIndexShort)
   {
      vk.vkCmdBindIndexBuffer(vkb.cmd, ib, 0, draw->shortIndexes ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
      vkb.boundIndexBuffer = draw->indexBuffer;
      vkb.boundIndexShort  = draw->shortIndexes;
   }
   vk.vkCmdDrawIndexed(vkb.cmd, draw->indexCount, 1,
         draw->indexOffset / (draw->shortIndexes ? 2 : 4), (int32_t)(draw->vertexOffset / stride), 0);
   vkb.drawn = 1;
}

/* a post pass's triangle: no vertex data, three vertexes */
void vkb_draw_fullscreen(const vkb_draw_t *draw)
{
   unsigned buffer, offset;
   void *p;
   VkDescriptorSet set;
   if (!vkb.inRenderPass || !draw->block)
      return;
   p = vkb_ring_alloc_frame(vkb_block_bytes(draw), VKB_DRAW_BLOCK_ALIGN, &buffer, &offset);
   if (!p)
      return;
   memcpy(p, draw->block, vkb_block_bytes(draw));
   vkb_uploads_mid_frame(draw);
   set = vkb_descriptor_set(draw);
   if (!set)
      return;
   vkb_bind(draw, set, ringOffsetOf(buffer, offset));
   vk.vkCmdDraw(vkb.cmd, 3, 1, 0, 0);
}

/* The render pass has to close for a copy out of the target and reopen
 * after it, with the bindings re-established. */
void vkb_leave_render_pass(void)
{
   vkb_render_pass_end();
}

void vkb_enter_render_pass(void)
{
   int viewport[4], scissor[4];
   if (vkb.inRenderPass || !vkb.frameOpen)
      return;
   memcpy(viewport, vkb.viewport, sizeof(viewport));
   scissor[0] = vkb.scissor[0];
   scissor[1] = (int)vkb.target->height - vkb.scissor[1] - vkb.scissor[3];
   scissor[2] = vkb.scissor[2];
   scissor[3] = vkb.scissor[3];
   vkb_render_pass_begin();
   vkb_viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
   vkb_scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
}
