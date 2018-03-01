#pragma hdrstop
#include "../../idlib/precompiled.h"

/*
================================================================================================
Contains the Image implementation for OpenGL.
================================================================================================
*/

#include "../tr_local.h"

#ifdef DOOM3_VULKAN

#include <vulkan/vulkan.h>
#include <vector>

const int VK_IMAGE_SET_OFFSET = 2;

static inline size_t ByteSize(uint32_t w, uint32_t h, const idImageOpts& opts)
{
	return (w * h * BitsForFormat(opts.format)) / 8;
}

static inline size_t PadAlign(uint32_t offset)
{
	return (offset + 3) & ~3;
}

static inline size_t MipOffset(int mipLevel, const idImageOpts& opts)
{
	uint32_t w = 0, h = 0;
	size_t offset = 0;

	for (int i = 0; i < mipLevel; ++i)
	{
		w = Max(opts.width >> i, 1);
		h = Max(opts.height >> i, 1);
		offset += PadAlign(ByteSize(w, h, opts));
	}

	return offset;
}

static inline size_t FaceOffset(int z, const idImageOpts& opts)
{
	uint32_t mipWidth = 0, mipHeight = 0;
	size_t offset = 0;

	for (int i = 0; i < z; ++i)
	{
		for (int k = 0; k < opts.numLevels; ++k)
		{
			mipWidth = Max(opts.width >> k, 1);
			mipHeight = Max(opts.height >> k, 1);
			offset += PadAlign(ByteSize(mipWidth, mipHeight, opts));
		}
	}

	return offset;
}

void idImage::FinaliseImageUpload()
{
	VkImageViewType target = VK_IMAGE_VIEW_TYPE_2D;
	uint32_t layerCount = 1;
	switch ( opts.textureType ) {
		case TT_2D:
			target = VK_IMAGE_VIEW_TYPE_2D;
			layerCount = 1;
			break;
		case TT_CUBIC:
			target = VK_IMAGE_VIEW_TYPE_CUBE;
			layerCount = 6;
			break;
		default:
			idLib::FatalError( "%s: bad texture type %d", GetName(), opts.textureType );
			return;
	}
	

	VkExtent3D extent = { 
		(uint32_t)opts.width, (uint32_t)opts.height, 1
	};
	
	std::vector<VkBufferImageCopy> copies;
	VkDeviceSize offset = 0;

	const uint32_t numFaces = opts.textureType == TT_CUBIC ? 6 : 1;
	for (uint32_t f = 0; f < numFaces; ++f)
	{
		for (int i = 0; i < opts.numLevels; ++i)
		{
			uint32_t mipWidth = Max(opts.width >> i, 1);
			uint32_t mipHeight = Max(opts.height >> i, 1);

			VkBufferImageCopy copy = {};
			copy.imageExtent = { mipWidth, mipHeight, 1 };
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.baseArrayLayer = f;
			copy.imageSubresource.layerCount = 1;
			copy.imageSubresource.mipLevel = i;
			copy.bufferOffset = offset;

			if (opts.format == FMT_BGRA8)
			{
				copy.bufferImageHeight = opts.height;
				copy.bufferRowLength = opts.width;
			}
			else
			{
				copy.bufferImageHeight = (mipHeight + 3) & ~3;// opts.height;
				copy.bufferRowLength = (mipWidth + 3) & ~3;// opts.width;
			}

			copies.push_back(copy);
			offset += PadAlign(ByteSize(mipWidth, mipHeight, opts));
		}
	}

	VkCommandBuffer cmd = Vk_StartOneShotCommandBuffer();
	vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)copies.size(), copies.data());
	Vk_SubmitOneShotCommandBuffer(cmd);

	Vk_DestroyBuffer(stagingBuffer);
	Vk_FreeMemory(stagingMemory);
	stagingMemory = stagingBuffer = VK_NULL_HANDLE;

	VkImageSubresourceRange range = {};
	range.layerCount = layerCount;
	range.levelCount = opts.numLevels;
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Vk_SetImageLayout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	VkComponentSwizzle r, g, b, a;
	if ( opts.colorFormat == CFM_GREEN_ALPHA ) {
		r = g = b = VK_COMPONENT_SWIZZLE_ONE;
		a = VK_COMPONENT_SWIZZLE_G;
	} else if ( opts.format == FMT_LUM8 ) {
		r = g = b = VK_COMPONENT_SWIZZLE_R;
		a = VK_COMPONENT_SWIZZLE_ONE;
	} else if ( opts.format == FMT_L8A8 ) {
		r = g = b = VK_COMPONENT_SWIZZLE_R;
		a = VK_COMPONENT_SWIZZLE_G;
	} else if ( opts.format == FMT_ALPHA ) {
		r = g = b = VK_COMPONENT_SWIZZLE_ONE;
		a = VK_COMPONENT_SWIZZLE_R;
	} else if ( opts.format == FMT_INT8 ) {
		r = g = b = a = VK_COMPONENT_SWIZZLE_R;
	} else {
		r = VK_COMPONENT_SWIZZLE_R;
		g = VK_COMPONENT_SWIZZLE_G;
		b = VK_COMPONENT_SWIZZLE_B;
		a = VK_COMPONENT_SWIZZLE_A;
	}

	VkFilter minFilter, magFilter;
	switch( filter ) {
		case TF_DEFAULT:
			if ( r_useTrilinearFiltering.GetBool() ) {
				minFilter = VK_FILTER_LINEAR;
			} else {
				minFilter = VK_FILTER_NEAREST;
			}
			magFilter = VK_FILTER_LINEAR;
			break;
		case TF_LINEAR:
			minFilter = VK_FILTER_LINEAR;
			magFilter = VK_FILTER_LINEAR;
			break;
		case TF_NEAREST:
			minFilter = VK_FILTER_NEAREST;
			magFilter = VK_FILTER_NEAREST;
			break;
		default:
			common->FatalError( "%s: bad texture filter %d", GetName(), filter );
	}

	VkBool32 anisoEnabled = VK_FALSE;
	float maxAniso = 1.0f;
	if ( glConfig.anisotropicFilterAvailable ) {
		// only do aniso filtering on mip mapped images
		if ( filter == TF_DEFAULT ) {
			int aniso = r_maxAnisotropicFiltering.GetInteger();
			if ( aniso > glConfig.maxTextureAnisotropy ) {
				aniso = glConfig.maxTextureAnisotropy;
			}
			if ( aniso < 0 ) {
				aniso = 0;
			}
			maxAniso = (float)aniso;
			anisoEnabled = VK_TRUE;
		}
	}

	float lodBias = 1.0f;
	if ( glConfig.textureLODBiasAvailable && ( usage != TD_FONT ) ) {
		// use a blurring LOD bias in combination with high anisotropy to fix our aliasing grate textures...
		lodBias = r_lodBias.GetFloat();
	}

	// set the wrap/clamp modes
	VkSamplerAddressMode u, v;
	VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	switch( repeat ) {
		case TR_REPEAT:
			u = v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;
		case TR_CLAMP_TO_ZERO: {
			borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			u = v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			}
			break;
		case TR_CLAMP_TO_ZERO_ALPHA: {
			borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			u = v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			}
			break;
		case TR_CLAMP:
			u = v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		default:
			common->FatalError( "%s: bad texture repeat %d", GetName(), repeat );
	}



	{
		VkSamplerCreateInfo samplerCreateInfo = {};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.addressModeU = u;
		samplerCreateInfo.addressModeV = v;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.minFilter = minFilter;
		samplerCreateInfo.magFilter = magFilter;
		samplerCreateInfo.maxLod = 1.0f;
		samplerCreateInfo.borderColor = borderColor;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.anisotropyEnable = anisoEnabled;
		samplerCreateInfo.maxAnisotropy = maxAniso;
		samplerCreateInfo.mipLodBias = lodBias;
		
		vkCreateSampler(Vk_GetDevice(), &samplerCreateInfo, nullptr, &sampler);
	}

	UpdateDescriptorSet();
}

/*
========================
idImage::SubImageUpload
========================
*/
void idImage::SubImageUpload(int mipLevel, int x, int y, int z, int width, int height, const void * pic, int pixelPitch) const {
	assert( x >= 0 && y >= 0 && mipLevel >= 0 && width >= 0 && height >= 0 && mipLevel < opts.numLevels );

	int compressedSize = 0;

	if ( IsCompressed() ) {
		assert( !(x&3) && !(y&3) );

		// compressed size may be larger than the dimensions due to padding to quads
		int quadW = ( width + 3 ) & ~3;
		int quadH = ( height + 3 ) & ~3;
		compressedSize = quadW * quadH * BitsForFormat( opts.format ) / 8;

		int padW = ( opts.width + 3 ) & ~3;
		int padH = ( opts.height + 3 ) & ~3;
		(void)padH;
		(void)padW;
		assert( x + width <= padW && y + height <= padH );
		// upload the non-aligned value, OpenGL understands that there
		// will be padding
		if ( x + width > opts.width ) {
			width = opts.width - x;
		}
		if ( y + height > opts.height ) {
			height = opts.height - x;
		}
	} else {
		assert( x + width <= opts.width && y + height <= opts.height );
	}

	VkDeviceSize size = PadAlign(ByteSize(width, height, opts));
	size = Max(size, (VkDeviceSize)compressedSize);

	if (size == 0) return;

	//All the mips for each face are stored next to each other, so run through
	//them and figure out the offset for the whole face to use as a start point
	//then we can further offset to the specific mip we want.
	VkDeviceSize offset = FaceOffset(z, opts) + MipOffset(mipLevel, opts);

	void* ptr = Vk_MapMemory(stagingMemory, offset, size, 0);

	//For RGB565 images only, swap byte order to simulate GL_UNPACK_SWAP_BYTES
	//RGB565 images are always going to be the light projection textures
	//Could probably do this without the alloc and swap the bytes on load instead
	if (opts.format == FMT_RGB565)
	{
		uint16_t* img = (uint16_t*)pic;
		uint16_t* copy = new uint16_t[size * 2]{ 0 };
		uint8_t lo, hi;
		for (VkDeviceSize i = 0; i < size/2; ++i)
		{
			lo = (img[i] & 0xff);
			hi = (img[i] >> 8) & 0xff;
			copy[i] = lo << 8 | hi;
		}

		memcpy(ptr, copy, size);
		delete[] copy;
	}
	else
	{
		memcpy(ptr, pic, size);
	}

	Vk_UnmapMemory(stagingMemory);
}

/*
========================
idImage::SetPixel
========================
*/
void idImage::SetPixel(int mipLevel, int x, int y, const void * data, int dataSize) {
	SubImageUpload(mipLevel, x, y, 0, 1, 1, data);
}

/*
========================
idImage::SetTexParameters
========================
*/
void idImage::SetTexParameters() {
}

/*
========================
idImage::AllocImage

Every image will pass through this function. Allocates all the necessary MipMap levels for the 
Image, but doesn't put anything in them.

This should not be done during normal game-play, if you can avoid it.
========================
*/
void idImage::AllocImage() {
	PurgeImage();

	VkDeviceSize size = 0;
	if (IsCompressed())
	{
		size = (((opts.width + 3) / 4) * ((opts.height + 3) / 4) * int64(16));
		size *= opts.numLevels;

		if (opts.textureType == TT_CUBIC)
			size *= 6;
			
		size = (size * BitsForFormat(opts.format)) / 8;
	}
	else
	{
		size = opts.width * opts.height * opts.numLevels;
		
		if (opts.textureType == TT_CUBIC)
			size *= 6;

		size = size * BitsForFormat(opts.format) / 8;
	}

	if (size == 0) return;

	//TODO: don't do this for every image!
	VkBufferCreateInfo buff = {};
	buff.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buff.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	buff.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buff.size = size;

	stagingBuffer = Vk_CreateAndBindBuffer(buff, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingMemory);
	
	bool res = AllocImageInternal(image, imageView);
	assert(res);

	VkImageSubresourceRange range = {};
	range.layerCount = opts.textureType == TT_CUBIC ? 6 : 1;
	range.levelCount = opts.numLevels;
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	Vk_SetImageLayout(image, format, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);

	SetTexParameters();
}

/*
========================
idImage::PurgeImage
========================
*/

void idImage::PurgeImage()
{
	if(texnum != TEXTURE_NOT_LOADED)
		renderSystem->QueueImagePurge(this);
}

void idImage::ActuallyPurgeImage() {
	if (texnum != TEXTURE_NOT_LOADED) {
		if(sampler != VK_NULL_HANDLE)
			vkDestroySampler(Vk_GetDevice(), sampler, nullptr);
		
		Vk_DestroyImageAndView(image, imageView);
		
		if(descriptorSet != VK_NULL_HANDLE)
			Vk_FreeDescriptorSet(descriptorSet);
		
		texnum = TEXTURE_NOT_LOADED;
		image = imageView = sampler = VK_NULL_HANDLE;
		descriptorSet = VK_NULL_HANDLE;
	}

	// clear all the current binding caches, so the next bind will do a real one
	for ( int i = 0 ; i < MAX_MULTITEXTURE_UNITS ; i++ ) {
		backEnd.glState.tmu[i].current2DMap = TEXTURE_NOT_LOADED;
		backEnd.glState.tmu[i].currentCubeMap = TEXTURE_NOT_LOADED;
	}
}

/*
========================
idImage::Resize
========================
*/
void idImage::Resize( int width, int height ) {
	if ( opts.width == width && opts.height == height ) {
		return;
	}
	opts.width = width;
	opts.height = height;

	VkImage newImage;
	VkImageView newView;

	bool res = AllocImageInternal(newImage, newView);
	assert(res);

	Vk_DestroyImageAndView(image, imageView);
	image = newImage;
	imageView = newView;

	UpdateDescriptorSet();

	//vkDeviceWaitIdle(Vk_GetDevice());
}

/*
========================
idImage::SetSamplerState
========================
*/
void idImage::SetSamplerState(textureFilter_t tf, textureRepeat_t trep) {

}


/*
==============
Bind

Automatically enables 2D mapping or cube mapping if needed
==============
*/
void idImage::Bind() {
	RENDERLOG_PRINTF( "idImage::Bind( %s )\n", GetName() );

	// load the image if necessary (FIXME: not SMP safe!)
	if ( !IsLoaded() ) {
		// load the image on demand here, which isn't our normal game operating mode
		ActuallyLoadImage( true );
	}

	const int texUnit = backEnd.glState.currenttmu;

	tmu_t * tmu = &backEnd.glState.tmu[texUnit];
	// bind the texture
	if ( opts.textureType == TT_2D ) {
			tmu->current2DMap = texnum;
	} else if ( opts.textureType == TT_CUBIC ) {
			tmu->currentCubeMap = texnum;
	}

	vkCmdBindDescriptorSets(Vk_ActiveCommandBuffer(), 
		VK_PIPELINE_BIND_POINT_GRAPHICS, Vk_GetPipelineLayout(),
		texUnit + VK_IMAGE_SET_OFFSET, 1, &descriptorSet, 0, nullptr);
}

/*
====================
CopyFramebuffer
====================
*/
void idImage::CopyFramebuffer(int x, int y, int imageWidth, int imageHeight) {
	//Consider this more of a "pause" than an "end" - 
	//after we copy, we resume rendering to the same backbuffer w/o clearing
	Vk_EndRenderPass();

	//If the backbuffer has been resized then we need to resize this image too
	if (!(opts.width == imageWidth && opts.height == imageHeight))
	{
		Resize(imageWidth, imageHeight);
	}

	VkImage img = Vk_ActiveColorBuffer();

	VkImageSubresourceRange range = {};
	range.layerCount = 1;
	range.levelCount = 1;
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageMemoryBarrier prep = {};
	prep.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	prep.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	prep.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	prep.image = image;
	prep.subresourceRange = range;
	prep.srcAccessMask = 0;
	prep.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(Vk_ActiveCommandBuffer(),
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_DEPENDENCY_BY_REGION_BIT,
		0, nullptr, 0, nullptr, 1, &prep);

	VkImageCopy region = {};
	region.extent = { (uint32_t)opts.width, (uint32_t)opts.height, 1};
	region.dstSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = 1;
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	vkCmdCopyImage(Vk_ActiveCommandBuffer(), 
		img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region);

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.image = image;
	barrier.subresourceRange = range;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(Vk_ActiveCommandBuffer(),
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);

	//Restart the render pass to continue rendering, but don't clear.
	Vk_StartRenderPass();
}


/*
====================
CopyDepthbuffer
====================
*/
void idImage::CopyDepthbuffer(int x, int y, int imageWidth, int imageHeight) {

}

bool idImage::AllocImageInternal(VkImage& newImage, VkImageView& newView)
{
	if (!R_IsInitialized()) {
		return false;
	}


	switch (opts.format) {
	case FMT_RGBA8:
		format = VK_FORMAT_R8G8B8A8_UNORM;
		break;
	case FMT_BGRA8:
		format = VK_FORMAT_B8G8R8A8_UNORM;
		break;
	case FMT_XRGB8:
		format = VK_FORMAT_R8G8B8_UNORM;
		break;
	case FMT_ALPHA:
		format = VK_FORMAT_R8_UNORM;
		break;
	case FMT_L8A8:
		format = VK_FORMAT_R8G8_UNORM;
		break;
	case FMT_LUM8:
		format = VK_FORMAT_R8_UNORM;
		break;
	case FMT_INT8:
		format = VK_FORMAT_R8_UNORM;
		break;
	case FMT_DXT1:
		format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
		break;
	case FMT_DXT5:
		format = VK_FORMAT_BC3_UNORM_BLOCK;
		break;
	case FMT_DEPTH:
		format = VK_FORMAT_UNDEFINED;
		break;
	case FMT_X16:
		format = VK_FORMAT_R16_UNORM;
		break;
	case FMT_Y16_X16:
		format = VK_FORMAT_R16G16_UNORM;
		break;
	case FMT_RGB565:
		format = VK_FORMAT_R5G6B5_UNORM_PACK16;
		break;
	default:
		format = VK_FORMAT_UNDEFINED;
		idLib::Error( "Unhandled image format %d in %s\n", opts.format, GetName() );
		break;
	}

	VkImageViewType target = VK_IMAGE_VIEW_TYPE_2D;
	uint32_t layerCount = 1;
	switch ( opts.textureType ) {
		case TT_2D:
			target = VK_IMAGE_VIEW_TYPE_2D;
			layerCount = 1;
			break;
		case TT_CUBIC:
			target = VK_IMAGE_VIEW_TYPE_CUBE;
			layerCount = 6;
			break;
		default:
			idLib::FatalError( "%s: bad texture type %d", GetName(), opts.textureType );
			return false;
	}

	VkComponentSwizzle r, g, b, a;
	if ( opts.colorFormat == CFM_GREEN_ALPHA ) {
		r = g = b = VK_COMPONENT_SWIZZLE_ONE;
		a = VK_COMPONENT_SWIZZLE_G;
	} else if ( opts.format == FMT_LUM8 ) {
		r = g = b = VK_COMPONENT_SWIZZLE_R;
		a = VK_COMPONENT_SWIZZLE_ONE;
	} else if ( opts.format == FMT_L8A8 ) {
		r = g = b = VK_COMPONENT_SWIZZLE_R;
		a = VK_COMPONENT_SWIZZLE_G;
	} else if ( opts.format == FMT_ALPHA ) {
		r = g = b = VK_COMPONENT_SWIZZLE_ONE;
		a = VK_COMPONENT_SWIZZLE_R;
	} else if ( opts.format == FMT_INT8 ) {
		r = g = b = a = VK_COMPONENT_SWIZZLE_R;
	} else {
		r = VK_COMPONENT_SWIZZLE_R;
		g = VK_COMPONENT_SWIZZLE_G;
		b = VK_COMPONENT_SWIZZLE_B;
		a = VK_COMPONENT_SWIZZLE_A;
	}
	
	VkExtent3D extent = { (uint32_t)opts.width, (uint32_t)opts.height, (uint32_t)1 };


	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.extent = extent;
	info.arrayLayers = opts.textureType == TT_CUBIC ? 6 : 1;
	info.mipLevels = opts.numLevels;
	info.format = format;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	
	if (opts.textureType == TT_CUBIC)
		info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;


	newImage = Vk_AllocAndCreateImage(info, deviceMemory);

	VkImageSubresourceRange range = {};
	range.layerCount = layerCount;
	range.levelCount = opts.numLevels;
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.components = { r, g, b, a };
	view.image = newImage;
	view.viewType = target;
	view.format = format;
	view.subresourceRange = range;

	texnum = (GLuint)newView;
	newView = Vk_CreateImageView(view);

	return true;
}

void idImage::UpdateDescriptorSet()
{
	Vk_QueueDestroyDescriptorSet(descriptorSet);
	descriptorSet = Vk_AllocDescriptorSetForImage();
	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = imageView;
	imageInfo.sampler = sampler;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.dstBinding = 0;
	write.dstSet = descriptorSet;
	write.pImageInfo = &imageInfo;
			
	vkUpdateDescriptorSets(Vk_GetDevice(), 1, &write, 0, 0);
}

#endif