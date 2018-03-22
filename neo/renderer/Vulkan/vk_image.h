#ifndef ID_IMAGE_VK_H_
#define ID_IMAGE_VK_H_

#include "../tr_local.h"

#ifdef DOOM3_VULKAN
#include <vulkan/vulkan.h>

class idImageVk : public idImage {
public:
	idImageVk(const char* name) : idImage(name) {}

	virtual void FinaliseImageUpload() override;
	virtual void SubImageUpload(int mipLevel, int x, int y, int z, int width, 
		int height, const void * pic, int pixelPitch = 0) const override;
	virtual void SetPixel(int mipLevel, int x, int y, const void * data, 
		int dataSize) override;
	virtual void SetTexParameters() override;
	virtual void AllocImage() override;
	virtual void PurgeImage() override;
	virtual void ActuallyPurgeImage(bool force = false) override;
	virtual void Resize(int width, int height) override;
	virtual void SetSamplerState(textureFilter_t tf, 
		textureRepeat_t tr) override;
	virtual void Bind() override;
	virtual void CopyFramebuffer(int x, int y, int imageWidth, 
		int imageHeight) override;
	virtual void CopyDepthbuffer(int x, int y, int imageWidth,
		int imageHeight) override;

private:

	bool AllocImageInternal(VkImage& newImage, VkImageView& newView);
	void UpdateDescriptorSet();
	void CopyImageInternal(int imageWidth, int imageHeight, VkImage img, 
		VkImageAspectFlags aspect);
	ID_INLINE VkImageAspectFlags Aspect() const;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;
	VkBuffer deviceBuffer;
	VkDeviceMemory deviceMemory;
	VkImage image;
	VkImageView imageView;
	VkFormat format;
	VkSampler sampler = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
};

#endif

#endif