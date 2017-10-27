#pragma hdrstop
#include "../../idlib/precompiled.h"

/*
================================================================================================
Contains the Image implementation for OpenGL.
================================================================================================
*/

#include "../tr_local.h"
#include <vulkan/vulkan.h>
#include <vector>

#ifdef DOOM3_VULKAN

void idImage::FinaliseImageUpload()
{
	VkImageSubresourceRange range = {};
	range.layerCount = 1;
	range.levelCount = 1;
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Vk_SetImageLayout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	view.image = image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	view.format = format;
	view.subresourceRange = range;

	imageView = Vk_CreateImageView(view);
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

	VkExtent3D extent = { 
		(uint32_t)opts.width, (uint32_t)opts.height, (uint32_t)opts.numLevels
	};
	
	std::vector<VkBufferImageCopy> copies;
	{
		VkBufferImageCopy copy = {};
		copy.imageExtent = extent;
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.baseArrayLayer = 0;
		copy.imageSubresource.layerCount = 1;
		copy.bufferOffset = 0;
		copy.bufferImageHeight = opts.height;
		copy.bufferRowLength = opts.width;
		copy.imageSubresource.mipLevel = 0;

		copies.push_back(copy);
	}

	VkCommandBuffer cmd = Vk_StartOneShotCommandBuffer();
	vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)copies.size(), copies.data());
	Vk_SubmitOneShotCommandBuffer(cmd);	
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
	//TODO
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

	switch (opts.format) {
	case FMT_RGBA8:
		format = VK_FORMAT_R8G8B8A8_UNORM;
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
		format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		break;
	case FMT_DXT5:
		format = VK_FORMAT_BC2_UNORM_BLOCK;
		break;
	case FMT_DEPTH:
		format = VK_FORMAT_UNDEFINED;
		break;
	case FMT_X16:
		format = VK_FORMAT_UNDEFINED;
		break;
	case FMT_Y16_X16:
		format = VK_FORMAT_UNDEFINED;
		break;
	case FMT_RGB565:
		format = VK_FORMAT_R5G6B5_UNORM_PACK16;
		break;
	default:
		idLib::Error( "Unhandled image format %d in %s\n", opts.format, GetName() );
		break;

	}

	if (!R_IsInitialized()) {
		return;
	}

	VkDeviceSize size = opts.width * opts.height * BitsForFormat(opts.format);

	VkExtent3D extent = { (uint32_t)opts.width, (uint32_t)opts.height, (uint32_t)1 };

	VkBufferCreateInfo buff = {};
	buff.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buff.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	buff.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buff.size = size;

	stagingBuffer = Vk_CreateAndBindBuffer(buff, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.extent = extent;
	info.arrayLayers = 1;
	info.mipLevels = 1;
	info.format = format;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.samples = VK_SAMPLE_COUNT_1_BIT;


	image = Vk_AllocAndCreateImage(info);

	VkImageSubresourceRange range = {};
	range.layerCount = 1;
	range.levelCount = 1;
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	Vk_SetImageLayout(image, format, info.initialLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);

	SetTexParameters();
}

/*
========================
idImage::PurgeImage
========================
*/
void idImage::PurgeImage() {
	if (texnum != TEXTURE_NOT_LOADED) {
		Vk_DestroyImageAndView(image, imageView);
		Vk_DestroyBuffer(stagingBuffer);
		texnum = TEXTURE_NOT_LOADED;
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
	AllocImage();
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
		if ( tmu->current2DMap != texnum ) {
			tmu->current2DMap = texnum;
			//qglBindMultiTextureEXT( GL_TEXTURE0_ARB + texUnit, GL_TEXTURE_2D, texnum );
		}
	} else if ( opts.textureType == TT_CUBIC ) {
		if ( tmu->currentCubeMap != texnum ) {
			tmu->currentCubeMap = texnum;
			//qglBindMultiTextureEXT( GL_TEXTURE0_ARB + texUnit, GL_TEXTURE_CUBE_MAP_EXT, texnum );
		}
	}
}

/*
====================
CopyFramebuffer
====================
*/
void idImage::CopyFramebuffer(int x, int y, int imageWidth, int imageHeight) {

}


/*
====================
CopyDepthbuffer
====================
*/
void idImage::CopyDepthbuffer(int x, int y, int imageWidth, int imageHeight) {

}

#endif