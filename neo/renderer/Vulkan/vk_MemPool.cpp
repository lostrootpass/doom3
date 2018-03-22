#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "vk_MemPool.h"
#include "sys/win32/win_vkutil.h"

VkImage idMemPoolVk::AllocImage(const VkImageCreateInfo& info)
{
	VkImage image;

	VkCheck(vkCreateImage(Vk_GetDevice(), &info, nullptr, &image));

	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements(Vk_GetDevice(), image, &memReq);

	VkDeviceSize size = (memReq.size + memReq.alignment) & -memReq.alignment;

	ImageMemBlock* destBlock = nullptr;
	ImageMemPool* destPool = nullptr;

	for (int poolIdx = 0; poolIdx < pools.Num(); ++poolIdx)
	{
		size_t end = 0;

		idList<ImageMemBlock, TAG_IDLIB_LIST>& blocks = pools[poolIdx].blocks;
		for (int blockIdx = 0; blockIdx < blocks.Num(); ++blockIdx)
		{
			ImageMemBlock& block = blocks[blockIdx];

			if (block.startAddress - end >= size)
			{
				blocks.Insert(ImageMemBlock(end), blockIdx);
				destBlock = &blocks[blockIdx];
				break;
			}
			
			end = block.startAddress + block.size;
		}

		if (destBlock != nullptr)
		{
			//We found a spot in the middle of the pool.
			destPool = &pools[poolIdx];
			break;
		}
		else
		{
			//No spot in the middle, check for space at the end.
			if (end + size <= ImageMemPool::MEMPOOL_SIZE)
			{
				int idx = blocks.Append(ImageMemBlock(end));
				destBlock = &blocks[idx];
				destPool = &pools[poolIdx];
				break;
			}
		}
	}

	if (destPool == nullptr)
	{
		uint32_t memType = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		int idx = pools.Append(ImageMemPool());
		destPool = &pools[idx];

		VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc.allocationSize = ImageMemPool::MEMPOOL_SIZE;
		alloc.memoryTypeIndex = Vk_GetMemoryTypeIndex(memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VkCheck(vkAllocateMemory(Vk_GetDevice(), &alloc, nullptr, 
			&destPool->deviceMemory));

		int b = destPool->blocks.Append(ImageMemBlock(0));
		destBlock = &destPool->blocks[b];
	}

	VkCheck(vkBindImageMemory(Vk_GetDevice(), image, 
		destPool->deviceMemory, destBlock->startAddress));

	destBlock->size = size;
	destBlock->image = image;

	return image;
}

void idMemPoolVk::FreeImage(VkImage image)
{
	for (int poolIdx = 0; poolIdx < pools.Num(); ++poolIdx)
	{
		idList<ImageMemBlock, TAG_IDLIB_LIST>& blocks = pools[poolIdx].blocks;
		for (int blockIdx = 0; blockIdx < blocks.Num(); ++blockIdx)
		{
			if (blocks[blockIdx].image == image)
			{
				vkDestroyImage(Vk_GetDevice(), image, nullptr);

				//Important to preserve list order here so do a slow remove
				blocks.RemoveIndex(blockIdx);
				break;
			}
		}

		//If we removed the last block in the pool, clean the pool up, too.
		if (blocks.Num() == 0)
		{
			vkFreeMemory(Vk_GetDevice(), pools[poolIdx].deviceMemory, nullptr);

			pools.RemoveIndexFast(poolIdx);
			break;
		}
	}
}
