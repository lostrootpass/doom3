#ifndef ID_RENDER_BACKEND_GL_H_
#define ID_RENDER_BACKEND_GL_H_

class idRenderBackendGL : public idRenderBackend
{

public:
	virtual void BakeTextureMatrixIntoTexgen(idPlane lightProject[3], const float *textureMatrix) override;
	virtual void BasicFog(const drawSurf_t *drawSurfs, const idPlane fogPlanes[4], const idRenderMatrix * inverseBaseLightProject) override;
	virtual void BindVariableStageImage(const textureStage_t *texture, const float *shaderRegisters) override;
	virtual void BlendLight(const drawSurf_t *drawSurfs, const viewLight_t * vLight) override;
	virtual void BlendLight(const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2, const viewLight_t * vLight) override;
	virtual void CopyRender(const void* data) override;
	virtual void DrawElementsWithCounters(const drawSurf_t* drawSurf) override;
	virtual void DrawInteractions() override;
	virtual int DrawShaderPasses(const drawSurf_t * const * const drawSurfs, const int numDrawSurfs, const float guiStereoScreenOffset, const int stereoEye) override;
	virtual void DrawSingleInteraction(drawInteraction_t * din) override;
	virtual void DrawView(const void* data, const int stereoEye) override;
	virtual void DrawViewInternal(const viewDef_t* viewDef, const int stereoEye) override;
	virtual void FinishStageTexturing(const shaderStage_t *pStage, const drawSurf_t *surf) override;
	virtual void FillDepthBufferFast(drawSurf_t **drawSurfs, int numDrawSurfs) override;
	virtual void FillDepthBufferGeneric(const drawSurf_t * const * drawSurfs, int numDrawSurfs) override;
	virtual void FogAllLights() override;
	virtual void FogPass(const drawSurf_t * drawSurfs, const drawSurf_t * drawSurfs2, const viewLight_t * vLight) override;
	virtual void GetShaderTextureMatrix(const float *shaderRegisters, const textureStage_t *texture, float matrix[16]) override;
	virtual void LoadShaderTextureMatrix(const float *shaderRegisters, const textureStage_t *texture) override;
	virtual void MotionBlur() override;
	virtual void PostProcess(const void* data) override;
	virtual void PrepareStageTexturing(const shaderStage_t * pStage, const drawSurf_t * surf) override;
	virtual void RenderInteractions(const drawSurf_t *surfList, const viewLight_t * vLight, int depthFunc, bool performStencilTest, bool useLightDepthBounds) override;
	virtual void SetMVP(const idRenderMatrix& mvp) override;
	virtual void SetupForFastPathInteractions(const idVec4 & diffuseColor, const idVec4 & specularColor) override;
	virtual void SetupInteractionStage(const shaderStage_t *surfaceStage, const float *surfaceRegs, const float lightColor[4], idVec4 matrix[2], float color[4]) override;
	virtual void StencilSelectLight(const viewLight_t * vLight) override;
	virtual void StencilShadowPass(const drawSurf_t *drawSurfs, const viewLight_t * vLight) override;
};

#endif