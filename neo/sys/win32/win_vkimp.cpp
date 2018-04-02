#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "win_local.h"
#include "rc/doom_resource.h"
#include "../../renderer/tr_local.h"

#ifdef DOOM3_VULKAN
#include <vulkan/vulkan.h>
#include <set>

#include "win_gfxcommon.h"
#include "win_vkutil.h"
#include "../../renderer/Vulkan/vk_MemPool.h"

VkInstance vkInstance = VK_NULL_HANDLE;
VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
VkPhysicalDevice vkPhysicalDevice = VK_NULL_HANDLE;
VkPhysicalDeviceProperties vkPhysicalDeviceProperties;
VkPhysicalDeviceFeatures vkPhysicalDeviceFeatures;
VkDevice vkDevice = VK_NULL_HANDLE;	
VkCommandPool vkCommandPool = VK_NULL_HANDLE;
VkSwapchainKHR vkSwapchain = VK_NULL_HANDLE;
VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
VkDescriptorSet vkDescriptorSet = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet vkJointDescriptorSets[VERTCACHE_NUM_FRAMES];
VkDebugReportCallbackEXT vkDebugCallback;

struct QueueInfo
{
	VkQueue vkQueue;
	uint32_t index;
};

QueueInfo vkGraphicsQueue;
QueueInfo vkPresentQueue;

struct RenderImage
{
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct Framebuffer
{
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

struct RenderPass
{
	RenderImage color;
	RenderImage depth;
	RenderImage resolve;
	Framebuffer fb;

	VkCommandBuffer cmd;
	VkSemaphore semaphore;
	VkRenderPass handle;
};

RenderPass screenRenderPass;
RenderPass offscreenRenderPass;
RenderPass* activeRenderPass = nullptr;

std::vector<Framebuffer> framebuffers;
std::vector<VkCommandBuffer> commandBuffers;
std::vector<VkCommandBuffer> backEndCommandBuffers;
std::vector<VkCommandBuffer> uniformCommandBuffers;
std::vector<VkFence> fences;

VkSurfaceCapabilitiesKHR surfaceCaps;

VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
VkSemaphore renderingFinishedSemaphore = VK_NULL_HANDLE;
VkSemaphore backEndFinishedSemaphore = VK_NULL_HANDLE;
VkSemaphore uniformSyncFinishedSemaphore = VK_NULL_HANDLE;

uint32_t activeCommandBufferIdx = -1;

VkDescriptorSetLayout uniformSetLayout;
VkDescriptorSetLayout imageSetLayout;
VkDescriptorSetLayout jointSetLayout;

std::vector<VkDescriptorSet> destructionQueue;

VkQueryPool queryPool;

idMemPoolVk imageMemPool;

const uint32_t MAX_IMAGE_DESC_SETS = 8192;

static void CreateVulkanContextOnHWND(HWND hwnd, bool isDebug)
{
	VkWin32SurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hinstance = GetModuleHandle(NULL);
	createInfo.hwnd = hwnd;
	
	VkCheck(vkCreateWin32SurfaceKHR(vkInstance, &createInfo, nullptr, &vkSurface));
}

static void Vk_CreateQueryPool()
{
	VkQueryPoolCreateInfo create = {};
	create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	create.queryCount = 2;
	create.queryType = VK_QUERY_TYPE_TIMESTAMP;
	
	VkCheck(vkCreateQueryPool(vkDevice, &create, nullptr, &queryPool));
}

static void Vk_CreateInstance()
{
	VkApplicationInfo applicationInfo = {};
	applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	applicationInfo.apiVersion = VK_API_VERSION_1_0;
	applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	applicationInfo.pApplicationName = "DOOM 3 BFG Edition";
	applicationInfo.pEngineName = "DOOM3";
	applicationInfo.pNext = nullptr;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &applicationInfo;

	std::vector<const char*> extensions;
	VulkanUtil::getRequiredExtensions(extensions);
	createInfo.enabledExtensionCount = (uint32_t)extensions.size();
	createInfo.ppEnabledExtensionNames = extensions.data();

	if (VulkanUtil::DEBUGENABLE)
	{
		const char* validationLayerNames[] = {
			"VK_LAYER_LUNARG_standard_validation"
		};

		createInfo.enabledLayerCount = 1;
		createInfo.ppEnabledLayerNames = validationLayerNames;
	}
	else
	{
		createInfo.enabledLayerCount = 0;
	}
	
	vkCreateInstance(&createInfo, nullptr, &vkInstance);
}

bool R_IsVulkanAvailable()
{
	if(vkInstance == VK_NULL_HANDLE)
		Vk_CreateInstance();

	return (vkInstance != VK_NULL_HANDLE);
}

static int Vk_ChoosePixelFormat(const HDC hdc, const int multisamples, const bool stereo3D) {
	return -1;
}

static void Vk_PickPhysicalDevice()
{
	uint32_t physicalDeviceCount;
	VkCheck(vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, nullptr));

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

	if (physicalDeviceCount > 0)
	{
		VkPhysicalDevice* physicalDevices = new VkPhysicalDevice[physicalDeviceCount];
		VkCheck(vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, physicalDevices));
		physicalDevice = physicalDevices[0]; //default to any GPU in case it's all we have.


		//Pick the first discrete GPU we encounter.
		for (uint32_t i = 0; i < physicalDeviceCount; i++)
		{
			vkGetPhysicalDeviceProperties(physicalDevices[i], &vkPhysicalDeviceProperties);

			if (vkPhysicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				physicalDevice = physicalDevices[i];
				glConfig.maxTextureImageUnits = vkPhysicalDeviceProperties.limits.maxPerStageDescriptorSampledImages;
				glConfig.depthBoundsTestAvailable = true;

				VkFormatProperties props;
				vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D32_SFLOAT_S8_UINT, &props);
				//TODO: fall back to other formats.
				if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
				{
					idLib::FatalError("Depth stencil format not supported");
				}

				vkGetPhysicalDeviceFeatures(physicalDevice, &vkPhysicalDeviceFeatures);

				break;
			}
		}

		delete[] physicalDevices;
	}

	vkPhysicalDevice = physicalDevice;
}

static void Vk_QueryDeviceQueueFamilies(VkPhysicalDevice device)
{
	vkGraphicsQueue.index = -1, vkPresentQueue.index = -1;
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families.data());

	for (uint32_t i = 0; i < queueFamilyCount; i++)
	{
		if (families[i].queueCount > 0 && families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			 vkGraphicsQueue.index = i;
		}

		VkBool32 presentSupport;
		VkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vkSurface, &presentSupport));

		if (families[i].queueCount > 0 && presentSupport) {
			vkPresentQueue.index = i;
		}

		if (vkGraphicsQueue.index != -1 && vkPresentQueue.index != -1)
			break;
	}
}

static void Vk_InitDevice()
{
	VkDeviceQueueCreateInfo queueCreateInfo = {};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;

	std::vector<const char*> extensions = { 
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	VkDeviceCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info.pEnabledFeatures = &vkPhysicalDeviceFeatures;
	info.ppEnabledExtensionNames = extensions.data();
	info.enabledExtensionCount = (uint32_t)extensions.size();

	const float priority = 1.0f;
	Vk_QueryDeviceQueueFamilies(vkPhysicalDevice);
	
	std::vector<VkDeviceQueueCreateInfo> queryInfos;
	std::set<uint32_t> uniqueFamilies = { vkGraphicsQueue.index, vkPresentQueue.index };

	for (uint32_t family : uniqueFamilies)
	{
		VkDeviceQueueCreateInfo dqInfo = {};
		dqInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		dqInfo.queueCount = 1;
		dqInfo.queueFamilyIndex = family;
		dqInfo.pQueuePriorities = &priority;
		queryInfos.push_back(dqInfo);
	}

	info.queueCreateInfoCount = (uint32_t)queryInfos.size();
	info.pQueueCreateInfos = queryInfos.data();

	if (VulkanUtil::DEBUGENABLE)
	{
		info.enabledLayerCount = (uint32_t)VulkanUtil::VALIDATION_LAYERS.size();
		info.ppEnabledLayerNames = VulkanUtil::VALIDATION_LAYERS.data();
	}

	VkCheck(vkCreateDevice(vkPhysicalDevice, &info, nullptr, &vkDevice));

	vkGetDeviceQueue(vkDevice, vkGraphicsQueue.index, 0, &vkGraphicsQueue.vkQueue);
	vkGetDeviceQueue(vkDevice, vkPresentQueue.index, 0, &vkPresentQueue.vkQueue);
}

static void Vk_InitCommandPool()
{
	VkCommandPoolCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	info.queueFamilyIndex = vkGraphicsQueue.index;

	VkCheck(vkCreateCommandPool(vkDevice, &info, nullptr, &vkCommandPool));
}

static void Vk_PopulateSwapChainInfo()
{
	VkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkPhysicalDevice, vkSurface, &surfaceCaps));

	uint32_t formatCount;
	VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(vkPhysicalDevice, vkSurface, &formatCount, nullptr));

	std::vector<VkSurfaceFormatKHR> surfaceFormats;
	if (formatCount > 0)
	{
		surfaceFormats.resize(formatCount);
		VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(vkPhysicalDevice, vkSurface, &formatCount, surfaceFormats.data()));
	}

	uint32_t presentModeCount;
	VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(vkPhysicalDevice, vkSurface, &presentModeCount, nullptr));
	
	std::vector<VkPresentModeKHR> presentModes;
	if (presentModeCount > 0)
	{
		presentModes.resize(presentModeCount);
		VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(vkPhysicalDevice, vkSurface, &presentModeCount, presentModes.data()));
	}
}

uint32_t Vk_GetMemoryTypeIndex(uint32_t bits, VkMemoryPropertyFlags flags) 
{
	VkPhysicalDeviceMemoryProperties props;
	vkGetPhysicalDeviceMemoryProperties(vkPhysicalDevice, &props);

	for (uint32_t i = 0; i < props.memoryTypeCount; i++)
	{
		if (((props.memoryTypes[i].propertyFlags & flags) == flags) && (bits & (1 << i)))
		{
			return i;
		}
	}

	return -1;
}

static void Vk_CreateResolveImage(RenderImage* renderImage, 
	VkImageUsageFlagBits usage, VkFormat format, VkImageAspectFlags aspectMask,
	VkImageLayout dstLayout)
{
	if (renderImage->view != VK_NULL_HANDLE)
		vkDestroyImageView(vkDevice, renderImage->view, nullptr);

	if (renderImage->image != VK_NULL_HANDLE)
		vkDestroyImage(vkDevice, renderImage->image, nullptr);

	if (renderImage->memory != VK_NULL_HANDLE)
		vkFreeMemory(vkDevice, renderImage->memory, nullptr);

	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.extent.width = surfaceCaps.currentExtent.width;
	info.extent.height = surfaceCaps.currentExtent.height;
	info.extent.depth = 1;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.samples = Vk_SampleCount();
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.format = format;
	info.imageType = VK_IMAGE_TYPE_2D;

	VkCheck(vkCreateImage(vkDevice, &info, nullptr, &renderImage->image));

	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements(vkDevice, renderImage->image, &memReq);

	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = memReq.size;
	alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits, 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkCheck(vkAllocateMemory(vkDevice, &alloc, nullptr, &renderImage->memory));
	VkCheck(vkBindImageMemory(vkDevice, renderImage->image, renderImage->memory, 0));

	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.format = format;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.image = renderImage->image;
	view.subresourceRange.aspectMask = aspectMask;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;
	view.subresourceRange.levelCount = 1;

	VkCheck(vkCreateImageView(vkDevice, &view, nullptr, &renderImage->view));

	VkImageSubresourceRange range = {};
	range.aspectMask = aspectMask;
	range.layerCount = 1;
	range.levelCount = 1;
	Vk_SetImageLayout(renderImage->image, format, VK_IMAGE_LAYOUT_UNDEFINED, 
		dstLayout, range);
}

static void Vk_CreateDepthBuffer(RenderImage* renderImage)
{
	if (renderImage->view != VK_NULL_HANDLE)
		vkDestroyImageView(vkDevice, renderImage->view, nullptr);

	if (renderImage->image != VK_NULL_HANDLE)
		vkDestroyImage(vkDevice, renderImage->image, nullptr);
	
	if (renderImage->memory != VK_NULL_HANDLE)
		vkFreeMemory(vkDevice, renderImage->memory, nullptr);

	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.extent.width = surfaceCaps.currentExtent.width;
	info.extent.height = surfaceCaps.currentExtent.height;
	info.extent.depth = 1;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.samples = Vk_SampleCount();
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	info.imageType = VK_IMAGE_TYPE_2D;

	VkCheck(vkCreateImage(vkDevice, &info, nullptr, &renderImage->image));

	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements(vkDevice, renderImage->image, &memReq);

	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = memReq.size;
	alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkCheck(vkAllocateMemory(vkDevice, &alloc, nullptr, &renderImage->memory));
	VkCheck(vkBindImageMemory(vkDevice, renderImage->image, renderImage->memory, 0));

	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.image = renderImage->image;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;
	view.subresourceRange.levelCount = 1;

	VkCheck(vkCreateImageView(vkDevice, &view, nullptr, &renderImage->view));

	VkImageSubresourceRange range = {};
	range.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;
	range.layerCount = 1;
	range.levelCount = 1;
	Vk_SetImageLayout(renderImage->image,
		VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, range);
}

static void Vk_CreateOffscreenRenderPass()
{
	//Create offscreen buffer
	{
		RenderImage* renderImage = &offscreenRenderPass.color;

		VkImageCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.extent.width = surfaceCaps.currentExtent.width;
		info.extent.height = surfaceCaps.currentExtent.height;
		info.extent.depth = 1;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.format = VK_FORMAT_B8G8R8A8_UNORM;
		info.imageType = VK_IMAGE_TYPE_2D;

		VkCheck(vkCreateImage(vkDevice, &info, nullptr, &renderImage->image));

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(vkDevice, renderImage->image, &memReq);

		VkMemoryAllocateInfo alloc = {};
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.allocationSize = memReq.size;
		alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VkCheck(vkAllocateMemory(vkDevice, &alloc, nullptr, &renderImage->memory));
		VkCheck(vkBindImageMemory(vkDevice, renderImage->image, renderImage->memory, 0));


		VkImageViewCreateInfo viewCreateInfo = {};
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.image = renderImage->image;
		viewCreateInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

		viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCreateInfo.subresourceRange.baseMipLevel = 0;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.baseArrayLayer = 0;
		viewCreateInfo.subresourceRange.layerCount = 1;

		VkCheck(vkCreateImageView(vkDevice, &viewCreateInfo, nullptr, &renderImage->view));
	}

	//Create offscreen depth
	Vk_CreateDepthBuffer(&offscreenRenderPass.depth);

	//Create framebuffers
	{
		VkExtent2D extent = surfaceCaps.currentExtent;


		VkFramebufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.width = extent.width;
		info.height = extent.height;
		info.attachmentCount = 2;
		info.layers = 1;
		info.renderPass = screenRenderPass.handle;

		const VkImageView offscreenAttachments[] = {
			offscreenRenderPass.color.view, offscreenRenderPass.depth.view
		};

		info.pAttachments = offscreenAttachments;
		info.renderPass = screenRenderPass.handle;
		VkCheck(vkCreateFramebuffer(vkDevice, &info, nullptr, &offscreenRenderPass.fb.framebuffer));
	}

	//Color
	VkAttachmentDescription attachDesc = {};
	attachDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachDesc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	attachDesc.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	attachDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	//Color
	VkAttachmentReference attachRef = {};
	attachRef.attachment = 0;
	attachRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//Depth
	VkAttachmentReference depthAttach = {};
	depthAttach.attachment = 1;
	depthAttach.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	
	VkSubpassDescription subpass[2] = {};
	subpass[0].colorAttachmentCount = 1;
	subpass[0].pColorAttachments = &attachRef;
	subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass[0].pDepthStencilAttachment = &depthAttach;

	subpass[1].colorAttachmentCount = 1;
	subpass[1].pColorAttachments = &attachRef;
	subpass[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass[1].pDepthStencilAttachment = &depthAttach;

	VkAttachmentDescription depthDesc = {};
	depthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	depthDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthDesc.format = VK_FORMAT_D32_SFLOAT_S8_UINT;


	VkSubpassDependency dependencies[3] = {};
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstAccessMask = 
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstSubpass = 0;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[1].srcAccessMask = 
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].dstSubpass = 0;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[2].srcAccessMask =
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[2].srcSubpass = 0;
	dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;

	VkAttachmentDescription attachments[] = { attachDesc, depthDesc };
	VkRenderPassCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = 2;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = subpass;
	info.dependencyCount = 3;
	info.pDependencies = dependencies;

	VkCheck(vkCreateRenderPass(vkDevice, &info, nullptr, &offscreenRenderPass.handle));

	VkCommandBufferAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc.commandBufferCount = 1;
	alloc.commandPool = vkCommandPool;
	alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	
	VkCheck(vkAllocateCommandBuffers(vkDevice, &alloc, &offscreenRenderPass.cmd));
}

static void Vk_CreateRenderPass()
{
	const bool msaa = (Vk_SampleCount() > VK_SAMPLE_COUNT_1_BIT);

	//Color
	VkAttachmentDescription attachDesc = {};
	attachDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachDesc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	attachDesc.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachDesc.samples = Vk_SampleCount();
	attachDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkAttachmentDescription resolveAttachDesc = {};
	resolveAttachDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	resolveAttachDesc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	resolveAttachDesc.format = VK_FORMAT_B8G8R8A8_UNORM;
	resolveAttachDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	resolveAttachDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	resolveAttachDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	resolveAttachDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	resolveAttachDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	//Color
	VkAttachmentReference attachRef = {};
	attachRef.attachment = 0;
	attachRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorResolveRef = {};
	colorResolveRef.attachment = 2;
	colorResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//Depth
	VkAttachmentReference depthAttach = {};
	depthAttach.attachment = 1;
	depthAttach.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	
	VkSubpassDescription subpass[2] = {};
	subpass[0].colorAttachmentCount = 1;
	subpass[0].pColorAttachments = &attachRef;
	subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass[0].pDepthStencilAttachment = &depthAttach;

	if(msaa)
		subpass[0].pResolveAttachments = &colorResolveRef;

	subpass[1].colorAttachmentCount = 1;
	subpass[1].pColorAttachments = &attachRef;
	subpass[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass[1].pDepthStencilAttachment = &depthAttach;

	VkAttachmentDescription depthDesc = {};
	depthDesc.samples = Vk_SampleCount();
	depthDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthDesc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	depthDesc.format = VK_FORMAT_D32_SFLOAT_S8_UINT;


	VkSubpassDependency dependencies[3] = {};
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstAccessMask = 
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstSubpass = 0;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[1].srcAccessMask = 
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkAttachmentDescription attachments[] = { attachDesc, depthDesc, resolveAttachDesc };
	VkRenderPassCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = msaa ? 3 : 2;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = subpass;
	info.dependencyCount = 2;
	info.pDependencies = dependencies;

	VkCheck(vkCreateRenderPass(vkDevice, &info, nullptr, &screenRenderPass.handle));
}


static void Vk_CreateSwapChain()
{
	Vk_PopulateSwapChainInfo();
	
	const bool msaa = (Vk_SampleCount() > VK_SAMPLE_COUNT_1_BIT);

	VkSurfaceCapabilitiesKHR caps = surfaceCaps;
	uint32_t imageCount;
	{
		VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		info.clipped = VK_TRUE;
		info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
		info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		info.surface = vkSurface;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.minImageCount = caps.minImageCount;
		info.imageExtent.width = caps.currentExtent.width;
		info.imageExtent.height = caps.currentExtent.height;
		info.preTransform = caps.currentTransform;
		info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;

		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkSwapchain != VK_NULL_HANDLE)
		{
			info.oldSwapchain = vkSwapchain;
		}

		//Keep existing swap chain around until after the new one is created
		VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
		VkCheck(vkCreateSwapchainKHR(vkDevice, &info, nullptr, &newSwapchain));

		if (vkSwapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(vkDevice, vkSwapchain, nullptr);
		}

		vkSwapchain = newSwapchain;

		VkCheck(vkGetSwapchainImagesKHR(vkDevice, vkSwapchain, &imageCount, nullptr));
	}

	//If framebuffers is already populated, we've been here before, so
	//destroy the old image views first in order to recreate
	for (size_t i = 0; i < framebuffers.size(); ++i)
	{
		vkDestroyImageView(vkDevice, framebuffers[i].view, nullptr);
		vkDestroyFramebuffer(vkDevice, framebuffers[i].framebuffer, nullptr);
	}

	//Retrieve the most up-to-date swapchain image handles

	std::vector<VkImage> images;
	images.resize(imageCount);
	framebuffers.resize(imageCount);
	VkCheck(vkGetSwapchainImagesKHR(vkDevice, vkSwapchain, &imageCount, images.data()));


	//Create the image views
	for (size_t i = 0; i < images.size(); ++i)
	{
		framebuffers[i].image = images[i];

		VkImageViewCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = framebuffers[i].image;
		info.format = VK_FORMAT_B8G8R8A8_UNORM;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;

		info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.baseMipLevel = 0;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		info.subresourceRange.layerCount = 1;

		VkCheck(vkCreateImageView(vkDevice, &info, nullptr, &(framebuffers[i].view)));
	}

	//Create the depth buffer
	Vk_CreateDepthBuffer(&screenRenderPass.depth);
	

	Vk_CreateResolveImage(&screenRenderPass.resolve,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_FORMAT_B8G8R8A8_UNORM,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	//Create framebuffers
	{
		VkExtent2D extent = surfaceCaps.currentExtent;


		VkFramebufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.width = extent.width;
		info.height = extent.height;
		info.attachmentCount = msaa ? 3 : 2;
		info.layers = 1;
		info.renderPass = screenRenderPass.handle;

		VkImageView attachments[3];
		attachments[1] = screenRenderPass.depth.view;

		if (msaa)
			attachments[0] = screenRenderPass.resolve.view;

		for (Framebuffer& fb : framebuffers)
		{
			if(msaa)
				attachments[2] = fb.view;
			else
				attachments[0] = fb.view;

			info.pAttachments = attachments;
			VkCheck(vkCreateFramebuffer(vkDevice, &info, nullptr, &(fb.framebuffer)));
		}
	}

	//Create semaphores
	{
		VkSemaphoreCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		if(imageAvailableSemaphore == VK_NULL_HANDLE)
			VkCheck(vkCreateSemaphore(vkDevice, &info, nullptr, &imageAvailableSemaphore));

		if(renderingFinishedSemaphore == VK_NULL_HANDLE)
			VkCheck(vkCreateSemaphore(vkDevice, &info, nullptr, &renderingFinishedSemaphore));

		if (backEndFinishedSemaphore == VK_NULL_HANDLE)
			VkCheck(vkCreateSemaphore(vkDevice, &info, nullptr, &backEndFinishedSemaphore));

		if (uniformSyncFinishedSemaphore == VK_NULL_HANDLE)
			VkCheck(vkCreateSemaphore(vkDevice, &info, nullptr, &uniformSyncFinishedSemaphore));
	}
}

static void Vk_RecordCommandBuffer(VkFramebuffer framebuffer, VkCommandBuffer cmd, VkRenderPass pass)
{	
	VkClearValue colorClear, depthClear;
	colorClear.color = { 0.0f, 0.0f, 0.0f, 1.0f };
	depthClear.depthStencil = { 1.0f, STENCIL_SHADOW_TEST_VALUE };
	VkClearValue clearValues[3] = {
		colorClear, depthClear, colorClear
	};

	VkExtent2D extent = surfaceCaps.currentExtent;

	VkRenderPassBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	info.clearValueCount = (Vk_SampleCount() > VK_SAMPLE_COUNT_1_BIT) ? 3 : 2;
	info.pClearValues = clearValues;
	info.renderPass = pass;
	info.renderArea.offset = { 0, 0 };
	info.renderArea.extent = extent;
	info.framebuffer = framebuffer;

	VkViewport viewport = { 0, 0, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
	VkRect2D scissor = { 0, 0, extent.width, extent.height };

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetDepthBias(cmd, backEnd.glState.polyOfsBias, 0.0f, backEnd.glState.polyOfsScale);
	vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
}

VkCommandBuffer Vk_StartFrame()
{
	const uint64_t uint64max = std::numeric_limits<uint64_t>::max();

	VkCheck(vkAcquireNextImageKHR(vkDevice, vkSwapchain, 
		uint64max, imageAvailableSemaphore,
		VK_NULL_HANDLE, &activeCommandBufferIdx));

	VkCommandBuffer cmd = commandBuffers[activeCommandBufferIdx];

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

	if (activeCommandBufferIdx != -1)
	{
		//Wait for the fence *here* because it's not until after we've retrieved
		//the idx that we know what fence we're supposed to be waiting for.
		vkWaitForFences(vkDevice, 1, &fences[activeCommandBufferIdx], 
			VK_TRUE, uint64max);
		vkResetFences(vkDevice, 1, &fences[activeCommandBufferIdx]);
	}

	VkCheck(vkBeginCommandBuffer(cmd, &beginInfo));
	VkCheck(vkBeginCommandBuffer(backEndCommandBuffers[activeCommandBufferIdx], &beginInfo));

	vkCmdResetQueryPool(Vk_ActiveCommandBuffer(), queryPool, 0, 2);

	return commandBuffers[activeCommandBufferIdx];
}

VkCommandBuffer Vk_StartOffscrenRenderPass()
{
	activeRenderPass = &offscreenRenderPass;

	//VkCommandBuffer cmd = offscreenRenderPass.cmd;
	VkCommandBuffer cmd = commandBuffers[activeCommandBufferIdx];

	Vk_RecordCommandBuffer(offscreenRenderPass.fb.framebuffer, 
		cmd, offscreenRenderPass.handle);

	return cmd;
}

VkCommandBuffer Vk_StartRenderPass()
{
	activeRenderPass = &screenRenderPass;

	Vk_RecordCommandBuffer(framebuffers[activeCommandBufferIdx].framebuffer,
		commandBuffers[activeCommandBufferIdx], screenRenderPass.handle);

	return commandBuffers[activeCommandBufferIdx];
}

VkCommandBuffer Vk_ActiveCommandBuffer()
{
	if (activeCommandBufferIdx == -1)
		return VK_NULL_HANDLE;

	return commandBuffers[activeCommandBufferIdx];
}

void Vk_EndFrame()
{
	VkCommandBuffer cmd = Vk_ActiveCommandBuffer();

	if (cmd == VK_NULL_HANDLE)
		return;

	if (activeRenderPass == &screenRenderPass)
	{
		VkImageSubresourceRange range = {};
		range.layerCount = 1;
		range.levelCount = 1;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.image = Vk_ActiveColorBuffer();
		barrier.subresourceRange = range;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = 0;

		vkCmdPipelineBarrier(commandBuffers[activeCommandBufferIdx],
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		if(Vk_SampleCount() > VK_SAMPLE_COUNT_1_BIT)
		{
			barrier.image = screenRenderPass.resolve.image;
			vkCmdPipelineBarrier(commandBuffers[activeCommandBufferIdx],
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.subresourceRange = range;
		barrier.image = screenRenderPass.depth.image;
		vkCmdPipelineBarrier(commandBuffers[activeCommandBufferIdx],
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	renderProgManager->EndFrame();
	VkCheck(vkEndCommandBuffer(cmd));

	VkCheck(vkEndCommandBuffer(backEndCommandBuffers[activeCommandBufferIdx]));

	for (VkDescriptorSet set : destructionQueue)
	{
		Vk_FreeDescriptorSet(set);
	}

	destructionQueue.clear();
}

void Vk_EndRenderPass()
{
	VkCommandBuffer cmd = Vk_ActiveCommandBuffer();

	if (cmd == VK_NULL_HANDLE)
		return;

	vkCmdEndRenderPass(cmd);

	if (activeRenderPass == &offscreenRenderPass)
	{
		VkImageCopy region = {};
		region.extent = { 
			(uint32_t)renderSystem->GetWidth(),
			(uint32_t)renderSystem->GetHeight(),
			1
		};
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		vkCmdCopyImage(cmd, offscreenRenderPass.color.image, 
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			framebuffers[activeCommandBufferIdx].image, 
			VK_IMAGE_LAYOUT_GENERAL, 1, &region);
	}
}

static void Vk_AllocateAllCommandBuffers()
{
	commandBuffers.resize(framebuffers.size());

	VkCommandBufferAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.commandBufferCount = (uint32_t)commandBuffers.size();
	info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	info.commandPool = vkCommandPool;

	VkCheck(vkAllocateCommandBuffers(vkDevice, &info, commandBuffers.data()));

	backEndCommandBuffers.resize(framebuffers.size());

	VkCheck(vkAllocateCommandBuffers(vkDevice, &info, backEndCommandBuffers.data()));

	uniformCommandBuffers.resize(framebuffers.size());

	VkCheck(vkAllocateCommandBuffers(vkDevice, &info, uniformCommandBuffers.data()));
}

static void Vk_CreatePipelineLayout()
{
	//Uniform layouts (set 0)

	VkDescriptorSetLayoutBinding bindings[3] = {};
	//set 0 binding 0 - vertex uniforms
	bindings[0].binding = 0;
	bindings[0].descriptorCount = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	//set 0 binding 1 - fragment uniforms
	bindings[1].binding = 1;
	bindings[1].descriptorCount = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	
	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;

	VkCheck(vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &uniformSetLayout));

	//set 1 binding 0 - joints
	bindings[0].binding = 0;
	bindings[0].descriptorCount = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	layoutInfo.bindingCount = 1;

	VkCheck(vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &jointSetLayout));

	//Image layouts
	
	//For images, rather than have one set contain all our bindings, use one
	//binding per set and multiple sets. This makes it easier to rebind multiple
	//images on the fly for the shaders that take multiple texture inputs
	//and avoids having to risk breaking the existing GL_SelectTexture behaviour
	//Considering we're never binding more than a small number of images, this
	//is guaranteed to stay under the set binding limit.

	//binding 0 - combined image sampler
	bindings[0].binding = 0;
	bindings[0].descriptorCount = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &bindings[0];
	VkCheck(vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &imageSetLayout));

	VkDescriptorPoolSize sizes[3] = {};
	sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	sizes[0].descriptorCount = 16;

	sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sizes[1].descriptorCount = MAX_IMAGE_DESC_SETS;

	sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	sizes[2].descriptorCount = 16;

	VkDescriptorPoolCreateInfo poolCreateInfo = {};
	poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolCreateInfo.pPoolSizes = sizes;
	poolCreateInfo.poolSizeCount = 3;
	poolCreateInfo.maxSets = MAX_IMAGE_DESC_SETS;
	poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	VkCheck(vkCreateDescriptorPool(vkDevice, &poolCreateInfo, nullptr, &descriptorPool));

	const uint32_t SET_COUNT = 7;
	VkDescriptorSet layouts[] = {
		//set 0 - uniforms
		uniformSetLayout,

		//set 1 - joint bindings
		jointSetLayout,

		//sets 2-6 - texture bindings
		imageSetLayout,
		imageSetLayout,
		imageSetLayout,
		imageSetLayout,
		imageSetLayout
	};

	VkPushConstantRange pushConstant = {};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(uint32_t);
	pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkPipelineLayoutCreateInfo layoutCreateInfo = {};
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCreateInfo.pSetLayouts = layouts;
	layoutCreateInfo.setLayoutCount = SET_COUNT;
	layoutCreateInfo.pushConstantRangeCount = 1;
	layoutCreateInfo.pPushConstantRanges = &pushConstant;

	VkCheck(vkCreatePipelineLayout(vkDevice, &layoutCreateInfo, nullptr, &vkPipelineLayout));
}

static void Vk_CreateUniformDescriptorSet()
{
	//Allocate the uniform set here
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &uniformSetLayout;

	VkCheck(vkAllocateDescriptorSets(vkDevice, &allocInfo, &vkDescriptorSet));
}

static void Vk_CreateJointDescriptorSets()
{
	VkDescriptorSetLayout layouts[] = {
		jointSetLayout, jointSetLayout
	};

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = VERTCACHE_NUM_FRAMES;
	allocInfo.pSetLayouts = &layouts[0];

	VkCheck(vkAllocateDescriptorSets(vkDevice, &allocInfo, &vkJointDescriptorSets[0]));
}

static void Vk_RegisterDebugger()
{
	VkDebugReportCallbackCreateInfoEXT info = {};
	info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	info.pfnCallback = VulkanUtil::debugCallback;
	info.pUserData = nullptr;
	info.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT
		| VK_DEBUG_REPORT_DEBUG_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
		VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;

	PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT = 
		(PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(vkInstance, "vkCreateDebugReportCallbackEXT");
	if (vkCreateDebugReportCallbackEXT)
		vkCreateDebugReportCallbackEXT(vkInstance, &info, nullptr, &vkDebugCallback);
}

static bool Vk_InitDriver(VkImpParams_t params)
{
	PIXELFORMATDESCRIPTOR src = 
	{
		sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
		1,								// version number
		PFD_DRAW_TO_WINDOW |			// support window
		PFD_SUPPORT_OPENGL |			// support OpenGL
		PFD_DOUBLEBUFFER,				// double buffered
		PFD_TYPE_RGBA,					// RGBA type
		32,								// 32-bit color depth
		0, 0, 0, 0, 0, 0,				// color bits ignored
		8,								// 8 bit destination alpha
		0,								// shift bit ignored
		0,								// no accumulation buffer
		0, 0, 0, 0, 					// accum bits ignored
		24,								// 24-bit z-buffer	
		8,								// 8-bit stencil buffer
		0,								// no auxiliary buffer
		PFD_MAIN_PLANE,					// main layer
		0,								// reserved
		0, 0, 0							// layer masks ignored
    };


	common->Printf("Initializing Vulkan driver\n");
	
	//
	// get a DC for our window if we don't already have one allocated
	//
	if ( win32.hDC == NULL ) {
		common->Printf( "...getting DC: " );

		if ( ( win32.hDC = GetDC( win32.hWnd ) ) == NULL ) {
			common->Printf( "^3failed^0\n" );
			return false;
		}
		common->Printf( "succeeded\n" );
	}

	// the multisample path uses the wgl 
	if ( wglChoosePixelFormatARB ) {
		win32.pixelformat = Vk_ChoosePixelFormat( win32.hDC, params.multiSamples, params.stereo );
	} else {
		// this is the "classic" choose pixel format path
		common->Printf( "Using classic ChoosePixelFormat\n" );

		// eventually we may need to have more fallbacks, but for
		// now, ask for everything
		if ( params.stereo ) {
			common->Printf( "...attempting to use stereo\n" );
			src.dwFlags |= PFD_STEREO;
		}

		//
		// choose, set, and describe our desired pixel format.  If we're
		// using a minidriver then we need to bypass the GDI functions,
		// otherwise use the GDI functions.
		//
		if ( ( win32.pixelformat = ChoosePixelFormat( win32.hDC, &src ) ) == 0 ) {
			common->Printf( "...^3GLW_ChoosePFD failed^0\n");
			return false;
		}
		common->Printf( "...PIXELFORMAT %d selected\n", win32.pixelformat );
	}

	// get the full info
	DescribePixelFormat( win32.hDC, win32.pixelformat, sizeof( win32.pfd ), &win32.pfd );
	glConfig.colorBits = win32.pfd.cColorBits;
	glConfig.depthBits = win32.pfd.cDepthBits;
	glConfig.stencilBits = win32.pfd.cStencilBits;

	// XP seems to set this incorrectly
	if ( !glConfig.stencilBits ) {
		glConfig.stencilBits = 8;
	}

	// the same SetPixelFormat is used either way
	if ( SetPixelFormat( win32.hDC, win32.pixelformat, &win32.pfd ) == FALSE ) {
		common->Printf( "...^3SetPixelFormat failed^0\n", win32.hDC );
		return false;
	}

	//
	// startup the Vulkan subsystem by creating a context and making it current
	//
	common->Printf( "...creating Vk context: " );
	CreateVulkanContextOnHWND( win32.hWnd, r_debugContext.GetBool() );
	common->Printf( "succeeded\n" );

	Vk_PickPhysicalDevice();
	Vk_InitDevice();
	Vk_InitCommandPool();
	Vk_CreatePipelineLayout();
	Vk_CreateUniformDescriptorSet();
	Vk_CreateJointDescriptorSets();
	//Offscreen pass not currently used.
	//Vk_CreateOffscreenRenderPass();
	Vk_CreateRenderPass();
	Vk_CreateSwapChain();
	Vk_AllocateAllCommandBuffers();

	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	fences.resize(framebuffers.size());

	for(size_t i = 0; i < fences.size(); ++i)
		vkCreateFence(vkDevice, &fenceCreateInfo, nullptr, &fences[i]);

	Vk_RegisterDebugger();

	Vk_CreateQueryPool();

	return true;
}

/*
=======================
Vk_CreateWindow

Responsible for creating the Win32 window.
If fullscreen, it won't have a border
=======================
*/
static bool Vk_CreateWindow( VkImpParams_t params ) {
	int				x, y, w, h;
	if ( !GFX_GetWindowDimensions( params, x, y, w, h ) ) {
		return false;
	}

	int				stylebits;
	int				exstyle;
	if ( params.fullScreen != 0 ) {
		exstyle = WS_EX_TOPMOST;
		stylebits = WS_POPUP|WS_VISIBLE|WS_SYSMENU;
	} else {
		exstyle = 0;
		stylebits = WINDOW_STYLE|WS_SYSMENU;
	}

	win32.hWnd = CreateWindowEx (
		 exstyle, 
		 WIN32_WINDOW_CLASS_NAME,
		 GAME_NAME,
		 stylebits,
		 x, y, w, h,
		 NULL,
		 NULL,
		 win32.hInstance,
		 NULL);

	if ( !win32.hWnd ) {
		common->Printf( "^3Vk_CreateWindow() - Couldn't create window^0\n" );
		return false;
	}

	::SetTimer( win32.hWnd, 0, 100, NULL );

	ShowWindow( win32.hWnd, SW_SHOW );
	UpdateWindow( win32.hWnd );
	common->Printf( "...created window @ %d,%d (%dx%d)\n", x, y, w, h );

	// makeCurrent NULL frees the DC, so get another
	win32.hDC = GetDC( win32.hWnd );
	if ( !win32.hDC ) {
		common->Printf( "^3Vk_CreateWindow() - GetDC()failed^0\n" );
		return false;
	}

	// Check to see if we can get a stereo pixel format, even if we aren't going to use it,
	// so the menu option can be 
	if ( Vk_ChoosePixelFormat( win32.hDC, params.multiSamples, true ) != -1 ) {
		glConfig.stereoPixelFormatAvailable = true;
	} else {
		glConfig.stereoPixelFormatAvailable = false;
	}

	if ( !Vk_InitDriver( params ) ) {
		ShowWindow( win32.hWnd, SW_HIDE );
		DestroyWindow( win32.hWnd );
		win32.hWnd = NULL;
		return false;
	}

	SetForegroundWindow( win32.hWnd );
	SetFocus( win32.hWnd );

	glConfig.isFullscreen = params.fullScreen;

	return true;
}


bool VkImp_Init(VkImpParams_t params)
{
	const char	*driverName;
	HDC		hDC;

	//cmdSystem->AddCommand( "testSwapBuffers", GLimp_TestSwapBuffers, CMD_FL_SYSTEM, "Times swapbuffer options" );

	common->Printf( "Initializing Vulkan subsystem with multisamples:%i stereo:%i fullscreen:%i\n", 
		params.multiSamples, params.stereo, params.fullScreen );

	// check our desktop attributes
	hDC = GetDC( GetDesktopWindow() );
	win32.desktopBitsPixel = GetDeviceCaps( hDC, BITSPIXEL );
	win32.desktopWidth = GetDeviceCaps( hDC, HORZRES );
	win32.desktopHeight = GetDeviceCaps( hDC, VERTRES );
	ReleaseDC( GetDesktopWindow(), hDC );

	// we can't run in a window unless it is 32 bpp
	if ( win32.desktopBitsPixel < 32 && params.fullScreen <= 0 ) {
		common->Printf("^3Windowed mode requires 32 bit desktop depth^0\n");
		return false;
	}

	// save the hardware gamma so it can be
	// restored on exit
	//GLimp_SaveGamma();

	// create our window classes if we haven't already
	GFX_CreateWindowClasses();

	// try to create a window with the correct pixel format
	// and init the renderer context
	if ( !Vk_CreateWindow( params ) ) {
		VkImp_Shutdown();
		return false;
	}

	glConfig.isFullscreen = params.fullScreen;
	glConfig.isStereoPixelFormat = params.stereo;
	glConfig.nativeScreenWidth = params.width;
	glConfig.nativeScreenHeight = params.height;
	glConfig.multisamples = params.multiSamples;

	glConfig.pixelAspect = 1.0f;	// FIXME: some monitor modes may be distorted
									// should side-by-side stereo modes be consider aspect 0.5?

	// get the screen size, which may not be reliable...
	// If we use the windowDC, I get my 30" monitor, even though the window is
	// on a 27" monitor, so get a dedicated DC for the full screen device name.
	const idStr deviceName = GetDeviceName( Max( 0, params.fullScreen - 1 ) );

	HDC deviceDC = CreateDC( deviceName.c_str(), deviceName.c_str(), NULL, NULL );
	const int mmWide = GetDeviceCaps( win32.hDC, HORZSIZE );
	DeleteDC( deviceDC );

	if ( mmWide == 0 ) {
		glConfig.physicalScreenWidthInCentimeters = 100.0f;
	} else {
		glConfig.physicalScreenWidthInCentimeters = 0.1f * mmWide;
	}

	return true;
}

static void Vk_FreeRenderImage(RenderImage* i)
{

}

void VkImp_Shutdown()
{
	vkDeviceWaitIdle(vkDevice);

	for (size_t i = 0; i < framebuffers.size(); ++i)
	{
		vkDestroyImageView(vkDevice, framebuffers[i].view, nullptr);
		vkDestroyFramebuffer(vkDevice, framebuffers[i].framebuffer, nullptr);
	}
	framebuffers.clear();

	//destroy in reverse order:
	vkFreeCommandBuffers(vkDevice, vkCommandPool, commandBuffers.size(), commandBuffers.data());
	commandBuffers.clear();

	vkFreeCommandBuffers(vkDevice, vkCommandPool, backEndCommandBuffers.size(), backEndCommandBuffers.data());
	backEndCommandBuffers.clear();

	vkFreeCommandBuffers(vkDevice, vkCommandPool, uniformCommandBuffers.size(), uniformCommandBuffers.data());
	uniformCommandBuffers.clear();

	for (size_t i = 0; i < fences.size(); ++i)
	{
		vkDestroyFence(vkDevice, fences[i], nullptr);
	}
	fences.clear();

	//Offscreen pass not currently used.
	//Vk_FreeRenderImage(offscreenRenderPass.color);
	//Vk_FreeRenderImage(offscreenRenderPass.depth);
	//vkDestroyRenderPass(vkDevice, offscreenRenderPass.handle, nullptr);

	Vk_FreeRenderImage(&screenRenderPass.color);
	Vk_FreeRenderImage(&screenRenderPass.depth);
	vkDestroyRenderPass(vkDevice, screenRenderPass.handle, nullptr);

	vkDestroyQueryPool(vkDevice, queryPool, nullptr);
	vkFreeDescriptorSets(vkDevice, descriptorPool, VERTCACHE_NUM_FRAMES, vkJointDescriptorSets);
	vkFreeDescriptorSets(vkDevice, descriptorPool, 1, &vkDescriptorSet);
	vkDestroyDescriptorSetLayout(vkDevice, uniformSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(vkDevice, imageSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(vkDevice, jointSetLayout, nullptr);
	vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
	vkDestroySemaphore(vkDevice, imageAvailableSemaphore, nullptr);
	vkDestroySemaphore(vkDevice, renderingFinishedSemaphore, nullptr);
	vkDestroySemaphore(vkDevice, backEndFinishedSemaphore, nullptr);
	vkDestroySemaphore(vkDevice, uniformSyncFinishedSemaphore, nullptr);
	vkDestroySwapchainKHR(vkDevice, vkSwapchain, nullptr);
	vkDestroyPipelineLayout(vkDevice, vkPipelineLayout, nullptr);
	vkDestroyCommandPool(vkDevice, vkCommandPool, nullptr);
	vkDestroyDevice(vkDevice, nullptr);
	vkDestroySurfaceKHR(vkInstance, vkSurface, nullptr);
	
	PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT =
		(PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(vkInstance, "vkDestroyDebugReportCallbackEXT");
	if (vkDestroyDebugReportCallbackEXT)
		vkDestroyDebugReportCallbackEXT(vkInstance, vkDebugCallback, nullptr);
	
	vkDestroyInstance(vkInstance, nullptr);
}

void Vk_FlipPresent()
{
	if (activeCommandBufferIdx == -1)
		return;

	//Flipping works by submitting three command queues:
	//* First, the uniforms used for this frame have to finish syncing
	//* After that, rendering for this frame is allowed to begin
	//* During all this, vertex/index/join data for next frame is synced

	VkSubmitInfo uniformInfo = {};
	uniformInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	uniformInfo.signalSemaphoreCount = 1;
	uniformInfo.pSignalSemaphores = &uniformSyncFinishedSemaphore;
	uniformInfo.commandBufferCount = 1;
	uniformInfo.pCommandBuffers = &uniformCommandBuffers[activeCommandBufferIdx];

	VkSemaphore backendSignals[] = { backEndFinishedSemaphore };
	VkCommandBuffer backendBuffers[] = { backEndCommandBuffers[activeCommandBufferIdx] };
	VkSubmitInfo backendSubmitInfo = {};
	backendSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	backendSubmitInfo.signalSemaphoreCount = 1;
	backendSubmitInfo.pSignalSemaphores = backendSignals;
	backendSubmitInfo.commandBufferCount = 1;
	backendSubmitInfo.pCommandBuffers = backendBuffers;

	VkSemaphore semaphores[] = {
		backEndFinishedSemaphore,
		uniformSyncFinishedSemaphore,
		imageAvailableSemaphore
	};
	VkSemaphore signals[] = { renderingFinishedSemaphore };
	VkPipelineStageFlags stages[] = {
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	VkCommandBuffer buffers[] = { commandBuffers[(size_t)activeCommandBufferIdx] };

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 3;
	submitInfo.pWaitSemaphores = &semaphores[0];
	submitInfo.pWaitDstStageMask = &stages[0];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signals;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = buffers;

	VkSubmitInfo submits[3] = { uniformInfo, backendSubmitInfo, submitInfo };
	VkCheck(vkQueueSubmit(vkGraphicsQueue.vkQueue, 3, submits, fences[activeCommandBufferIdx]));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signals;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &vkSwapchain;
	presentInfo.pImageIndices = &activeCommandBufferIdx;

	VkResult res = vkQueuePresentKHR(vkPresentQueue.vkQueue, &presentInfo);
	if (res == VK_ERROR_DEVICE_LOST || res == VK_ERROR_OUT_OF_DATE_KHR)
	{
		//Recreate device
		vkDeviceWaitIdle(vkDevice);
		Vk_CreateSwapChain();
	}
	else
	{
		VkCheck(res);
	}

	static int msaaCount = r_multiSamples.GetInteger();
	if (msaaCount != r_multiSamples.GetInteger())
	{
		vkDeviceWaitIdle(vkDevice);

		msaaCount = r_multiSamples.GetInteger();
		if(renderProgManager)
			((idRenderProgManagerVk*)(renderProgManager))->DestroyPipelines();

		vkDestroyRenderPass(vkDevice, screenRenderPass.handle, nullptr);
		Vk_CreateRenderPass();
		Vk_CreateSwapChain();
	}
}

VkBuffer Vk_CreateAndBindBuffer(const VkBufferCreateInfo& info, VkMemoryPropertyFlags flags, VkDeviceMemory& memory)
{
	VkBuffer buffer;

	VkMemoryRequirements memReq;

	VkCheck(vkCreateBuffer(vkDevice, &info, nullptr, &buffer));
	vkGetBufferMemoryRequirements(vkDevice, buffer, &memReq);

	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = memReq.size;
	alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits, flags);

	VkCheck(vkAllocateMemory(vkDevice, &alloc, nullptr, &memory));
	VkCheck(vkBindBufferMemory(vkDevice, buffer, memory, 0));

	return buffer;
}

VkImage Vk_AllocAndCreateImage(const VkImageCreateInfo& info, VkDeviceMemory& memory)
{
	return imageMemPool.AllocImage(info);
}

VkImageView Vk_CreateImageView(const VkImageViewCreateInfo& info)
{
	VkImageView view;

	VkCheck(vkCreateImageView(vkDevice, &info, nullptr, &view));

	return view;
}

VkCommandBuffer Vk_StartOneShotCommandBuffer()
{
	VkCommandBuffer buffer;
	
	VkCommandBufferAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.commandPool = vkCommandPool;
	info.commandBufferCount = 1;
	info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	vkAllocateCommandBuffers(vkDevice, &info, &buffer);

	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkCheck(vkBeginCommandBuffer(buffer, &begin));

	return buffer;
}

void Vk_SubmitOneShotCommandBuffer(VkCommandBuffer cmd)
{
	VkCheck(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	VkResult res;
	VkCheck(res = vkQueueSubmit(vkGraphicsQueue.vkQueue, 1, &submit, VK_NULL_HANDLE));
	VkCheck(res = vkQueueWaitIdle(vkGraphicsQueue.vkQueue));

	vkFreeCommandBuffers(vkDevice, vkCommandPool, 1, &cmd);
}

void Vk_SetImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, 
	VkImageLayout newLayout, VkImageSubresourceRange& range)
{

	VkCommandBuffer buffer = Vk_StartOneShotCommandBuffer();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.image = image;
	barrier.subresourceRange = range;

	VkPipelineStageFlags srcMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dstMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	switch (oldLayout)
	{
	case VK_IMAGE_LAYOUT_PREINITIALIZED:
		barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	}

	switch (newLayout)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dstMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dstMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		break;
	}

	vkCmdPipelineBarrier(buffer, srcMask, dstMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);	

	Vk_SubmitOneShotCommandBuffer(buffer);
}

void Vk_DestroyImageAndView(VkImage image, VkImageView imageView)
{
	vkDestroyImageView(vkDevice, imageView, nullptr);
	imageMemPool.FreeImage(image);
}

void Vk_DestroyBuffer(VkBuffer buffer)
{
	vkDestroyBuffer(vkDevice, buffer, nullptr);
}

void Vk_FreeMemory(VkDeviceMemory memory)
{

}

VkPipeline Vk_CreatePipeline(VkGraphicsPipelineCreateInfo& info)
{
	info.renderPass = activeRenderPass->handle;

	VkPipeline pipeline;
	VkCheck(vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));

	return pipeline;
}

VkShaderModule Vk_CreateShaderModule(const char* bytes, size_t length)
{
	VkShaderModule m;

	VkShaderModuleCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = length;
	info.pCode = (uint32_t*)bytes;

	VkCheck(vkCreateShaderModule(vkDevice, &info, nullptr, &m));

	return m;
}

void Vk_UsePipeline(VkPipeline p)
{
	if (p == VK_NULL_HANDLE) return;
	vkCmdBindPipeline(commandBuffers[activeCommandBufferIdx], VK_PIPELINE_BIND_POINT_GRAPHICS, p);
}

void* Vk_MapMemory(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags)
{
	void* ptr;
	vkMapMemory(vkDevice, memory, offset, size, flags, &ptr);
	return ptr;
}

void Vk_UnmapMemory(VkDeviceMemory m)
{
	vkUnmapMemory(vkDevice, m);
}

void Vk_CreateUniformBuffer(VkDeviceMemory& stagingMemory, VkBuffer& stagingBuffer,
	VkDeviceMemory& memory, VkBuffer& buffer, VkDeviceSize size)
{
	VkBufferCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.size = size;

	stagingBuffer = Vk_CreateAndBindBuffer(info, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingMemory);

	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	buffer = Vk_CreateAndBindBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory);
}

void Vk_UpdateDescriptorSet(VkWriteDescriptorSet& write)
{
	write.dstSet = vkDescriptorSet;
	vkUpdateDescriptorSets(vkDevice, 1, &write, 0, 0);
}

VkDevice Vk_GetDevice()
{
	return vkDevice;
}

VkDescriptorSet Vk_AllocDescriptorSetForImage()
{
	VkDescriptorSet set;

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &imageSetLayout;

	VkCheck(vkAllocateDescriptorSets(vkDevice, &allocInfo, &set));

	return set;
}

VkPipelineLayout Vk_GetPipelineLayout()
{
	return vkPipelineLayout;
}

VkDescriptorSet Vk_UniformDescriptorSet()
{
	return vkDescriptorSet;
}

VkDescriptorSet Vk_AllocateJointBufferSetForFrame(int idx, geoBufferSet_t& gbs)
{
	idJointBufferVk* jb = (idJointBufferVk*)gbs.jointBuffer;
	VkDescriptorBufferInfo buf = {};
	buf.buffer = jb->GetBuffer();
	buf.range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	write.dstBinding = 0;
	write.dstSet = vkJointDescriptorSets[idx];
	write.pBufferInfo = &buf;

	vkUpdateDescriptorSets(Vk_GetDevice(), 1, &write, 0, 0);
	
	return vkJointDescriptorSets[idx];
}

VkDescriptorSet Vk_JointBufferSetForFrame(int idx)
{
	assert(idx < VERTCACHE_NUM_FRAMES);
	return vkJointDescriptorSets[idx];
}

void Vk_FreeDescriptorSet(const VkDescriptorSet set)
{
	vkFreeDescriptorSets(vkDevice, descriptorPool, 1, &set);
}

void Vk_ClearAttachments(uint32_t mask, byte stencilValue)
{
	if (mask == 0)
		return;

	std::vector<VkClearAttachment> attachments;

	if (mask & VK_IMAGE_ASPECT_DEPTH_BIT || mask & VK_IMAGE_ASPECT_STENCIL_BIT)
	{
		VkClearAttachment attachment = {};
		if (mask & VK_IMAGE_ASPECT_DEPTH_BIT)
		{
			attachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			attachment.clearValue.depthStencil.depth = 1.0f;
		}

		if (mask & VK_IMAGE_ASPECT_STENCIL_BIT)
		{
			attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			attachment.clearValue.depthStencil.stencil = stencilValue;
		}

		attachments.push_back(attachment);
	}

	if (mask & VK_IMAGE_ASPECT_COLOR_BIT)
	{
		VkClearAttachment attachment = {};
		attachment.aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
		attachment.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };

		attachments.push_back(attachment);
	}

	VkClearRect rect = {};
	rect.layerCount = 1;
	rect.rect = { 
		0, 0,
		(uint32_t)renderSystem->GetWidth(), (uint32_t)renderSystem->GetHeight()
	};

	vkCmdClearAttachments(Vk_ActiveCommandBuffer(), attachments.size(),
		attachments.data(), 1, &rect);
}

VkImage Vk_ActiveColorBuffer()
{
	return framebuffers[activeCommandBufferIdx].image;
}

VkImage Vk_ActiveDepthBuffer()
{
	return screenRenderPass.depth.image;
}

void Vk_QueueDestroyDescriptorSet(VkDescriptorSet set)
{
	destructionQueue.push_back(set);
}

VkSampleCountFlagBits Vk_MaxSupportedSampleCount()
{
	VkSampleCountFlags colorLimit =
		vkPhysicalDeviceProperties.limits.framebufferColorSampleCounts;
	VkSampleCountFlags depthLimit =
		vkPhysicalDeviceProperties.limits.framebufferDepthSampleCounts;

	VkSampleCountFlags limit = Min(colorLimit, depthLimit);

	if (limit & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
	if (limit & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
	if (limit & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
	if (limit & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
	if (limit & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
	if (limit & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;

	return VK_SAMPLE_COUNT_1_BIT;
}

VkSampleCountFlagBits Vk_SampleCount()
{
	VkSampleCountFlagBits maxSamples = Vk_MaxSupportedSampleCount();
	const int msaa = r_multiSamples.GetInteger();

	//Fall through on each case until we get a supported sample count.
	switch (msaa)
	{
	case 64:
		if (maxSamples >= VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
	case 32:
		if (maxSamples >= VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
	case 16:
		if (maxSamples >= VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
	case 8:
		if (maxSamples >= VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
	case 4:
		if (maxSamples >= VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
	case 2:
		if (maxSamples >= VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
	}

	return VK_SAMPLE_COUNT_1_BIT;
}

void Vk_StartFrameTimeCounter()
{
	vkCmdWriteTimestamp(Vk_ActiveCommandBuffer(),
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);
}

void Vk_EndFrameTimeCounter()
{
	vkCmdWriteTimestamp(Vk_ActiveCommandBuffer(),
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);
}

uint64_t Vk_GetFrameTimeCounter()
{
	uint64_t results[2] = { 0, 0 };

	VkResult res = vkGetQueryPoolResults(vkDevice, queryPool, 0, 2, 
		sizeof(results), results, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

	if (res != VK_SUCCESS)
		return 0;

	uint64_t time = (results[1] - results[0]);

	//internal units (whatever the timestamp period is) to ms
	time /= 1000 / vkPhysicalDeviceProperties.limits.timestampPeriod;

	return time;
}

VkCommandBuffer Vk_CurrentBackendCommandBuffer()
{
	if (activeCommandBufferIdx == -1)
		return VK_NULL_HANDLE;

	return backEndCommandBuffers[activeCommandBufferIdx];
}

VkCommandBuffer Vk_CurrentUniformCommandBuffer()
{
	if (activeCommandBufferIdx == -1)
		return VK_NULL_HANDLE;

	return uniformCommandBuffers[activeCommandBufferIdx];
}

#endif