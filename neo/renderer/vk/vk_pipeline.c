/*
 * vk_pipeline: the render passes, the one descriptor set layout every
 * program shares, the shader modules from the committed SPIR-V, and the
 * graphics pipelines, made on first use for each combination of program
 * and state the engine draws with.
 */
#include <string.h>

#include "vk_internal.h"

#include "spirv/zfill_vert.h"
#include "spirv/zfill_opaque_frag.h"
#include "spirv/zfill_alpha_frag.h"
#include "spirv/zfill_clip_frag.h"
#include "spirv/diffuse_vert.h"
#include "spirv/diffuse_frag.h"
#include "spirv/diffuse_gui_frag.h"
#include "spirv/skybox_vert.h"
#include "spirv/reflect_vert.h"
#include "spirv/diffuse_cube_vert.h"
#include "spirv/cube_frag.h"
#include "spirv/blit_vert.h"
#include "spirv/blit_frag.h"

/* the engine's state bits (tr_local.h) */
#define GLS_SRCBLEND_BITS      0x0000000f
#define GLS_DSTBLEND_BITS      0x000000f0
#define GLS_DEPTHMASK          0x00000100
#define GLS_REDMASK            0x00000200
#define GLS_GREENMASK          0x00000400
#define GLS_BLUEMASK           0x00000800
#define GLS_ALPHAMASK          0x00001000
#define GLS_POLYMODE_LINE      0x00002000
#define GLS_DEPTHFUNC_ALWAYS   0x00010000
#define GLS_DEPTHFUNC_EQUAL    0x00020000

typedef struct vkb_program_source
{
   const unsigned int *vert;
   size_t vertBytes;
   const unsigned int *frag;
   size_t fragBytes;
} vkb_program_source_t;

#define SPV(name) spirv_##name, sizeof(spirv_##name)

static const vkb_program_source_t sources[VKB_PROGRAM_COUNT] = {
   { SPV(zfill_vert), SPV(zfill_opaque_frag) },
   { SPV(zfill_vert), SPV(zfill_alpha_frag) },
   { SPV(zfill_vert), SPV(zfill_clip_frag) },
   { SPV(diffuse_vert), SPV(diffuse_frag) },
   { SPV(diffuse_vert), SPV(diffuse_gui_frag) },
   { SPV(skybox_vert), SPV(cube_frag) },
   { SPV(reflect_vert), SPV(cube_frag) },
   { SPV(diffuse_cube_vert), SPV(cube_frag) },
   { SPV(blit_vert), SPV(blit_frag) }
};

static VkShaderModule vkb_module(const unsigned int *code, size_t bytes)
{
   VkShaderModuleCreateInfo ci;
   VkShaderModule m = VK_NULL_HANDLE;
   memset(&ci, 0, sizeof(ci));
   ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   ci.codeSize = bytes;
   ci.pCode    = code;
   if (vk.vkCreateShaderModule(vkb.device, &ci, NULL, &m) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   return m;
}

/* The render pass for a colour format: colour and depth-stencil, both
 * loaded (the engine clears what it clears per view) and kept.  The
 * frontend's image ends readable; the scene's too, for the composite. */
VkRenderPass vkb_render_pass(enum vkb_format colorFormat, int frontend)
{
   VkAttachmentDescription att[2];
   VkAttachmentReference colorRef, depthRef;
   VkSubpassDescription sub;
   VkSubpassDependency deps[2];
   VkRenderPassCreateInfo rci;
   VkRenderPass *slot = &vkb.renderPass[colorFormat];
   (void)frontend;
   if (*slot)
      return *slot;

   memset(att, 0, sizeof(att));
   att[0].format         = vkb_vk_format(colorFormat);
   att[0].samples        = VK_SAMPLE_COUNT_1_BIT;
   att[0].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
   att[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
   att[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   att[0].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   att[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   att[1].format         = vkb_depth_stencil_format();
   att[1].samples        = VK_SAMPLE_COUNT_1_BIT;
   att[1].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
   att[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
   att[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
   att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
   att[1].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   att[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   colorRef.attachment = 0;
   colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   depthRef.attachment = 1;
   depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   memset(&sub, 0, sizeof(sub));
   sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
   sub.colorAttachmentCount    = 1;
   sub.pColorAttachments       = &colorRef;
   sub.pDepthStencilAttachment = &depthRef;

   memset(deps, 0, sizeof(deps));
   deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
   deps[0].dstSubpass    = 0;
   deps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
   deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
   deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
      | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
   deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
      | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
   deps[1].srcSubpass    = 0;
   deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
   deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
   deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
   deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
   deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

   memset(&rci, 0, sizeof(rci));
   rci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rci.attachmentCount = 2;
   rci.pAttachments    = att;
   rci.subpassCount    = 1;
   rci.pSubpasses      = &sub;
   rci.dependencyCount = 2;
   rci.pDependencies   = deps;
   if (vk.vkCreateRenderPass(vkb.device, &rci, NULL, slot) != VK_SUCCESS)
      *slot = VK_NULL_HANDLE;
   return *slot;
}

int vkb_pipelines_init(void)
{
   VkDescriptorSetLayoutBinding bindings[2 + VKB_MAX_TEXTURE_UNITS];
   VkDescriptorSetLayoutCreateInfo dci;
   VkPipelineLayoutCreateInfo pci;
   unsigned i;

   memset(bindings, 0, sizeof(bindings));
   bindings[0].binding         = 0;
   bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
   bindings[0].descriptorCount = 1;
   bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
   bindings[1]                 = bindings[0];
   bindings[1].binding         = 1;
   for (i = 0; i < VKB_MAX_TEXTURE_UNITS; i++)
   {
      bindings[2 + i].binding         = 2 + i;
      bindings[2 + i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      bindings[2 + i].descriptorCount = 1;
      bindings[2 + i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
   }
   memset(&dci, 0, sizeof(dci));
   dci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dci.bindingCount = 2 + VKB_MAX_TEXTURE_UNITS;
   dci.pBindings    = bindings;
   if (vk.vkCreateDescriptorSetLayout(vkb.device, &dci, NULL, &vkb.setLayout) != VK_SUCCESS)
      return 0;
   memset(&pci, 0, sizeof(pci));
   pci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pci.setLayoutCount = 1;
   pci.pSetLayouts    = &vkb.setLayout;
   if (vk.vkCreatePipelineLayout(vkb.device, &pci, NULL, &vkb.pipelineLayout) != VK_SUCCESS)
      return 0;

   for (i = 0; i < VKB_PROGRAM_COUNT; i++)
   {
      vkb.modules[i][0] = vkb_module(sources[i].vert, sources[i].vertBytes);
      vkb.modules[i][1] = vkb_module(sources[i].frag, sources[i].fragBytes);
      if (!vkb.modules[i][0] || !vkb.modules[i][1])
         return 0;
   }
   vkb.pipelineCount = 0;
   return 1;
}

void vkb_pipelines_shutdown(void)
{
   unsigned i;
   for (i = 0; i < vkb.pipelineCount; i++)
      if (vkb.pipelines[i].pipeline)
         vk.vkDestroyPipeline(vkb.device, vkb.pipelines[i].pipeline, NULL);
   vkb.pipelineCount = 0;
   for (i = 0; i < VKB_PROGRAM_COUNT; i++)
   {
      if (vkb.modules[i][0])
         vk.vkDestroyShaderModule(vkb.device, vkb.modules[i][0], NULL);
      if (vkb.modules[i][1])
         vk.vkDestroyShaderModule(vkb.device, vkb.modules[i][1], NULL);
      vkb.modules[i][0] = vkb.modules[i][1] = VK_NULL_HANDLE;
   }
   for (i = 0; i < VKB_FORMAT_COUNT; i++)
      if (vkb.renderPass[i])
      {
         vk.vkDestroyRenderPass(vkb.device, vkb.renderPass[i], NULL);
         vkb.renderPass[i] = VK_NULL_HANDLE;
      }
   if (vkb.pipelineLayout)
      vk.vkDestroyPipelineLayout(vkb.device, vkb.pipelineLayout, NULL);
   if (vkb.setLayout)
      vk.vkDestroyDescriptorSetLayout(vkb.device, vkb.setLayout, NULL);
   vkb.pipelineLayout = VK_NULL_HANDLE;
   vkb.setLayout      = VK_NULL_HANDLE;
}

static VkBlendFactor vkb_src_factor(unsigned bits)
{
   switch (bits & GLS_SRCBLEND_BITS)
   {
      case 0x1: return VK_BLEND_FACTOR_ZERO;
      case 0x3: return VK_BLEND_FACTOR_DST_COLOR;
      case 0x4: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
      case 0x5: return VK_BLEND_FACTOR_SRC_ALPHA;
      case 0x6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      case 0x7: return VK_BLEND_FACTOR_DST_ALPHA;
      case 0x8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
      case 0x9: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
      default:  return VK_BLEND_FACTOR_ONE;
   }
}

static VkBlendFactor vkb_dst_factor(unsigned bits)
{
   switch (bits & GLS_DSTBLEND_BITS)
   {
      case 0x20: return VK_BLEND_FACTOR_ONE;
      case 0x30: return VK_BLEND_FACTOR_SRC_COLOR;
      case 0x40: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
      case 0x50: return VK_BLEND_FACTOR_SRC_ALPHA;
      case 0x60: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      case 0x70: return VK_BLEND_FACTOR_DST_ALPHA;
      case 0x80: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
      default:   return VK_BLEND_FACTOR_ZERO;
   }
}

static VkPipeline vkb_pipeline_make(const vkb_pipeline_key_t *key)
{
   VkPipelineShaderStageCreateInfo stages[2];
   VkVertexInputBindingDescription binding;
   VkVertexInputAttributeDescription attrs[6];
   VkPipelineVertexInputStateCreateInfo vi;
   VkPipelineInputAssemblyStateCreateInfo ia;
   VkPipelineViewportStateCreateInfo vp;
   VkPipelineRasterizationStateCreateInfo rs;
   VkPipelineMultisampleStateCreateInfo ms;
   VkPipelineDepthStencilStateCreateInfo ds;
   VkPipelineColorBlendAttachmentState blend;
   VkPipelineColorBlendStateCreateInfo cb;
   VkDynamicState dynamics[6];
   VkPipelineDynamicStateCreateInfo dy;
   VkGraphicsPipelineCreateInfo gci;
   VkPipeline pipeline = VK_NULL_HANDLE;
   unsigned stateBits = key->stateBits;
   unsigned numAttrs;
   int frontCCW;

   memset(stages, 0, sizeof(stages));
   stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
   stages[0].module = vkb.modules[key->program][0];
   stages[0].pName  = "main";
   stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
   stages[1].module = vkb.modules[key->program][1];
   stages[1].pName  = "main";

   memset(&binding, 0, sizeof(binding));
   memset(attrs, 0, sizeof(attrs));
   binding.binding   = 0;
   binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   if (key->layout == VKB_VERTEX_SHADOW)
   {
      binding.stride     = 16;
      attrs[0].location  = 0;
      attrs[0].format    = VK_FORMAT_R32G32B32A32_SFLOAT;
      attrs[0].offset    = 0;
      numAttrs           = 1;
   }
   else
   {
      /* idDrawVert: xyz 0, st 12, normal 20, tangents 32 and 44, color 56 */
      binding.stride     = 60;
      attrs[0].location  = 0;
      attrs[0].format    = VK_FORMAT_R32G32B32_SFLOAT;
      attrs[0].offset    = 0;
      attrs[1].location  = 1;
      attrs[1].format    = VK_FORMAT_R8G8B8A8_UNORM;
      attrs[1].offset    = 56;
      attrs[2].location  = 2;
      attrs[2].format    = VK_FORMAT_R32G32_SFLOAT;
      attrs[2].offset    = 12;
      attrs[3].location  = 3;
      attrs[3].format    = VK_FORMAT_R32G32B32_SFLOAT;
      attrs[3].offset    = 20;
      attrs[4].location  = 4;
      attrs[4].format    = VK_FORMAT_R32G32B32_SFLOAT;
      attrs[4].offset    = 32;
      attrs[5].location  = 5;
      attrs[5].format    = VK_FORMAT_R32G32B32_SFLOAT;
      attrs[5].offset    = 44;
      numAttrs           = 6;
   }
   memset(&vi, 0, sizeof(vi));
   vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   vi.vertexBindingDescriptionCount   = key->program == VKB_PROGRAM_BLIT ? 0 : 1;
   vi.pVertexBindingDescriptions      = &binding;
   vi.vertexAttributeDescriptionCount = key->program == VKB_PROGRAM_BLIT ? 0 : numAttrs;
   vi.pVertexAttributeDescriptions    = attrs;

   memset(&ia, 0, sizeof(ia));
   ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

   memset(&vp, 0, sizeof(vp));
   vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   vp.viewportCount = 1;
   vp.scissorCount  = 1;

   /* The engine's front faces are counter-clockwise in GL's y-up window;
    * in Vulkan's y-down framebuffer the same picture winds clockwise.  A
    * mirrored view inverts it. */
   frontCCW = key->mirror;
   memset(&rs, 0, sizeof(rs));
   rs.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
   rs.depthClampEnable = vkb.features.depthClamp;
   rs.polygonMode      = (key->lineMode || (stateBits & GLS_POLYMODE_LINE)) && vkb.features.fillModeNonSolid
      ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
   rs.cullMode         = key->cull == VKB_CULL_BACK ? VK_CULL_MODE_BACK_BIT
      : key->cull == VKB_CULL_FRONT ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
   rs.frontFace        = frontCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
   rs.depthBiasEnable  = key->polygonOffset ? VK_TRUE : VK_FALSE;
   rs.lineWidth        = 1.0f;

   memset(&ms, 0, sizeof(ms));
   ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

   memset(&ds, 0, sizeof(ds));
   ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
   ds.depthTestEnable  = VK_TRUE;
   ds.depthWriteEnable = (stateBits & GLS_DEPTHMASK) ? VK_FALSE : VK_TRUE;
   ds.depthCompareOp   = (stateBits & GLS_DEPTHFUNC_ALWAYS) ? VK_COMPARE_OP_ALWAYS
      : (stateBits & GLS_DEPTHFUNC_EQUAL) ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
   if (key->stencil != VKB_STENCIL_OFF)
   {
      VkStencilOpState front, back;
      memset(&front, 0, sizeof(front));
      front.compareOp = VK_COMPARE_OP_ALWAYS;
      front.failOp = front.passOp = front.depthFailOp = VK_STENCIL_OP_KEEP;
      back = front;
      switch (key->stencil)
      {
         case VKB_STENCIL_TEST:
            front.compareOp = back.compareOp = VK_COMPARE_OP_EQUAL;
            break;
         case VKB_STENCIL_SHADOW_PASS:
            front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
            back.passOp  = VK_STENCIL_OP_DECREMENT_AND_WRAP;
            break;
         case VKB_STENCIL_SHADOW_FAIL:
            front.depthFailOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
            back.depthFailOp  = VK_STENCIL_OP_INCREMENT_AND_WRAP;
            break;
         default:
            break;
      }
      ds.stencilTestEnable = VK_TRUE;
      ds.front = front;
      ds.back  = back;
   }

   memset(&blend, 0, sizeof(blend));
   blend.blendEnable         = ((stateBits & GLS_SRCBLEND_BITS) != 0 || (stateBits & GLS_DSTBLEND_BITS) != 0)
      ? VK_TRUE : VK_FALSE;
   blend.srcColorBlendFactor = vkb_src_factor(stateBits);
   blend.dstColorBlendFactor = vkb_dst_factor(stateBits);
   blend.colorBlendOp        = VK_BLEND_OP_ADD;
   blend.srcAlphaBlendFactor = blend.srcColorBlendFactor;
   blend.dstAlphaBlendFactor = blend.dstColorBlendFactor;
   blend.alphaBlendOp        = VK_BLEND_OP_ADD;
   blend.colorWriteMask      = ((stateBits & GLS_REDMASK) ? 0 : VK_COLOR_COMPONENT_R_BIT)
      | ((stateBits & GLS_GREENMASK) ? 0 : VK_COLOR_COMPONENT_G_BIT)
      | ((stateBits & GLS_BLUEMASK) ? 0 : VK_COLOR_COMPONENT_B_BIT)
      | ((stateBits & GLS_ALPHAMASK) ? 0 : VK_COLOR_COMPONENT_A_BIT);
   memset(&cb, 0, sizeof(cb));
   cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   cb.attachmentCount = 1;
   cb.pAttachments    = &blend;

   dynamics[0] = VK_DYNAMIC_STATE_VIEWPORT;
   dynamics[1] = VK_DYNAMIC_STATE_SCISSOR;
   dynamics[2] = VK_DYNAMIC_STATE_DEPTH_BIAS;
   dynamics[3] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
   dynamics[4] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
   dynamics[5] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
   memset(&dy, 0, sizeof(dy));
   dy.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   dy.dynamicStateCount = 6;
   dy.pDynamicStates    = dynamics;

   memset(&gci, 0, sizeof(gci));
   gci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   gci.stageCount          = 2;
   gci.pStages             = stages;
   gci.pVertexInputState   = &vi;
   gci.pInputAssemblyState = &ia;
   gci.pViewportState      = &vp;
   gci.pRasterizationState = &rs;
   gci.pMultisampleState   = &ms;
   gci.pDepthStencilState  = &ds;
   gci.pColorBlendState    = &cb;
   gci.pDynamicState       = &dy;
   gci.layout              = vkb.pipelineLayout;
   gci.renderPass          = vkb.renderPass[key->target];
   gci.subpass             = 0;
   if (vk.vkCreateGraphicsPipelines(vkb.device, vkb.pipelineCache, 1, &gci, NULL, &pipeline) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   return pipeline;
}

/* The pipeline for a key, made on first use.  A linear table: a frame
 * reuses a few dozen pipelines and the lookup is a memcmp each. */
VkPipeline vkb_pipeline(const vkb_pipeline_key_t *key)
{
   unsigned i;
   for (i = 0; i < vkb.pipelineCount; i++)
      if (memcmp(&vkb.pipelines[i].key, key, sizeof(*key)) == 0)
         return vkb.pipelines[i].pipeline;
   if (vkb.pipelineCount == VKB_PIPELINE_SLOTS)
      return VK_NULL_HANDLE;
   vkb.pipelines[vkb.pipelineCount].key      = *key;
   vkb.pipelines[vkb.pipelineCount].pipeline = vkb_pipeline_make(key);
   return vkb.pipelines[vkb.pipelineCount++].pipeline;
}
