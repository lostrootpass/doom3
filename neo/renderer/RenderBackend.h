#ifndef ID_RENDER_BACKEND_H_
#define ID_RENDER_BACKEND_H_

class idRenderBackend
{
public:
	idRenderBackend() {};
	virtual ~idRenderBackend() {};

	virtual void BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float *textureMatrix ) = 0;
	virtual void BasicFog( const drawSurf_t *drawSurfs, const idPlane fogPlanes[4], const idRenderMatrix * inverseBaseLightProject ) = 0;
	virtual void BindVariableStageImage(const textureStage_t *texture, const float *shaderRegisters) = 0;
	virtual void BlendLight( const drawSurf_t *drawSurfs, const viewLight_t * vLight ) = 0;
	virtual void BlendLight( const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2, const viewLight_t * vLight ) = 0;
	virtual void CopyRender(const void* data) = 0;
	virtual void DrawElementsWithCounters(const drawSurf_t* drawSurf) = 0;
	virtual void DrawInteractions() = 0;
	virtual int DrawShaderPasses(const drawSurf_t * const * const drawSurfs, const int numDrawSurfs, const float guiStereoScreenOffset, const int stereoEye) = 0;
	virtual void DrawSingleInteraction( drawInteraction_t * din ) = 0;
	virtual void DrawView(const void* data, const int stereoEye) = 0;
	virtual void DrawViewInternal(const viewDef_t* viewDef, const int stereoEye) = 0;
	virtual void FinishStageTexturing(const shaderStage_t *pStage, const drawSurf_t *surf) = 0;

	/*
	=====================
	RB_FillDepthBufferFast

	Optimized fast path code.

	If there are subview surfaces, they must be guarded in the depth buffer to allow
	the mirror / subview to show through underneath the current view rendering.

	Surfaces with perforated shaders need the full shader setup done, but should be
	drawn after the opaque surfaces.

	The bulk of the surfaces should be simple opaque geometry that can be drawn very rapidly.

	If there are no subview surfaces, we could clear to black and use fast-Z rendering
	on the 360.
	=====================
	*/
	virtual void FillDepthBufferFast(drawSurf_t **drawSurfs, int numDrawSurfs) = 0;
	virtual void FillDepthBufferGeneric( const drawSurf_t * const * drawSurfs, int numDrawSurfs ) = 0;
	virtual void FogAllLights() = 0;
	virtual void FogPass( const drawSurf_t * drawSurfs,  const drawSurf_t * drawSurfs2, const viewLight_t * vLight ) = 0;
	virtual void GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, float matrix[16] ) = 0;
	virtual void LoadShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture ) = 0;	
	virtual void MotionBlur()  = 0;;
	virtual void PostProcess(const void* data) = 0; 
	virtual void PrepareStageTexturing(const shaderStage_t * pStage, const drawSurf_t * surf) = 0;
	virtual void RenderInteractions( const drawSurf_t *surfList, const viewLight_t * vLight, int depthFunc, bool performStencilTest, bool useLightDepthBounds ) = 0;
	virtual void SetMVP(const idRenderMatrix& mvp) = 0;
	virtual void SetupForFastPathInteractions( const idVec4 & diffuseColor, const idVec4 & specularColor ) = 0;
	virtual void SetupInteractionStage(const shaderStage_t *surfaceStage, const float *surfaceRegs, const float lightColor[4],
		idVec4 matrix[2], float color[4]) = 0;
	
	/*
	==================
	RB_StencilSelectLight
	
	Deform the zeroOneCubeModel to exactly cover the light volume. Render the deformed cube model to the stencil buffer in
	such a way that only fragments that are directly visible and contained within the volume will be written creating a 
	mask to be used by the following stencil shadow and draw interaction passes.
	==================
	*/
	virtual void StencilSelectLight( const viewLight_t * vLight ) = 0;

	/*
	=====================
	RB_StencilShadowPass
	
	The stencil buffer should have been set to 128 on any surfaces that might receive shadows.
	=====================
	*/
	virtual void StencilShadowPass( const drawSurf_t *drawSurfs, const viewLight_t * vLight ) = 0;
	
	ID_INLINE void SetVertexParm( renderParm_t rp, const float * value ) {
		renderProgManager->SetUniformValue( rp, value );
	}

	ID_INLINE void SetVertexParms( renderParm_t rp, const float * value, int num ) {
		for ( int i = 0; i < num; i++ ) {
			renderProgManager->SetUniformValue( (renderParm_t)( rp + i ), value + ( i * 4 ) );
		}
	}

	ID_INLINE void SetFragmentParm( renderParm_t rp, const float * value ) {
		renderProgManager->SetUniformValue( rp, value );
	}

	void SetMVPWithStereoOffset( const idRenderMatrix & mvp, const float stereoOffset ) { 
		idRenderMatrix offset = mvp;
		offset[0][3] += stereoOffset;

		SetVertexParms( RENDERPARM_MVPMATRIX_X, offset[0], 4 );
	}

	void SetVertexColorParms( stageVertexColor_t svc ) {
		static const float zero[4] = { 0, 0, 0, 0 };
		static const float one[4] = { 1, 1, 1, 1 };
		static const float negOne[4] = { -1, -1, -1, -1 };

		switch ( svc ) {
			case SVC_IGNORE:
				SetVertexParm( RENDERPARM_VERTEXCOLOR_MODULATE, zero );
				SetVertexParm( RENDERPARM_VERTEXCOLOR_ADD, one );
				break;
			case SVC_MODULATE:
				SetVertexParm( RENDERPARM_VERTEXCOLOR_MODULATE, one );
				SetVertexParm( RENDERPARM_VERTEXCOLOR_ADD, zero );
				break;
			case SVC_INVERSE_MODULATE:
				SetVertexParm( RENDERPARM_VERTEXCOLOR_MODULATE, negOne );
				SetVertexParm( RENDERPARM_VERTEXCOLOR_ADD, one );
				break;
		}
	}

	static const int INTERACTION_TEXUNIT_BUMP			= 0;
	static const int INTERACTION_TEXUNIT_FALLOFF		= 1;
	static const int INTERACTION_TEXUNIT_PROJECTION	= 2;
	static const int INTERACTION_TEXUNIT_DIFFUSE		= 3;
	static const int INTERACTION_TEXUNIT_SPECULAR		= 4;
};

#endif