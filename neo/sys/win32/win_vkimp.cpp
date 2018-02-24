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

VkInstance vkInstance = VK_NULL_HANDLE;
VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
VkPhysicalDevice vkPhysicalDevice = VK_NULL_HANDLE;
VkPhysicalDeviceProperties vkPhysicalDeviceProperties;
VkPhysicalDeviceFeatures vkPhysicalDeviceFeatures;
VkDevice vkDevice = VK_NULL_HANDLE;	
VkCommandPool vkCommandPool = VK_NULL_HANDLE;
VkSwapchainKHR vkSwapchain = VK_NULL_HANDLE;
VkRenderPass vkRenderPass = VK_NULL_HANDLE;
VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
VkDescriptorSet vkDescriptorSet = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet vkJointDescriptorSets[VERTCACHE_NUM_FRAMES];

VkImage depthImage = VK_NULL_HANDLE;
VkImageView depthView = VK_NULL_HANDLE;
VkDeviceMemory depthMemory = VK_NULL_HANDLE;

VkDebugReportCallbackEXT vkDebugCallback;

struct QueueInfo
{
	VkQueue vkQueue;
	uint32_t index;
};

QueueInfo vkGraphicsQueue;
QueueInfo vkPresentQueue;

struct Framebuffer
{
	VkImage image;
	VkImageView view;
	VkFramebuffer framebuffer;
};

std::vector<Framebuffer> framebuffers;
std::vector<VkCommandBuffer> commandBuffers;

VkSurfaceCapabilitiesKHR surfaceCaps;

VkSemaphore imageAvailableSemaphore;
VkSemaphore renderingFinishedSemaphore;

uint32_t activeCommandBufferIdx = -1;

VkDescriptorSetLayout uniformSetLayout;
VkDescriptorSetLayout imageSetLayout;
VkDescriptorSetLayout jointSetLayout;

const uint32_t MAX_IMAGE_DESC_SETS = 8192;

static void CreateVulkanContextOnHWND(HWND hwnd, bool isDebug)
{
	VkWin32SurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hinstance = GetModuleHandle(NULL);
	createInfo.hwnd = hwnd;
	
	VkCheck(vkCreateWin32SurfaceKHR(vkInstance, &createInfo, nullptr, &vkSurface));
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
	
	VkCheck(vkCreateInstance(&createInfo, nullptr, &vkInstance));
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

static void Vk_CreateRenderPass()
{
	//Color
	VkAttachmentDescription attachDesc = {};
	attachDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachDesc.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	attachDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkAttachmentReference attachRef = {};
	attachRef.attachment = 0;
	attachRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &attachRef;
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;


	//Depth
	VkAttachmentReference depthAttach = {};
	depthAttach.attachment = 1;
	depthAttach.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	
	VkSubpassDescription depthPass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.pDepthStencilAttachment = &depthAttach;

	VkAttachmentDescription depthDesc = {};
	depthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	depthDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	//depthDesc.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	depthDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthDesc.format = VK_FORMAT_D32_SFLOAT_S8_UINT;


	VkSubpassDependency dependency = {};
	dependency.srcAccessMask = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;

	dependency.dstAccessMask = 
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstSubpass = 0;

	VkAttachmentDescription attachments[] = { attachDesc, depthDesc };
	VkRenderPassCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = 2;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 1;
	info.pDependencies = &dependency;

	VkCheck(vkCreateRenderPass(vkDevice, &info, nullptr, &vkRenderPass));
}

static void Vk_CreateSwapChain()
{
	Vk_PopulateSwapChainInfo();

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
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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

	//Create the image views

	std::vector<VkImage> images;
	images.resize(imageCount);
	framebuffers.resize(imageCount);
	VkCheck(vkGetSwapchainImagesKHR(vkDevice, vkSwapchain, &imageCount, images.data()));

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
	{
		VkImageCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.extent.width = surfaceCaps.currentExtent.width;
		info.extent.height = surfaceCaps.currentExtent.height;
		info.extent.depth = 1;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
		info.imageType = VK_IMAGE_TYPE_2D;

		VkCheck(vkCreateImage(vkDevice, &info, nullptr, &depthImage));

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(vkDevice, depthImage, &memReq);

		VkMemoryAllocateInfo alloc = {};
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.allocationSize = memReq.size;
		alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VkCheck(vkAllocateMemory(vkDevice, &alloc, nullptr, &depthMemory));
		VkCheck(vkBindImageMemory(vkDevice, depthImage, depthMemory, 0));

		VkImageViewCreateInfo view = {};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.image = depthImage;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;
		view.subresourceRange.levelCount = 1;

		VkCheck(vkCreateImageView(vkDevice, &view, nullptr, &depthView));

		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;
		range.layerCount = 1;
		range.levelCount = 1;
		Vk_SetImageLayout(depthImage,
			VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, range);
	}

	//Create framebuffers
	{
		VkExtent2D extent = surfaceCaps.currentExtent;

		for (Framebuffer& fb : framebuffers)
		{
			const VkImageView attachments[] = {
				fb.view, depthView
			};

			VkFramebufferCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			info.width = extent.width;
			info.height = extent.height;
			info.renderPass = vkRenderPass;
			info.attachmentCount = 2;
			info.pAttachments = attachments;
			info.layers = 1;

			VkCheck(vkCreateFramebuffer(vkDevice, &info, nullptr, &(fb.framebuffer)));
		}
	}

	//Create semaphores
	{
		VkSemaphoreCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkCheck(vkCreateSemaphore(vkDevice, &info, nullptr, &imageAvailableSemaphore));
		VkCheck(vkCreateSemaphore(vkDevice, &info, nullptr, &renderingFinishedSemaphore));
	}
}

static void Vk_RecordCommandBuffer(VkFramebuffer framebuffer, VkCommandBuffer cmd)
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	VkCheck(vkBeginCommandBuffer(cmd, &beginInfo));
	
	VkClearValue colorClear, depthClear;
	colorClear.color = { 0.0f, 0.0f, 0.5f, 1.0f };
	depthClear.depthStencil = { 1.0f, STENCIL_SHADOW_TEST_VALUE };
	VkClearValue clearValues[] = {
		colorClear, depthClear
	};

	VkExtent2D extent = surfaceCaps.currentExtent;

	VkRenderPassBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	info.clearValueCount = 2;
	info.pClearValues = clearValues;
	info.renderPass = vkRenderPass;
	info.renderArea.offset = { 0, 0 };
	info.renderArea.extent = extent;
	info.framebuffer = framebuffer;

	VkViewport viewport = { 0, 0, (float)extent.width, (float)extent.height, -1.0f, 1.0f };
	VkRect2D scissor = { 0, 0, extent.width, extent.height };

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetDepthBias(cmd, backEnd.glState.polyOfsBias, 0.0f, backEnd.glState.polyOfsScale);

	//VkImageSubresourceRange range = {};
	//range.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;
	//range.layerCount = 1;
	//range.levelCount = 1;
	//Vk_SetImageLayout(depthImage,
	//	VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_LAYOUT_UNDEFINED,
	//	VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);
	//Vk_ClearDepthStencilImage(STENCIL_SHADOW_TEST_VALUE);

	vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
}

VkCommandBuffer Vk_StartRenderPass()
{
	VkCheck(vkAcquireNextImageKHR(vkDevice, vkSwapchain, 
		std::numeric_limits<uint64_t>::max(), imageAvailableSemaphore,
		VK_NULL_HANDLE, &activeCommandBufferIdx));

	Vk_RecordCommandBuffer(framebuffers[activeCommandBufferIdx].framebuffer,
		commandBuffers[activeCommandBufferIdx]);

	return commandBuffers[activeCommandBufferIdx];
}

VkCommandBuffer Vk_ActiveCommandBuffer()
{
	if (activeCommandBufferIdx == -1)
		return VK_NULL_HANDLE;

	return commandBuffers[activeCommandBufferIdx];
}

void Vk_EndRenderPass()
{
	VkCommandBuffer cmd = Vk_ActiveCommandBuffer();

	if (cmd == VK_NULL_HANDLE)
		return;

	vkCmdEndRenderPass(cmd);

	renderProgManager->EndFrame();

	VkCheck(vkEndCommandBuffer(cmd));
}

static void Vk_RecordAllCommandBuffers()
{
	vkDeviceWaitIdle(vkDevice);

	commandBuffers.resize(framebuffers.size());

	VkCommandBufferAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.commandBufferCount = (uint32_t)commandBuffers.size();
	info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	info.commandPool = vkCommandPool;

	VkCheck(vkAllocateCommandBuffers(vkDevice, &info, commandBuffers.data()));
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

	VkPipelineLayoutCreateInfo layoutCreateInfo = {};
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCreateInfo.pSetLayouts = layouts;
	layoutCreateInfo.setLayoutCount = SET_COUNT;

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
	info.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT;

	PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT = 
		(PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(vkInstance, "vkCreateDebugReportCallbackEXT");
	if (vkCreateDebugReportCallbackEXT)
		vkCreateDebugReportCallbackEXT(vkInstance, &info, nullptr, &vkDebugCallback);
}

static bool Vk_InitDriver(VkImpParams_t params)
{
	Vk_CreateInstance();

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
	Vk_CreateRenderPass();
	Vk_CreateSwapChain();
	Vk_RecordAllCommandBuffers();

	Vk_RegisterDebugger();

	//common->Printf( "...making context current: " );
	//if ( !qwglMakeCurrent( win32.hDC, win32.hGLRC ) ) {
	//	qwglDeleteContext( win32.hGLRC );
	//	win32.hGLRC = NULL;
	//	common->Printf( "^3failed^0\n" );
	//	return false;
	//}
	//common->Printf( "succeeded\n" );

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

	// this will load the dll and set all our qgl* function pointers,
	// but doesn't create a window

	// r_glDriver is only intended for using instrumented OpenGL
	// dlls.  Normal users should never have to use it, and it is
	// not archived.
	//driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "opengl32";
	//if ( !QGL_Init( driverName ) ) {
		//common->Printf( "^3GLimp_Init() could not load r_glDriver \"%s\"^0\n", driverName );
		//return false;
	//}

	// getting the wgl extensions involves creating a fake window to get a context,
	// which is pretty disgusting, and seems to mess with the AGP VAR allocation
	//GLW_GetWGLExtensionsWithFakeWindow();



	// Optionally ChangeDisplaySettings to get a different fullscreen resolution.
	//if ( !GLW_ChangeDislaySettingsIfNeeded( params ) ) {
		//GLimp_Shutdown();
		//return false;
	//}

	// try to create a window with the correct pixel format
	// and init the renderer context
	if ( !Vk_CreateWindow( params ) ) {
		//GLimp_Shutdown();
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


	// wglSwapinterval, etc
	//GLW_CheckWGLExtensions( win32.hDC );

	// check logging
	//GLimp_EnableLogging( ( r_logFile.GetInteger() != 0 ) );

	return true;
}

void VkImp_Shutdown()
{
	vkDeviceWaitIdle(vkDevice);

	//destroy in reverse order:
	vkFreeCommandBuffers(vkDevice, vkCommandPool, commandBuffers.size(), commandBuffers.data());
	commandBuffers.clear();

	for (size_t i = 0; i < framebuffers.size(); ++i)
	{
		vkDestroyImageView(vkDevice, framebuffers[i].view, nullptr);
		vkDestroyImage(vkDevice, framebuffers[i].image, nullptr);
		vkDestroyFramebuffer(vkDevice, framebuffers[i].framebuffer, nullptr);
	}
	framebuffers.clear();

	vkFreeDescriptorSets(vkDevice, descriptorPool, VERTCACHE_NUM_FRAMES, vkJointDescriptorSets);
	vkFreeDescriptorSets(vkDevice, descriptorPool, 1, &vkDescriptorSet);
	vkDestroyDescriptorSetLayout(vkDevice, uniformSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(vkDevice, imageSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(vkDevice, jointSetLayout, nullptr);
	vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
	vkDestroySemaphore(vkDevice, imageAvailableSemaphore, nullptr);
	vkDestroySemaphore(vkDevice, renderingFinishedSemaphore, nullptr);
	vkDestroyImageView(vkDevice, depthView, nullptr);
	vkDestroyImage(vkDevice, depthImage, nullptr);
	vkFreeMemory(vkDevice, depthMemory, nullptr);
	vkDestroyRenderPass(vkDevice, vkRenderPass, nullptr);
	vkDestroyPipelineLayout(vkDevice, vkPipelineLayout, nullptr);
	vkDestroyCommandPool(vkDevice, vkCommandPool, nullptr);
	vkDestroySwapchainKHR(vkDevice, vkSwapchain, nullptr);
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

	VkSemaphore semaphores[] = { imageAvailableSemaphore };
	VkSemaphore signals[] = { renderingFinishedSemaphore };
	VkPipelineStageFlags stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkCommandBuffer buffers[] = { commandBuffers[(size_t)activeCommandBufferIdx] };
	VkSwapchainKHR swapChains[] = { vkSwapchain };

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = semaphores;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signals;
	submitInfo.pWaitDstStageMask = stages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = buffers;

	VkCheck(vkQueueSubmit(vkGraphicsQueue.vkQueue, 1, &submitInfo, VK_NULL_HANDLE));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signals;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &activeCommandBufferIdx;

	VkCheck(vkQueuePresentKHR(vkPresentQueue.vkQueue, &presentInfo));
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
	VkImage image;

	VkCheck(vkCreateImage(vkDevice, &info, nullptr, &image));

	static VkDeviceMemory imageMemPool = VK_NULL_HANDLE;
	//TODO: this has to be a big mempool for images (or several pools?)
	//will quickly go over hardware allocation limits if one alloc per image
	//freeing individual images and reallocing blocks will be a hassle though
	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements(vkDevice, image, &memReq);

	if (imageMemPool == VK_NULL_HANDLE)
	{
		VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		//alloc.allocationSize = memReq.size;
		alloc.allocationSize = 1024 * 1024 * 256; //256MB texture memory?
		alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VkCheck(vkAllocateMemory(vkDevice, &alloc, nullptr, &imageMemPool));
	}

	static VkDeviceSize memPoolOffset = 0;
	VkCheck(vkBindImageMemory(vkDevice, image, imageMemPool, memPoolOffset));

	memPoolOffset += (memReq.size + memReq.alignment) & -memReq.alignment;

	return image;
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
	vkDestroyImage(vkDevice, image, nullptr);
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
	info.renderPass = vkRenderPass;

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

void Vk_ClearDepthStencilImage(bool depth, bool stencil, byte value)
{
	if (!depth && !stencil)
		return;

	VkClearAttachment attachment = {};
	attachment.aspectMask = 0;

	if (depth)
	{
		attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		attachment.clearValue.depthStencil.depth = 1.0f;
	}

	if (stencil)
	{
		attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		attachment.clearValue.depthStencil.stencil = value;
	}

	VkClearRect rect = {};
	rect.layerCount = 1;
	rect.rect = { 
		0, 0,
		(uint32_t)renderSystem->GetWidth(), (uint32_t)renderSystem->GetHeight()
	};

	vkCmdClearAttachments(Vk_ActiveCommandBuffer(), 1, &attachment, 1, &rect);
}

#endif