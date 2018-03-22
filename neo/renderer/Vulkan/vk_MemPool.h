#ifndef ID_VK_MEMPOOL_H_
#define ID_VK_MEMPOOL_H_

#include <vulkan/vulkan.h>
#include "../tr_local.h"

class idMemPoolVk
{
public:
	VkImage AllocImage(const VkImageCreateInfo& info);
	void FreeImage(VkImage image);

private:
	struct ImageMemBlock
	{
		ImageMemBlock() : startAddress(0) {};
		explicit ImageMemBlock(uintptr_t offset) : startAddress(offset) {}

		VkDeviceSize size = 0 ;
		VkImage image = VK_NULL_HANDLE;

		uintptr_t startAddress = 0;
	};

	struct ImageMemPool
	{
		idList<ImageMemBlock, TAG_IDLIB_LIST> blocks;

		VkDeviceMemory deviceMemory = 0;
		size_t allocatedSize = 0;

		static const size_t MEMPOOL_SIZE = 1024 * 1024 * 64;
	};

	idList<ImageMemPool, TAG_IDLIB_LIST> pools;
};

#endif //ID_VK_MEMPOOL_H_