#ifndef VULKAN_IMPL_UTIL_H_
#define VULKAN_IMPL_UTIL_H_

#ifdef DOOM3_VULKAN

#include <vulkan/vulkan.h>
#include <vector>
#include <assert.h>

//const char* VK_VALIDATION_LAYERS[] = {
//	VK_EXT_DEBUG_REPORT_EXTENSION_NAME
//};

#ifndef NDEBUG
#define VkCheck(x) VulkanUtil::CheckResult(x)
#else
#define VkCheck(x) (x)
#endif

struct VulkanUtil
{
#ifndef NDEBUG
	static const bool DEBUGENABLE = true;
#else
	static const bool DEBUGENABLE = false;
#endif

	static const std::vector<const char*> VALIDATION_LAYERS;// = { VK_EXT_DEBUG_REPORT_EXTENSION_NAME };

	static bool getRequiredExtensions(std::vector<const char*>& extensions)
	{
		extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
		extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

		if (DEBUGENABLE)
		{
			for(size_t i = 0; i < VALIDATION_LAYERS.size(); i++)
				extensions.push_back(VALIDATION_LAYERS[i]);
		}

		return true;
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugReportFlagsEXT flags,
		VkDebugReportObjectTypeEXT objType,
		uint64_t obj,
		size_t location,
		int32_t code,
		const char* layerPrefix,
		const char* msg,
		void* userData)
	{
		printf("Validation layer: %s\r\n", msg);

		return VK_FALSE;
	}

	static void CheckResult(VkResult res)
	{
		assert(res == VK_SUCCESS);
	}
};

#endif

#endif //VULKAN_IMPL_UTIL_H_