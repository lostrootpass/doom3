#ifndef ID_RENDER_SYSTEM_VK_H_
#define ID_RENDER_SYSTEM_VK_H_

#ifdef DOOM3_VULKAN
class idRenderSystemVk : public idRenderSystemLocal
{

public:
	idRenderSystemVk();

	~idRenderSystemVk();

	virtual void Shutdown() override;

	virtual void InitRenderBackend() override;

	virtual void ShutdownRenderBackend() override;

	virtual stereo3DMode_t GetStereo3DMode() const override;

	virtual bool HasQuadBufferSupport() const override;

	virtual bool IsStereoScopicRenderingSupported() const override;

	virtual stereo3DMode_t GetStereoScopicRenderingMode() const override;

	virtual void EnableStereoScopicRendering(const stereo3DMode_t mode) const override;

	virtual float GetPhysicalScreenWidthInCentimeters() const override;

	virtual void SwapCommandBuffers_FinishRendering(uint64 *frontEndMicroSec, uint64 *backEndMicroSec, uint64 *shadowMicroSec, uint64 *gpuMicroSec) override;

	virtual const emptyCommand_t * SwapCommandBuffers_FinishCommandBuffers() override;

	virtual void RenderCommandBuffers(const emptyCommand_t * commandBuffers) override;

	virtual void TakeScreenshot(int width, int height, const char *fileName, int downSample, renderView_t *ref) override;

	virtual void CaptureRenderToImage(const char *imageName, bool clearColorAfterCopy = false) override;

	virtual void CaptureRenderToFile(const char *fileName, bool fixAlpha) override;

	virtual void Clear() override;

	virtual void QueueImagePurge(idImage* image);

	virtual void SetCull(int cullType) override;
	virtual void SetScissor(int x/*left*/, int y/*bottom*/, int w, int h) override;
	virtual void SetViewport(int x/*left*/, int y/*bottom*/, int w, int h) override;
	virtual void SetPolygonOffset(float scale, float bias) override;
	virtual void SetDepthBoundsTest(const float zmin, const float zmax);
	virtual void Clear(bool color, bool depth, bool stencil, byte stencilValue,
		float r, float g, float b, float a) override;
	virtual void SetDefaultState() override;
	virtual void SetState(uint64 stateBits, bool forceState = false) override;

private:
	idList<idImage*> purgeQueue;
};
#endif

#endif