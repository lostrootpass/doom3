#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "win_vkutil.h"

#ifdef DOOM3_VULKAN

const std::vector<const char*> VulkanUtil::VALIDATION_LAYERS = 
{
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME 
};

#endif