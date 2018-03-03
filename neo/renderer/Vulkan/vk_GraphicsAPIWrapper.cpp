
#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "../tr_local.h"

#ifdef DOOM3_VULKAN

void idRenderSystemVk::SetState(uint64 stateBits, bool forceGlState) {
	backEnd.glState.glStateBits = stateBits;
}

void idRenderSystemVk::SetCull(int cullType) {
	backEnd.glState.faceCulling = cullType;
}

void idRenderSystemVk::SetScissor(int x, int y, int w, int h) {
	VkRect2D sc = { 
		x,
		Max(0, renderSystem->GetHeight() - y - h),
		w, h
	};
	vkCmdSetScissor(Vk_ActiveCommandBuffer(), 0, 1, &sc);
}

void idRenderSystemVk::SetViewport(int x, int y, int w, int h) {
	VkViewport vp = { 
		x,
		Max(0, renderSystem->GetHeight() - y - h),
		w,
		h, 0.0f, 1.0f };
	vkCmdSetViewport(Vk_ActiveCommandBuffer(), 0, 1, &vp);
}

void idRenderSystemVk::SetPolygonOffset(float scale, float bias) {
	backEnd.glState.polyOfsScale = scale;
	backEnd.glState.polyOfsBias = bias;

	vkCmdSetDepthBias(Vk_ActiveCommandBuffer(), bias, 0.0f, scale);
}

void idRenderSystemVk::SetDepthBoundsTest(const float zmin, const float zmax) {
	if ( !glConfig.depthBoundsTestAvailable || zmin > zmax ) {
		return;
	}

	float m = zmax == 0.0f ? 1.0f : zmax;
	vkCmdSetDepthBounds(Vk_ActiveCommandBuffer(), zmin, m);
}

void idRenderSystemVk::Clear(bool color, bool depth, bool stencil,
	byte stencilValue, float r, float g, float b, float a) {
	uint32_t mask = 0;
	mask |= (color ? VK_IMAGE_ASPECT_COLOR_BIT : 0);
	mask |= (depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0);
	mask |= (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);

	Vk_ClearAttachments(mask, stencilValue);
}

void idRenderSystemVk::SetDefaultState() {
	memset( &backEnd.glState, 0, sizeof( backEnd.glState ) );
	renderSystem->SetState( 0, true );

	if ( r_useScissor.GetBool() ) {
		renderSystem->SetScissor( 0, 0, renderSystem->GetWidth(), renderSystem->GetHeight() );
		renderSystem->SetViewport(0, 0, renderSystem->GetWidth(), renderSystem->GetHeight());
	}
}

#endif