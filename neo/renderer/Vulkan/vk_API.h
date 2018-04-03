#ifndef ID_VK_API_H_
#define ID_VK_API_H_

#include "../tr_local.h"

#ifdef DOOM3_VULKAN

#include <vulkan/vulkan.h>

//Startup/shutdown
typedef glimpParms_t VkImpParams_t;
bool		VkImp_Init( VkImpParams_t params );
void VkImp_Shutdown();
bool R_IsVulkanAvailable();

//Active handles
VkCommandBuffer Vk_ActiveCommandBuffer();
VkCommandBuffer Vk_CurrentBackendCommandBuffer();
VkCommandBuffer Vk_CurrentUniformCommandBuffer();
VkImage Vk_ActiveColorBuffer();
VkImage Vk_ActiveDepthBuffer();
VkDevice Vk_GetDevice();
VkPipelineLayout Vk_GetPipelineLayout();
VkDescriptorSet Vk_UniformDescriptorSet();

//Frame/renderpass start/end/clear
VkCommandBuffer Vk_StartFrame();
void Vk_EndFrame();
VkCommandBuffer Vk_StartRenderPass();
void Vk_EndRenderPass();
void Vk_FlipPresent();
void Vk_ClearAttachments(uint32_t mask, byte stencilValue = 0);

//Command buffers
VkCommandBuffer Vk_StartOneShotCommandBuffer();
void Vk_SubmitOneShotCommandBuffer(VkCommandBuffer cmd);

//Image and buffer functions
VkBuffer Vk_CreateAndBindBuffer(const VkBufferCreateInfo& info,
	VkMemoryPropertyFlags flags, VkDeviceMemory& memory);
VkImage Vk_AllocAndCreateImage(const VkImageCreateInfo& info);
VkDescriptorSet Vk_AllocDescriptorSetForImage();
VkImageView Vk_CreateImageView(const VkImageViewCreateInfo& info);
void Vk_SetImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout,
	VkImageLayout newLayout, VkImageSubresourceRange& range);
void Vk_DestroyImageAndView(VkImage image, VkImageView imageView);
void Vk_DestroyBuffer(VkBuffer buffer);
void Vk_FreeMemory(VkDeviceMemory memory);
uint32_t Vk_GetMemoryTypeIndex(uint32_t bits, VkMemoryPropertyFlags flags);

//Pipelines
VkPipeline Vk_CreatePipeline(VkGraphicsPipelineCreateInfo& info);
VkShaderModule Vk_CreateShaderModule(const char* bytes, size_t length);
void Vk_UsePipeline(VkPipeline p);

//Memory mapping
void* Vk_MapMemory(VkDeviceMemory memory, VkDeviceSize offset,
	VkDeviceSize size, VkFlags flags);
void Vk_UnmapMemory(VkDeviceMemory memory);

//Descriptor sets
void Vk_CreateUniformBuffer(VkDeviceMemory& stagingMemory, VkBuffer& stagingBuffer,
	VkDeviceMemory& memory, VkBuffer& buffer, VkDeviceSize size);
void Vk_UpdateDescriptorSet(VkWriteDescriptorSet& write);
VkDescriptorSet Vk_AllocateJointBufferSetForFrame(int idx, geoBufferSet_t& gbs);
VkDescriptorSet Vk_JointBufferSetForFrame(int idx);
void Vk_FreeDescriptorSet(const VkDescriptorSet set);
void Vk_QueueDestroyDescriptorSet(VkDescriptorSet set);

//Antialiasing
VkSampleCountFlagBits Vk_MaxSupportedSampleCount();
VkSampleCountFlagBits Vk_SampleCount();

//Perf timers
void Vk_StartFrameTimeCounter();
void Vk_EndFrameTimeCounter();
uint64_t Vk_GetFrameTimeCounter();

#endif //DOOM3_VULKAN

#endif //ID_VK_API_H_