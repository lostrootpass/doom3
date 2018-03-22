#ifndef ID_IMAGE_GL_H_
#define ID_IMAGE_GL_H_

#include "../tr_local.h"

class idImageGL : public idImage {
public:
	idImageGL(const char* name) : idImage(name) {}

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
};

#endif