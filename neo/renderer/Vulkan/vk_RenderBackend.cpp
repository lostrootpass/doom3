#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "../tr_local.h"
#include "../RenderParms.h"

#ifdef DOOM3_VULKAN

#include "vk_RenderBackend.h"

idRenderBackendVk::idRenderBackendVk()
{

}

idRenderBackendVk::~idRenderBackendVk()
{

}

void idRenderBackendVk::BindVariableStageImage(const textureStage_t *texture, const float *shaderRegisters)
{
	if ( texture->cinematic ) {
		cinData_t cin;

		if ( r_skipDynamicTextures.GetBool() ) {
			globalImages->defaultImage->Bind();
			return;
		}

		// offset time by shaderParm[7] (FIXME: make the time offset a parameter of the shader?)
		// We make no attempt to optimize for multiple identical cinematics being in view, or
		// for cinematics going at a lower framerate than the renderer.
		cin = texture->cinematic->ImageForTime( backEnd.viewDef->renderView.time[0] + idMath::Ftoi( 1000.0f * backEnd.viewDef->renderView.shaderParms[11] ) );
		if ( cin.imageY != NULL ) {
			GL_SelectTexture( 0 );
			cin.imageY->Bind();
			GL_SelectTexture( 1 );
			cin.imageCr->Bind();
			GL_SelectTexture( 2 );
			cin.imageCb->Bind();
		} else {
			globalImages->blackImage->Bind();
			// because the shaders may have already been set - we need to make sure we are not using a bink shader which would 
			// display incorrectly.  We may want to get rid of RB_BindVariableStageImage and inline the code so that the
			// SWF GUI case is handled better, too
			renderProgManager->BindShader_TextureVertexColor();
		}
	} else {
		// FIXME: see why image is invalid
		if ( texture->image != NULL ) {
			texture->image->Bind();
		}
	}
}

void idRenderBackendVk::CopyRender(const void* data)
{
	const copyRenderCommand_t * cmd = (const copyRenderCommand_t *)data;

	if ( r_skipCopyTexture.GetBool() ) {
		return;
	}

	RENDERLOG_PRINTF( "***************** RB_CopyRender *****************\n" );

	if ( cmd->image ) {
		cmd->image->CopyFramebuffer( cmd->x, cmd->y, cmd->imageWidth, cmd->imageHeight );
	}

	if ( cmd->clearColorAfterCopy ) {
		renderSystem->Clear( true, false, false, STENCIL_SHADOW_TEST_VALUE, 0, 0, 0, 0 );
	}
}

void idRenderBackendVk::DrawElementsWithCounters(const drawSurf_t* drawSurf)
{
	backEnd.glState.vertexLayout = LAYOUT_DRAW_VERT;
	BindAndSubmitDrawcall(drawSurf);
}

void idRenderBackendVk::BindAndSubmitDrawcall(const drawSurf_t* surf)
{
	VkCommandBuffer cmd = Vk_ActiveCommandBuffer();

	// get vertex buffer
	vertCacheHandle_t vbHandle = 0;
	if (backEnd.glState.vertexLayout == LAYOUT_DRAW_VERT)
		vbHandle = surf->ambientCache;
	else
		vbHandle = surf->shadowCache;

	idVertexBuffer * vertexBuffer;
	if ( vertexCache->CacheIsStatic( vbHandle ) ) {
		vertexBuffer = (idVertexBuffer*)vertexCache->staticData.vertexBuffer;
	} else {
		const uint64 frameNum = (int)( vbHandle >> VERTCACHE_FRAME_SHIFT ) & VERTCACHE_FRAME_MASK;
		if ( frameNum != ( ( vertexCache->currentFrame - 1 ) & VERTCACHE_FRAME_MASK ) ) {
			idLib::Warning( "RB_DrawElementsWithCounters, vertexBuffer == NULL" );
			return;
		}
		vertexBuffer = (idVertexBuffer*)vertexCache->frameData[vertexCache->drawListNum].vertexBuffer;
	}
	const VkDeviceSize vertOffset = (VkDeviceSize)( vbHandle >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;

	VkBuffer vkBuf = ((idVertexBufferVk*)vertexBuffer)->GetBuffer();

	//renderdoc buffer view vertex format:
	//float x; float y; float z; half s; half t; byte normal[4]; byte tangent[4]; byte color[4]; byte color2[4];
	vkCmdBindVertexBuffers(cmd, 0, 1, &vkBuf, &vertOffset);

	// get index buffer
	const vertCacheHandle_t ibHandle = surf->indexCache;
	idIndexBuffer * indexBuffer;
	if ( vertexCache->CacheIsStatic( ibHandle ) ) {
		indexBuffer = (idIndexBuffer*)vertexCache->staticData.indexBuffer;
	} else {
		const uint64 frameNum = (int)( ibHandle >> VERTCACHE_FRAME_SHIFT ) & VERTCACHE_FRAME_MASK;
		if ( frameNum != ( ( vertexCache->currentFrame - 1 ) & VERTCACHE_FRAME_MASK ) ) {
			idLib::Warning( "RB_DrawElementsWithCounters, indexBuffer == NULL" );
			return;
		}
		indexBuffer = (idIndexBuffer*)vertexCache->frameData[vertexCache->drawListNum].indexBuffer;
	}
	const int indexOffset = (int)( ibHandle >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;
	vkBuf = ((idIndexBufferVk*)indexBuffer)->GetBuffer();

	vkCmdBindIndexBuffer(cmd, vkBuf, (VkDeviceSize)indexOffset, VK_INDEX_TYPE_UINT16);

	RENDERLOG_PRINTF( "Binding Buffers: %p:%i %p:%i\n", vertexBuffer, vertOffset, indexBuffer, indexOffset );

	if ( surf->jointCache ) {
		if ( !verify( renderProgManager->ShaderUsesJoints() ) ) {
			return;
		}
	} else {
		if ( !verify( !renderProgManager->ShaderUsesJoints() || renderProgManager->ShaderHasOptionalSkinning() ) ) {
			return;
		}
	}

	idRenderProgManagerVk* rpm = (idRenderProgManagerVk*)renderProgManager;
	VkPipeline pipeline = rpm->GetPipelineForState(backEnd.glState.glStateBits);

	//Don't try to draw things we haven't ported shaders for yet.
	if (pipeline == VK_NULL_HANDLE)
		return;

	Vk_UsePipeline(pipeline);

	rpm->CommitUniforms();
	VkDescriptorSet sets[] = { 
		Vk_UniformDescriptorSet(),
		Vk_JointBufferSetForFrame(vertexCache->drawListNum)
	};


	uint32_t jointOffset = 0;
	if(surf->jointCache)
		jointOffset = (int)( surf->jointCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;
	
	const uint32_t dynamicOffsetCount = 3;
	const uint32_t offsets[] = {
		rpm->GetCurrentVertUniformOffset(),
		rpm->GetCurrentFragUniformOffset(),
		jointOffset
	};

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Vk_GetPipelineLayout(),
		0, 2, sets, dynamicOffsetCount, offsets);

	vkCmdDrawIndexed(cmd, r_singleTriangle.GetBool() ? 3 : surf->numIndexes,
		1, 0, 0, 0);
}

void idRenderBackendVk::DrawInteractions()
{
	if ( r_skipInteractions.GetBool() ) {
		return;
	}

	renderLog.OpenMainBlock( MRB_DRAW_INTERACTIONS );
	renderLog.OpenBlock( "RB_DrawInteractions" );

	GL_SelectTexture( 0 );


	const bool useLightDepthBounds = r_useLightDepthBounds.GetBool();

	//
	// for each light, perform shadowing and adding
	//
	for ( const viewLight_t * vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		// do fogging later
		if ( vLight->lightShader->IsFogLight() ) {
			continue;
		}
		if ( vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( vLight->localInteractions == NULL && vLight->globalInteractions == NULL && vLight->translucentInteractions == NULL ) {
			continue;
		}

		const idMaterial * lightShader = vLight->lightShader;
		renderLog.OpenBlock( lightShader->GetName() );

		// set the depth bounds for the whole light
		if ( useLightDepthBounds ) {
			renderSystem->SetDepthBoundsTest( vLight->scissorRect.zmin, vLight->scissorRect.zmax );
		}

		// only need to clear the stencil buffer and perform stencil testing if there are shadows
		const bool performStencilTest = ( vLight->globalShadows != NULL || vLight->localShadows != NULL );

		// mirror flips the sense of the stencil select, and I don't want to risk accidentally breaking it
		// in the normal case, so simply disable the stencil select in the mirror case
		const bool useLightStencilSelect = ( r_useLightStencilSelect.GetBool() && backEnd.viewDef->isMirror == false );

		if ( performStencilTest ) {
			if ( useLightStencilSelect ) {
				// write a stencil mask for the visible light bounds to hi-stencil
				StencilSelectLight( vLight );
			} else {
				// always clear whole S-Cull tiles
				idScreenRect rect;
				rect.x1 = ( vLight->scissorRect.x1 +  0 ) & ~15;
				rect.y1 = ( vLight->scissorRect.y1 +  0 ) & ~15;
				rect.x2 = ( vLight->scissorRect.x2 + 15 ) & ~15;
				rect.y2 = ( vLight->scissorRect.y2 + 15 ) & ~15;

				if ( !backEnd.currentScissor.Equals( rect ) && r_useScissor.GetBool() ) {
					renderSystem->SetScissor( backEnd.viewDef->viewport.x1 + rect.x1,
								backEnd.viewDef->viewport.y1 + rect.y1,
								rect.x2 + 1 - rect.x1,
								rect.y2 + 1 - rect.y1 );
					backEnd.currentScissor = rect;
				}
				renderSystem->SetState( GLS_DEFAULT );	// make sure stencil mask passes for the clear
				renderSystem->Clear( false, false, true, STENCIL_SHADOW_TEST_VALUE, 0.0f, 0.0f, 0.0f, 0.0f );
			}
		}

		if ( vLight->globalShadows != NULL ) {
			renderLog.OpenBlock( "Global Light Shadows" );
			StencilShadowPass( vLight->globalShadows, vLight );
			renderLog.CloseBlock();
		}

		if ( vLight->localInteractions != NULL ) {
			renderLog.OpenBlock( "Local Light Interactions" );
			RenderInteractions( vLight->localInteractions, vLight, GLS_DEPTHFUNC_EQUAL, performStencilTest, useLightDepthBounds );
			renderLog.CloseBlock();
		}

		if ( vLight->localShadows != NULL ) {
			renderLog.OpenBlock( "Local Light Shadows" );
			StencilShadowPass( vLight->localShadows, vLight );
			renderLog.CloseBlock();
		}

		if ( vLight->globalInteractions != NULL ) {
			renderLog.OpenBlock( "Global Light Interactions" );
			RenderInteractions( vLight->globalInteractions, vLight, GLS_DEPTHFUNC_EQUAL, performStencilTest, useLightDepthBounds );
			renderLog.CloseBlock();
		}


		if ( vLight->translucentInteractions != NULL && !r_skipTranslucent.GetBool() ) {
			renderLog.OpenBlock( "Translucent Interactions" );

			// Disable the depth bounds test because translucent surfaces don't work with
			// the depth bounds tests since they did not write depth during the depth pass.
			if ( useLightDepthBounds ) {
				renderSystem->SetDepthBoundsTest( 0.0f, 0.0f );
			}

			// The depth buffer wasn't filled in for translucent surfaces, so they
			// can never be constrained to perforated surfaces with the depthfunc equal.

			// Translucent surfaces do not receive shadows. This is a case where a
			// shadow buffer solution would work but stencil shadows do not because
			// stencil shadows only affect surfaces that contribute to the view depth
			// buffer and translucent surfaces do not contribute to the view depth buffer.

			RenderInteractions( vLight->translucentInteractions, vLight, GLS_DEPTHFUNC_LESS, false, false );

			renderLog.CloseBlock();
		}

		renderLog.CloseBlock();
	}

	// disable stencil shadow test
	renderSystem->SetState( GLS_DEFAULT );

	// unbind texture units
	for ( int i = 0; i < 5; i++ ) {
		GL_SelectTexture( i );
		globalImages->BindNull();
	}
	GL_SelectTexture( 0 );

	// reset depth bounds
	if ( useLightDepthBounds ) {
		renderSystem->SetDepthBoundsTest( 0.0f, 0.0f );
	}

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();
}

int idRenderBackendVk::DrawShaderPasses(const drawSurf_t * const * const drawSurfs, const int numDrawSurfs, const float guiStereoScreenOffset, const int stereoEye)
{
	// only obey skipAmbient if we are rendering a view
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}

	renderLog.OpenBlock( "RB_DrawShaderPasses" );

	GL_SelectTexture( 1 );
	globalImages->BindNull();

	GL_SelectTexture( 0 );

	backEnd.currentSpace = (const viewEntity_t *)1;	// using NULL makes /analyze think surf->space needs to be checked...
	float currentGuiStereoOffset = 0.0f;

	int i = 0;
	for ( ; i < numDrawSurfs; i++ ) {
		const drawSurf_t * surf = drawSurfs[i];
		const idMaterial * shader = surf->material;

		if ( !shader->HasAmbient() ) {
			continue;
		}

		if ( shader->IsPortalSky() ) {
			continue;
		}

		// some deforms may disable themselves by setting numIndexes = 0
		if ( surf->numIndexes == 0 ) {
			continue;
		}

		if ( shader->SuppressInSubview() ) {
			continue;
		}

		if ( backEnd.viewDef->isXraySubview && surf->space->entityDef ) {
			if ( surf->space->entityDef->parms.xrayIndex != 2 ) {
				continue;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if ( shader->GetSort() >= SS_POST_PROCESS && !backEnd.currentRenderCopied ) {
			break;
		}

		// if we are rendering a 3D view and the surface's eye index doesn't match 
		// the current view's eye index then we skip the surface
		// if the stereoEye value of a surface is 0 then we need to draw it for both eyes.
		const int shaderStereoEye = shader->GetStereoEye();
		const bool isEyeValid = stereoRender_swapEyes.GetBool() ? ( shaderStereoEye == stereoEye ) : ( shaderStereoEye != stereoEye );
		if ( ( stereoEye != 0 ) && ( shaderStereoEye != 0 ) && ( isEyeValid ) ) {
			continue;
		}

		renderLog.OpenBlock( shader->GetName() );

		// determine the stereoDepth offset 
		// guiStereoScreenOffset will always be zero for 3D views, so the !=
		// check will never force an update due to the current sort value.
		const float thisGuiStereoOffset = guiStereoScreenOffset * surf->sort;

		// change the matrix and other space related vars if needed
		if ( surf->space != backEnd.currentSpace || thisGuiStereoOffset != currentGuiStereoOffset ) {
			backEnd.currentSpace = surf->space;
			currentGuiStereoOffset = thisGuiStereoOffset;

			const viewEntity_t *space = backEnd.currentSpace;

			if ( guiStereoScreenOffset != 0.0f ) {
				SetMVPWithStereoOffset( space->mvp, currentGuiStereoOffset );
			} else {
				SetMVP(space->mvp);
			}

			// set eye position in local space
			idVec4 localViewOrigin( 1.0f );
			R_GlobalPointToLocal( space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );
			SetVertexParm( RENDERPARM_LOCALVIEWORIGIN, localViewOrigin.ToFloatPtr() );

			// set model Matrix
			float modelMatrixTranspose[16];
			R_MatrixTranspose( space->modelMatrix, modelMatrixTranspose );
			SetVertexParms( RENDERPARM_MODELMATRIX_X, modelMatrixTranspose, 4 );

			// Set ModelView Matrix
			float modelViewMatrixTranspose[16];
			R_MatrixTranspose( space->modelViewMatrix, modelViewMatrixTranspose );
			SetVertexParms( RENDERPARM_MODELVIEWMATRIX_X, modelViewMatrixTranspose, 4 );			
		}

		// change the scissor if needed
		if (!backEnd.currentScissor.Equals( surf->scissorRect ) && r_useScissor.GetBool() ) {
			renderSystem->SetScissor( backEnd.viewDef->viewport.x1 + surf->scissorRect.x1, 
						backEnd.viewDef->viewport.y1 + surf->scissorRect.y1,
						surf->scissorRect.x2 + 1 - surf->scissorRect.x1,
						surf->scissorRect.y2 + 1 - surf->scissorRect.y1 );
			backEnd.currentScissor = surf->scissorRect;
		}

		// get the expressions for conditionals / color / texcoords
		const float	*regs = surf->shaderRegisters;

		// set face culling appropriately
		if ( surf->space->isGuiSurface ) {
			renderSystem->SetCull( CT_TWO_SIDED );
		} else {
			renderSystem->SetCull( shader->GetCullType() );
		}

		uint64 surfGLState = surf->extraGLState;

		// set polygon offset if necessary
		if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
			renderSystem->SetPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
			surfGLState = GLS_POLYGON_OFFSET;
		}

		for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {		
			const shaderStage_t *pStage = shader->GetStage(stage);

			// check the enable condition
			if ( regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}

			// skip the stages involved in lighting
			if ( pStage->lighting != SL_AMBIENT ) {
				continue;
			}

			uint64 stageGLState = surfGLState;
			if ( ( surfGLState & GLS_OVERRIDE ) == 0 ) {
				stageGLState |= pStage->drawStateBits;
			}

			// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
			if ( ( stageGLState & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
				continue;
			}

			// see if we are a new-style stage
			newShaderStage_t *newStage = pStage->newStage;
			if ( newStage != NULL ) {
				//--------------------------
				//
				// new style stages
				//
				//--------------------------
				if ( r_skipNewAmbient.GetBool() ) {
					continue;
				}
				renderLog.OpenBlock( "New Shader Stage" );

				renderSystem->SetState( stageGLState );
			
				renderProgManager->BindShader( newStage->glslProgram, newStage->glslProgram );

				for ( int j = 0; j < newStage->numVertexParms; j++ ) {
					float parm[4];
					parm[0] = regs[ newStage->vertexParms[j][0] ];
					parm[1] = regs[ newStage->vertexParms[j][1] ];
					parm[2] = regs[ newStage->vertexParms[j][2] ];
					parm[3] = regs[ newStage->vertexParms[j][3] ];
					SetVertexParm( (renderParm_t)( RENDERPARM_USER + j ), parm );
				}

				// set rpEnableSkinning if the shader has optional support for skinning
				if ( surf->jointCache && renderProgManager->ShaderHasOptionalSkinning() ) {
					const idVec4 skinningParm( 1.0f );
					SetVertexParm( RENDERPARM_ENABLE_SKINNING, skinningParm.ToFloatPtr() );
				}

				// bind texture units
				for ( int j = 0; j < newStage->numFragmentProgramImages; j++ ) {
					idImage * image = newStage->fragmentProgramImages[j];
					if ( image != NULL ) {
						GL_SelectTexture( j );
						image->Bind();
					}
				}
			
				// draw it
				DrawElementsWithCounters( surf );

				// unbind texture units
				for ( int j = 0; j < newStage->numFragmentProgramImages; j++ ) {
					idImage * image = newStage->fragmentProgramImages[j];
					if ( image != NULL ) {
						GL_SelectTexture( j );
						globalImages->BindNull();
					}
				}

				// clear rpEnableSkinning if it was set
				if ( surf->jointCache && renderProgManager->ShaderHasOptionalSkinning() ) {
					const idVec4 skinningParm( 0.0f );
					SetVertexParm( RENDERPARM_ENABLE_SKINNING, skinningParm.ToFloatPtr() );
				}

				GL_SelectTexture( 0 );
				renderProgManager->Unbind();

				renderLog.CloseBlock();
				continue;
			}

			//--------------------------
			//
			// old style stages
			//
			//--------------------------

			// set the color
			float color[4];
			color[0] = regs[ pStage->color.registers[0] ];
			color[1] = regs[ pStage->color.registers[1] ];
			color[2] = regs[ pStage->color.registers[2] ];
			color[3] = regs[ pStage->color.registers[3] ];

			// skip the entire stage if an add would be black
			if ( ( stageGLState & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) 
				&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
				continue;
			}

			// skip the entire stage if a blend would be completely transparent
			if ( ( stageGLState & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
				&& color[3] <= 0 ) {
				continue;
			}

			stageVertexColor_t svc = pStage->vertexColor;

			renderLog.OpenBlock( "Old Shader Stage" );
			GL_Color( color );

			if ( surf->space->isGuiSurface ) {
				// Force gui surfaces to always be SVC_MODULATE
				svc = SVC_MODULATE;

				// use special shaders for bink cinematics
				if ( pStage->texture.cinematic ) {
					if ( ( stageGLState & GLS_OVERRIDE ) != 0 ) {
						// This is a hack... Only SWF Guis set GLS_OVERRIDE
						// Old style guis do not, and we don't want them to use the new GUI renederProg
						renderProgManager->BindShader_BinkGUI();
					} else {
						renderProgManager->BindShader_Bink();
					}
				} else {
					if ( ( stageGLState & GLS_OVERRIDE ) != 0 ) {
						// This is a hack... Only SWF Guis set GLS_OVERRIDE
						// Old style guis do not, and we don't want them to use the new GUI renderProg
						renderProgManager->BindShader_GUI();
					} else {
						if ( surf->jointCache ) {
							renderProgManager->BindShader_TextureVertexColorSkinned();
						} else {
							renderProgManager->BindShader_TextureVertexColor();
						}
					}
				}
			} else if ( ( pStage->texture.texgen == TG_SCREEN ) || ( pStage->texture.texgen == TG_SCREEN2 ) ) {
				renderProgManager->BindShader_TextureTexGenVertexColor();
			} else if ( pStage->texture.cinematic ) {
				renderProgManager->BindShader_Bink();
			} else {
				if ( surf->jointCache ) {
					renderProgManager->BindShader_TextureVertexColorSkinned();
				} else {
					renderProgManager->BindShader_TextureVertexColor();
				}
			}
		
			SetVertexColorParms( svc );

			// bind the texture
			BindVariableStageImage( &pStage->texture, regs );

			// set privatePolygonOffset if necessary
			if ( pStage->privatePolygonOffset ) {
				renderSystem->SetPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
				stageGLState |= GLS_POLYGON_OFFSET;
			}

			// set the state
			renderSystem->SetState( stageGLState );

			PrepareStageTexturing( pStage, surf );

			// draw it
			DrawElementsWithCounters( surf );

			FinishStageTexturing( pStage, surf );

			// unset privatePolygonOffset if necessary
			if ( pStage->privatePolygonOffset ) {
				renderSystem->SetPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
			}
			renderLog.CloseBlock();
		}

		renderLog.CloseBlock();
	}

	renderSystem->SetCull( CT_FRONT_SIDED );
	GL_Color( 1.0f, 1.0f, 1.0f );

	renderLog.CloseBlock();
	return i;
}

void idRenderBackendVk::DrawView(const void* data, const int stereoEye)
{
	const drawSurfsCommand_t * cmd = (const drawSurfsCommand_t *)data;

	backEnd.viewDef = cmd->viewDef;

	// we will need to do a new copyTexSubImage of the screen
	// when a SS_POST_PROCESS material is used
	backEnd.currentRenderCopied = false;

	// if there aren't any drawsurfs, do nothing
	if ( !backEnd.viewDef->numDrawSurfs ) {
		return;
	}

	// skip render bypasses everything that has models, assuming
	// them to be 3D views, but leaves 2D rendering visible
	if ( r_skipRender.GetBool() && backEnd.viewDef->viewEntitys ) {
		return;
	}

	// skip render context sets the wgl context to NULL,
	// which should factor out the API cost, under the assumption
	// that all gl calls just return if the context isn't valid
	if ( r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys ) {
		//GLimp_DeactivateContext();
	}

	backEnd.pc.c_surfaces += backEnd.viewDef->numDrawSurfs;

	RB_ShowOverdraw();

	// render the scene
	DrawViewInternal( cmd->viewDef, stereoEye );

	MotionBlur();

	// restore the context for 2D drawing if we were stubbing it out
	if ( r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys ) {
		//GLimp_ActivateContext();
		renderSystem->SetDefaultState();
	}

	// optionally draw a box colored based on the eye number
	if ( r_drawEyeColor.GetBool() ) {
		const idScreenRect & r = backEnd.viewDef->viewport;
		renderSystem->SetScissor( ( r.x1 + r.x2 ) / 2, ( r.y1 + r.y2 ) / 2, 32, 32 );
		switch ( stereoEye ) {
			case -1:
				renderSystem->Clear( true, false, false, 0, 1.0f, 0.0f, 0.0f, 1.0f );
				break;
			case 1:
				renderSystem->Clear( true, false, false, 0, 0.0f, 1.0f, 0.0f, 1.0f );
				break;
			default:
				renderSystem->Clear( true, false, false, 0, 0.5f, 0.5f, 0.5f, 1.0f );
				break;
		}
	}
}

void idRenderBackendVk::DrawViewInternal(const viewDef_t* viewDef, const int stereoEye)
{
	renderLog.OpenBlock( "RB_DrawViewInternal" );

	//-------------------------------------------------
	// guis can wind up referencing purged images that need to be loaded.
	// this used to be in the gui emit code, but now that it can be running
	// in a separate thread, it must not try to load images, so do it here.
	//-------------------------------------------------
	drawSurf_t **drawSurfs = (drawSurf_t **)&viewDef->drawSurfs[0];
	const int numDrawSurfs = viewDef->numDrawSurfs;

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t * ds = viewDef->drawSurfs[ i ];
		if ( ds->material != NULL ) {
			const_cast<idMaterial *>( ds->material )->EnsureNotPurged();
		}
	}

	//-------------------------------------------------
	// RB_BeginDrawingView
	//
	// Any mirrored or portaled views have already been drawn, so prepare
	// to actually render the visible surfaces for this view
	//
	// clear the z buffer, set the projection matrix, etc
	//-------------------------------------------------

	// set the window clipping
	renderSystem->SetViewport( viewDef->viewport.x1,
		viewDef->viewport.y1,
		viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
		viewDef->viewport.y2 + 1 - viewDef->viewport.y1 );

	// the scissor may be smaller than the viewport for subviews
	renderSystem->SetScissor( backEnd.viewDef->viewport.x1 + viewDef->scissor.x1,
				backEnd.viewDef->viewport.y1 + viewDef->scissor.y1,
				viewDef->scissor.x2 + 1 - viewDef->scissor.x1,
				viewDef->scissor.y2 + 1 - viewDef->scissor.y1 );
	backEnd.currentScissor = viewDef->scissor;

	backEnd.glState.faceCulling = -1;		// force face culling to set next time

	// ensures that depth writes are enabled for the depth clear
	renderSystem->SetState( GLS_DEFAULT );


	// Clear the depth buffer and clear the stencil to 128 for stencil shadows as well as gui masking
	renderSystem->Clear( false, true, true, STENCIL_SHADOW_TEST_VALUE, 0.0f, 0.0f, 0.0f, 0.0f );

	// normal face culling
	renderSystem->SetCull( CT_FRONT_SIDED );

	//------------------------------------
	// sets variables that can be used by all programs
	//------------------------------------
	{
		//
		// set eye position in global space
		//
		float parm[4];
		parm[0] = backEnd.viewDef->renderView.vieworg[0];
		parm[1] = backEnd.viewDef->renderView.vieworg[1];
		parm[2] = backEnd.viewDef->renderView.vieworg[2];
		parm[3] = 1.0f;

		SetVertexParm( RENDERPARM_GLOBALEYEPOS, parm ); // rpGlobalEyePos

		// sets overbright to make world brighter
		// This value is baked into the specularScale and diffuseScale values so
		// the interaction programs don't need to perform the extra multiply,
		// but any other renderprogs that want to obey the brightness value
		// can reference this.
		float overbright = r_lightScale.GetFloat() * 0.5f;
		parm[0] = overbright;
		parm[1] = overbright;
		parm[2] = overbright;
		parm[3] = overbright;
		SetFragmentParm( RENDERPARM_OVERBRIGHT, parm );

		// Set Projection Matrix
		float projMatrixTranspose[16];
		R_MatrixTranspose( backEnd.viewDef->projectionMatrix, projMatrixTranspose );
		SetVertexParms( RENDERPARM_PROJMATRIX_X, projMatrixTranspose, 4 );
	}

	//-------------------------------------------------
	// fill the depth buffer and clear color buffer to black except on subviews
	//-------------------------------------------------
	FillDepthBufferFast( drawSurfs, numDrawSurfs );

	//-------------------------------------------------
	// main light renderer
	//-------------------------------------------------
	DrawInteractions();

	//-------------------------------------------------
	// now draw any non-light dependent shading passes
	//-------------------------------------------------
	int processed = 0;
	if ( !r_skipShaderPasses.GetBool() ) {
		renderLog.OpenMainBlock( MRB_DRAW_SHADER_PASSES );
		float guiScreenOffset;
		if ( viewDef->viewEntitys != NULL ) {
			// guiScreenOffset will be 0 in non-gui views
			guiScreenOffset = 0.0f;
		} else {
			guiScreenOffset = stereoEye * viewDef->renderView.stereoScreenSeparation;
		}
		processed = DrawShaderPasses( drawSurfs, numDrawSurfs, guiScreenOffset, stereoEye );
		renderLog.CloseMainBlock();
	}

	//-------------------------------------------------
	// fog and blend lights, drawn after emissive surfaces
	// so they are properly dimmed down
	//-------------------------------------------------
	FogAllLights();

	//-------------------------------------------------
	// capture the depth for the motion blur before rendering any post process surfaces that may contribute to the depth
	//-------------------------------------------------
	if ( r_motionBlur.GetInteger() > 0 ) {
		const idScreenRect & viewport = backEnd.viewDef->viewport;
		globalImages->currentDepthImage->CopyDepthbuffer( viewport.x1, viewport.y1, viewport.GetWidth(), viewport.GetHeight() );
	}

	//-------------------------------------------------
	// now draw any screen warping post-process effects using _currentRender
	//-------------------------------------------------
	if ( processed < numDrawSurfs && !r_skipPostProcess.GetBool() ) {
		int x = backEnd.viewDef->viewport.x1;
		int y = backEnd.viewDef->viewport.y1;
		int	w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
		int	h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

		RENDERLOG_PRINTF( "Resolve to %i x %i buffer\n", w, h );

		GL_SelectTexture( 0 );
	
		// resolve the screen
		globalImages->currentRenderImage->CopyFramebuffer( x, y, w, h );
		backEnd.currentRenderCopied = true;

		// RENDERPARM_SCREENCORRECTIONFACTOR amd RENDERPARM_WINDOWCOORD overlap
		// diffuseScale and specularScale

		// screen power of two correction factor (no longer relevant now)
		float screenCorrectionParm[4];
		screenCorrectionParm[0] = 1.0f;
		screenCorrectionParm[1] = 1.0f;
		screenCorrectionParm[2] = 0.0f;
		screenCorrectionParm[3] = 1.0f;
		SetFragmentParm( RENDERPARM_SCREENCORRECTIONFACTOR, screenCorrectionParm ); // rpScreenCorrectionFactor

		// window coord to 0.0 to 1.0 conversion
		float windowCoordParm[4];
		windowCoordParm[0] = 1.0f / w;
		windowCoordParm[1] = 1.0f / h;
		windowCoordParm[2] = 0.0f;
		windowCoordParm[3] = 1.0f;
		SetFragmentParm( RENDERPARM_WINDOWCOORD, windowCoordParm ); // rpWindowCoord

		// render the remaining surfaces
		renderLog.OpenMainBlock( MRB_DRAW_SHADER_PASSES_POST );
		DrawShaderPasses( drawSurfs + processed, numDrawSurfs - processed, 0.0f /* definitely not a gui */, stereoEye );
		renderLog.CloseMainBlock();
	}

	//-------------------------------------------------
	// render debug tools
	//-------------------------------------------------
	//RB_RenderDebugTools( drawSurfs, numDrawSurfs );

	renderLog.CloseBlock();
}


void idRenderBackendVk::FillDepthBufferFast(drawSurf_t **drawSurfs, int numDrawSurfs)
{
	if ( numDrawSurfs == 0 ) {
		return;
	}

	// if we are just doing 2D rendering, no need to fill the depth buffer
	if ( backEnd.viewDef->viewEntitys == NULL ) {
		return;
	}

	renderLog.OpenMainBlock( MRB_FILL_DEPTH_BUFFER );
	renderLog.OpenBlock( "RB_FillDepthBufferFast" );

	renderSystem->StartDepthPass( backEnd.viewDef->scissor );

	// force MVP change on first surface
	backEnd.currentSpace = NULL;

	// draw all the subview surfaces, which will already be at the start of the sorted list,
	// with the general purpose path
	renderSystem->SetState( GLS_DEFAULT );

	int	surfNum;
	for ( surfNum = 0; surfNum < numDrawSurfs; surfNum++ ) {
		if ( drawSurfs[surfNum]->material->GetSort() != SS_SUBVIEW ) {
			break;
		}
		FillDepthBufferGeneric( &drawSurfs[surfNum], 1 );
	}

	const drawSurf_t ** perforatedSurfaces = (const drawSurf_t ** )_alloca( numDrawSurfs * sizeof( drawSurf_t * ) );
	int numPerforatedSurfaces = 0;

	// draw all the opaque surfaces and build up a list of perforated surfaces that
	// we will defer drawing until all opaque surfaces are done
	renderSystem->SetState( GLS_DEFAULT );

	// continue checking past the subview surfaces
	for ( ; surfNum < numDrawSurfs; surfNum++ ) {
		const drawSurf_t * surf = drawSurfs[ surfNum ];
		const idMaterial * shader = surf->material;

		// translucent surfaces don't put anything in the depth buffer
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			continue;
		}
		if ( shader->Coverage() == MC_PERFORATED ) {
			// save for later drawing
			perforatedSurfaces[ numPerforatedSurfaces ] = surf;
			numPerforatedSurfaces++;
			continue;
		}

		// set polygon offset?

		// set mvp matrix
		if ( surf->space != backEnd.currentSpace ) {
			SetMVP( surf->space->mvp );
			backEnd.currentSpace = surf->space;
		}

		renderLog.OpenBlock( shader->GetName() );

		if ( surf->jointCache ) {
			renderProgManager->BindShader_DepthSkinned();
		} else {
			renderProgManager->BindShader_Depth();
		}

		// must render with less-equal for Z-Cull to work properly
		assert( ( GL_GetCurrentState() & GLS_DEPTHFUNC_BITS ) == GLS_DEPTHFUNC_LESS );

		// draw it solid
		DrawElementsWithCounters( surf );

		renderLog.CloseBlock();
	}

	// draw all perforated surfaces with the general code path
	if ( numPerforatedSurfaces > 0 ) {
		FillDepthBufferGeneric( perforatedSurfaces, numPerforatedSurfaces );
	}

	// Allow platform specific data to be collected after the depth pass.
	renderSystem->FinishDepthPass();

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();
}

void idRenderBackendVk::FinishStageTexturing(const shaderStage_t *pStage, const drawSurf_t *surf)
{
	if ( pStage->texture.cinematic ) {
		// unbind the extra bink textures
		GL_SelectTexture( 1 );
		globalImages->BindNull();
		GL_SelectTexture( 2 );
		globalImages->BindNull();
		GL_SelectTexture( 0 );
	}

	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
		// see if there is also a bump map specified
		const shaderStage_t *bumpStage = surf->material->GetBumpStage();
		if ( bumpStage != NULL ) {
			// per-pixel reflection mapping with bump mapping
			GL_SelectTexture( 1 );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
		} else {
			// per-pixel reflection mapping without bump mapping
		}
		renderProgManager->Unbind();
	}
}

void idRenderBackendVk::FogAllLights()
{
	if ( r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0 
		 || backEnd.viewDef->isXraySubview /* don't fog in xray mode*/ ) {
		return;
	}
	renderLog.OpenMainBlock( MRB_FOG_ALL_LIGHTS );
	renderLog.OpenBlock( "RB_FogAllLights" );

	// force fog plane to recalculate
	backEnd.currentSpace = NULL;

	for ( viewLight_t * vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		if ( vLight->lightShader->IsFogLight() ) {
			FogPass( vLight->globalInteractions, vLight->localInteractions, vLight );
		} else if ( vLight->lightShader->IsBlendLight() ) {
			BlendLight( vLight->globalInteractions, vLight->localInteractions, vLight );
		}
	}

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();
}

extern idCVar rs_enable;
void idRenderBackendVk::PostProcess(const void* data)
{
	// only do the post process step if resolution scaling is enabled. Prevents the unnecessary copying of the framebuffer and
	// corresponding full screen quad pass.
	if ( rs_enable.GetInteger() == 0 ) { 
		return;
	}

	// resolve the scaled rendering to a temporary texture
	postProcessCommand_t * cmd = (postProcessCommand_t *)data;
	const idScreenRect & viewport = cmd->viewDef->viewport;
	globalImages->currentRenderImage->CopyFramebuffer( viewport.x1, viewport.y1, viewport.GetWidth(), viewport.GetHeight() );

	renderSystem->SetState( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS );
	renderSystem->SetCull( CT_TWO_SIDED );

	int screenWidth = renderSystem->GetWidth();
	int screenHeight = renderSystem->GetHeight();

	// set the window clipping
	renderSystem->SetViewport( 0, 0, screenWidth, screenHeight );
	renderSystem->SetScissor( 0, 0, screenWidth, screenHeight );

	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();
	renderProgManager->BindShader_PostProcess();

	// Draw
	DrawElementsWithCounters( &backEnd.unitSquareSurface );

	renderLog.CloseBlock();
}

void idRenderBackendVk::PrepareStageTexturing(const shaderStage_t * pStage, const drawSurf_t * surf)
{
	float useTexGenParm[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// set the texture matrix if needed
	LoadShaderTextureMatrix( surf->shaderRegisters, &pStage->texture );

	// texgens
	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {

		// see if there is also a bump map specified
		const shaderStage_t *bumpStage = surf->material->GetBumpStage();
		if ( bumpStage != NULL ) {
			// per-pixel reflection mapping with bump mapping
			GL_SelectTexture( 1 );
			bumpStage->texture.image->Bind();
			GL_SelectTexture( 0 );

			RENDERLOG_PRINTF( "TexGen: TG_REFLECT_CUBE: Bumpy Environment\n" );
			if ( surf->jointCache ) {
				renderProgManager->BindShader_BumpyEnvironmentSkinned();
			} else {
				renderProgManager->BindShader_BumpyEnvironment();
			}
		} else {
			RENDERLOG_PRINTF( "TexGen: TG_REFLECT_CUBE: Environment\n" );
			if ( surf->jointCache ) {
				renderProgManager->BindShader_EnvironmentSkinned();
			} else {
				renderProgManager->BindShader_Environment();
			}
		}

	} else if ( pStage->texture.texgen == TG_SKYBOX_CUBE ) {

		renderProgManager->BindShader_SkyBox();

	} else if ( pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {

		const int * parms = surf->material->GetTexGenRegisters();

		float wobbleDegrees = surf->shaderRegisters[ parms[0] ] * ( idMath::PI / 180.0f );
		float wobbleSpeed = surf->shaderRegisters[ parms[1] ] * ( 2.0f * idMath::PI / 60.0f );
		float rotateSpeed = surf->shaderRegisters[ parms[2] ] * ( 2.0f * idMath::PI / 60.0f );

		idVec3 axis[3];
		{
			// very ad-hoc "wobble" transform
			float s, c;
			idMath::SinCos( wobbleSpeed * backEnd.viewDef->renderView.time[0] * 0.001f, s, c );

			float ws, wc;
			idMath::SinCos( wobbleDegrees, ws, wc );

			axis[2][0] = ws * c;
			axis[2][1] = ws * s;
			axis[2][2] = wc;

			axis[1][0] = -s * s * ws;
			axis[1][2] = -s * ws * ws;
			axis[1][1] = idMath::Sqrt( idMath::Fabs( 1.0f - ( axis[1][0] * axis[1][0] + axis[1][2] * axis[1][2] ) ) );

			// make the second vector exactly perpendicular to the first
			axis[1] -= ( axis[2] * axis[1] ) * axis[2];
			axis[1].Normalize();

			// construct the third with a cross
			axis[0].Cross( axis[1], axis[2] );
		}

		// add the rotate
		float rs, rc;
		idMath::SinCos( rotateSpeed * backEnd.viewDef->renderView.time[0] * 0.001f, rs, rc );

		float transform[12];
		transform[0*4+0] = axis[0][0] * rc + axis[1][0] * rs;
		transform[0*4+1] = axis[0][1] * rc + axis[1][1] * rs;
		transform[0*4+2] = axis[0][2] * rc + axis[1][2] * rs;
		transform[0*4+3] = 0.0f;

		transform[1*4+0] = axis[1][0] * rc - axis[0][0] * rs;
		transform[1*4+1] = axis[1][1] * rc - axis[0][1] * rs;
		transform[1*4+2] = axis[1][2] * rc - axis[0][2] * rs;
		transform[1*4+3] = 0.0f;

		transform[2*4+0] = axis[2][0];
		transform[2*4+1] = axis[2][1];
		transform[2*4+2] = axis[2][2];
		transform[2*4+3] = 0.0f;

		SetVertexParms( RENDERPARM_WOBBLESKY_X, transform, 3 );
		renderProgManager->BindShader_WobbleSky();

	} else if ( ( pStage->texture.texgen == TG_SCREEN ) || ( pStage->texture.texgen == TG_SCREEN2 ) ) {

		useTexGenParm[0] = 1.0f;
		useTexGenParm[1] = 1.0f;
		useTexGenParm[2] = 1.0f;
		useTexGenParm[3] = 1.0f;

		float mat[16];
		R_MatrixMultiply( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

		RENDERLOG_PRINTF( "TexGen : %s\n", ( pStage->texture.texgen == TG_SCREEN ) ? "TG_SCREEN" : "TG_SCREEN2" );
		renderLog.Indent();

		float plane[4];
		plane[0] = mat[0*4+0];
		plane[1] = mat[1*4+0];
		plane[2] = mat[2*4+0];
		plane[3] = mat[3*4+0];
		SetVertexParm( RENDERPARM_TEXGEN_0_S, plane );
		RENDERLOG_PRINTF( "TEXGEN_S = %4.3f, %4.3f, %4.3f, %4.3f\n",  plane[0], plane[1], plane[2], plane[3] );

		plane[0] = mat[0*4+1];
		plane[1] = mat[1*4+1];
		plane[2] = mat[2*4+1];
		plane[3] = mat[3*4+1];
		SetVertexParm( RENDERPARM_TEXGEN_0_T, plane );
		RENDERLOG_PRINTF( "TEXGEN_T = %4.3f, %4.3f, %4.3f, %4.3f\n",  plane[0], plane[1], plane[2], plane[3] );

		plane[0] = mat[0*4+3];
		plane[1] = mat[1*4+3];
		plane[2] = mat[2*4+3];
		plane[3] = mat[3*4+3];
		SetVertexParm( RENDERPARM_TEXGEN_0_Q, plane );	
		RENDERLOG_PRINTF( "TEXGEN_Q = %4.3f, %4.3f, %4.3f, %4.3f\n",  plane[0], plane[1], plane[2], plane[3] );

		renderLog.Outdent();

	} else if ( pStage->texture.texgen == TG_DIFFUSE_CUBE ) {

		// As far as I can tell, this is never used
		idLib::Warning( "Using Diffuse Cube! Please contact Brian!" );

	} else if ( pStage->texture.texgen == TG_GLASSWARP ) {

		// As far as I can tell, this is never used
		idLib::Warning( "Using GlassWarp! Please contact Brian!" );
	}

	SetVertexParm( RENDERPARM_TEXGEN_0_ENABLED, useTexGenParm );
}

void idRenderBackendVk::SetMVP(const idRenderMatrix& mvp)
{
	//GL and Vulkan coordinate systems don't precisely match up
	//So we need a correction matrix here to invert Y and half Z
	idRenderMatrix m;
	m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
	m[1][0] = 0.0f; m[1][1] = -1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
	m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 0.5f; m[2][3] = 0.5f;
	m[3][0] = 0.0f; m[3][1] = 0.0f; m[3][2] = 0.0f; m[3][3] = 1.0f;
	
	idRenderMatrix corrected;
	idRenderMatrix::Multiply(m, mvp, corrected);
	SetVertexParms( RENDERPARM_MVPMATRIX_X, corrected[0], 4 );
}

void idRenderBackendVk::BakeTextureMatrixIntoTexgen(idPlane lightProject[3], const float *textureMatrix)
{
	float genMatrix[16];
	float final[16];

	genMatrix[0*4+0] = lightProject[0][0];
	genMatrix[1*4+0] = lightProject[0][1];
	genMatrix[2*4+0] = lightProject[0][2];
	genMatrix[3*4+0] = lightProject[0][3];

	genMatrix[0*4+1] = lightProject[1][0];
	genMatrix[1*4+1] = lightProject[1][1];
	genMatrix[2*4+1] = lightProject[1][2];
	genMatrix[3*4+1] = lightProject[1][3];

	genMatrix[0*4+2] = 0.0f;
	genMatrix[1*4+2] = 0.0f;
	genMatrix[2*4+2] = 0.0f;
	genMatrix[3*4+2] = 0.0f;

	genMatrix[0*4+3] = lightProject[2][0];
	genMatrix[1*4+3] = lightProject[2][1];
	genMatrix[2*4+3] = lightProject[2][2];
	genMatrix[3*4+3] = lightProject[2][3];

	R_MatrixMultiply( genMatrix, textureMatrix, final );

	lightProject[0][0] = final[0*4+0];
	lightProject[0][1] = final[1*4+0];
	lightProject[0][2] = final[2*4+0];
	lightProject[0][3] = final[3*4+0];

	lightProject[1][0] = final[0*4+1];
	lightProject[1][1] = final[1*4+1];
	lightProject[1][2] = final[2*4+1];
	lightProject[1][3] = final[3*4+1];
}

void idRenderBackendVk::BasicFog(const drawSurf_t *drawSurfs, const idPlane fogPlanes[4], const idRenderMatrix * inverseBaseLightProject)
{
	backEnd.currentSpace = NULL;

	for ( const drawSurf_t * drawSurf = drawSurfs; drawSurf != NULL; drawSurf = drawSurf->nextOnLight ) {
		if ( drawSurf->scissorRect.IsEmpty() ) {
			continue;	// !@# FIXME: find out why this is sometimes being hit!
						// temporarily jump over the scissor and draw so the gl error callback doesn't get hit
		}

		if ( !backEnd.currentScissor.Equals( drawSurf->scissorRect ) && r_useScissor.GetBool() ) {
			// change the scissor
			renderSystem->SetScissor( backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1,
						backEnd.viewDef->viewport.y1 + drawSurf->scissorRect.y1,
						drawSurf->scissorRect.x2 + 1 - drawSurf->scissorRect.x1,
						drawSurf->scissorRect.y2 + 1 - drawSurf->scissorRect.y1 );
			backEnd.currentScissor = drawSurf->scissorRect;
		}

		if ( drawSurf->space != backEnd.currentSpace ) {
			idPlane localFogPlanes[4];
			if ( inverseBaseLightProject == NULL ) {
				SetMVP( drawSurf->space->mvp );
				for ( int i = 0; i < 4; i++ ) {
					R_GlobalPlaneToLocal( drawSurf->space->modelMatrix, fogPlanes[i], localFogPlanes[i] );
				}
			} else {
				idRenderMatrix invProjectMVPMatrix;
				idRenderMatrix::Multiply( backEnd.viewDef->worldSpace.mvp, *inverseBaseLightProject, invProjectMVPMatrix );
				SetMVP( invProjectMVPMatrix );
				for ( int i = 0; i < 4; i++ ) {
					inverseBaseLightProject->InverseTransformPlane( fogPlanes[i], localFogPlanes[i], false );
				}
			}

			SetVertexParm( RENDERPARM_TEXGEN_0_S, localFogPlanes[0].ToFloatPtr() );
			SetVertexParm( RENDERPARM_TEXGEN_0_T, localFogPlanes[1].ToFloatPtr() );
			SetVertexParm( RENDERPARM_TEXGEN_1_T, localFogPlanes[2].ToFloatPtr() );
			SetVertexParm( RENDERPARM_TEXGEN_1_S, localFogPlanes[3].ToFloatPtr() );

			backEnd.currentSpace = ( inverseBaseLightProject == NULL ) ? drawSurf->space : NULL;
		}

		if ( drawSurf->jointCache ) {
			renderProgManager->BindShader_FogSkinned();
		} else {
			renderProgManager->BindShader_Fog();
		}

		
		DrawElementsWithCounters( drawSurf );
	}
}

void idRenderBackendVk::BlendLight(const drawSurf_t *drawSurfs, const viewLight_t * vLight)
{
	backEnd.currentSpace = NULL;

	for ( const drawSurf_t * drawSurf = drawSurfs; drawSurf != NULL; drawSurf = drawSurf->nextOnLight ) {
		if ( drawSurf->scissorRect.IsEmpty() ) {
			continue;	// !@# FIXME: find out why this is sometimes being hit!
						// temporarily jump over the scissor and draw so the gl error callback doesn't get hit
		}

		if ( !backEnd.currentScissor.Equals( drawSurf->scissorRect ) && r_useScissor.GetBool() ) {
			// change the scissor
			renderSystem->SetScissor( backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1,
						backEnd.viewDef->viewport.y1 + drawSurf->scissorRect.y1,
						drawSurf->scissorRect.x2 + 1 - drawSurf->scissorRect.x1,
						drawSurf->scissorRect.y2 + 1 - drawSurf->scissorRect.y1 );
			backEnd.currentScissor = drawSurf->scissorRect;
		}

		if ( drawSurf->space != backEnd.currentSpace ) {
			// change the matrix
			SetMVP( drawSurf->space->mvp );

			// change the light projection matrix
			idPlane	lightProjectInCurrentSpace[4];
			for ( int i = 0; i < 4; i++ ) {
				R_GlobalPlaneToLocal( drawSurf->space->modelMatrix, vLight->lightProject[i], lightProjectInCurrentSpace[i] );
			}

			SetVertexParm( RENDERPARM_TEXGEN_0_S, lightProjectInCurrentSpace[0].ToFloatPtr() );
			SetVertexParm( RENDERPARM_TEXGEN_0_T, lightProjectInCurrentSpace[1].ToFloatPtr() );
			SetVertexParm( RENDERPARM_TEXGEN_0_Q, lightProjectInCurrentSpace[2].ToFloatPtr() );
			SetVertexParm( RENDERPARM_TEXGEN_1_S, lightProjectInCurrentSpace[3].ToFloatPtr() );	// falloff

			backEnd.currentSpace = drawSurf->space;
		}

		DrawElementsWithCounters( drawSurf );
	}
}

void idRenderBackendVk::BlendLight(const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2, const viewLight_t * vLight)
{
	if ( drawSurfs == NULL ) {
		return;
	}
	if ( r_skipBlendLights.GetBool() ) {
		return;
	}
	renderLog.OpenBlock( vLight->lightShader->GetName() );

	const idMaterial * lightShader = vLight->lightShader;
	const float	* regs = vLight->shaderRegisters;

	// texture 1 will get the falloff texture
	GL_SelectTexture( 1 );
	vLight->falloffImage->Bind();

	// texture 0 will get the projected texture
	GL_SelectTexture( 0 );

	renderProgManager->BindShader_BlendLight();

	for ( int i = 0; i < lightShader->GetNumStages(); i++ ) {
		const shaderStage_t	*stage = lightShader->GetStage(i);

		if ( !regs[ stage->conditionRegister ] ) {
			continue;
		}

		renderSystem->SetState( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL );

		GL_SelectTexture( 0 );
		stage->texture.image->Bind();

		if ( stage->texture.hasMatrix ) {
			LoadShaderTextureMatrix( regs, &stage->texture );
		}

		// get the modulate values from the light, including alpha, unlike normal lights
		float lightColor[4];
		lightColor[0] = regs[ stage->color.registers[0] ];
		lightColor[1] = regs[ stage->color.registers[1] ];
		lightColor[2] = regs[ stage->color.registers[2] ];
		lightColor[3] = regs[ stage->color.registers[3] ];
		GL_Color( lightColor );

		BlendLight( drawSurfs, vLight );
		BlendLight( drawSurfs2, vLight );
	}

	GL_SelectTexture( 1 );
	globalImages->BindNull();

	GL_SelectTexture( 0 );

	renderProgManager->Unbind();
	renderLog.CloseBlock();
}

void idRenderBackendVk::DrawSingleInteraction(drawInteraction_t * din)
{
	if ( din->bumpImage == NULL ) {
		// stage wasn't actually an interaction
		return;
	}

	if ( din->diffuseImage == NULL || r_skipDiffuse.GetBool() ) {
		// this isn't a YCoCg black, but it doesn't matter, because
		// the diffuseColor will also be 0
		din->diffuseImage = globalImages->blackImage;
	}
	if ( din->specularImage == NULL || r_skipSpecular.GetBool() || din->ambientLight ) {
		din->specularImage = globalImages->blackImage;
	}
	if ( r_skipBump.GetBool() ) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	const bool diffuseIsBlack = ( din->diffuseImage == globalImages->blackImage )
									|| ( ( din->diffuseColor[0] <= 0 ) && ( din->diffuseColor[1] <= 0 ) && ( din->diffuseColor[2] <= 0 ) );
	const bool specularIsBlack = ( din->specularImage == globalImages->blackImage )
									|| ( ( din->specularColor[0] <= 0 ) && ( din->specularColor[1] <= 0 ) && ( din->specularColor[2] <= 0 ) );
	if ( diffuseIsBlack && specularIsBlack ) {
		return;
	}

	// bump matrix
	SetVertexParm( RENDERPARM_BUMPMATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	SetVertexParm( RENDERPARM_BUMPMATRIX_T, din->bumpMatrix[1].ToFloatPtr() );

	// diffuse matrix
	SetVertexParm( RENDERPARM_DIFFUSEMATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	SetVertexParm( RENDERPARM_DIFFUSEMATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );

	// specular matrix
	SetVertexParm( RENDERPARM_SPECULARMATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	SetVertexParm( RENDERPARM_SPECULARMATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	SetVertexColorParms( din->vertexColor );

	SetFragmentParm( RENDERPARM_DIFFUSEMODIFIER, din->diffuseColor.ToFloatPtr() );
	SetFragmentParm( RENDERPARM_SPECULARMODIFIER, din->specularColor.ToFloatPtr() );

	// texture 0 will be the per-surface bump map
 	GL_SelectTexture( INTERACTION_TEXUNIT_BUMP );
	din->bumpImage->Bind();

	// texture 3 is the per-surface diffuse map
	GL_SelectTexture( INTERACTION_TEXUNIT_DIFFUSE );
	din->diffuseImage->Bind();

	// texture 4 is the per-surface specular map
	GL_SelectTexture( INTERACTION_TEXUNIT_SPECULAR );
	din->specularImage->Bind();

	DrawElementsWithCounters( din->surf );
}

void idRenderBackendVk::FillDepthBufferGeneric(const drawSurf_t * const * drawSurfs, int numDrawSurfs)
{
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t * drawSurf = drawSurfs[i];
		const idMaterial * shader = drawSurf->material;

		// translucent surfaces don't put anything in the depth buffer and don't
		// test against it, which makes them fail the mirror clip plane operation
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			continue;
		}

		// get the expressions for conditionals / color / texcoords
		const float * regs = drawSurf->shaderRegisters;

		// if all stages of a material have been conditioned off, don't do anything
		int stage = 0;
		for ( ; stage < shader->GetNumStages(); stage++ ) {		
			const shaderStage_t * pStage = shader->GetStage( stage );
			// check the stage enable condition
			if ( regs[ pStage->conditionRegister ] != 0 ) {
				break;
			}
		}
		if ( stage == shader->GetNumStages() ) {
			continue;
		}

		// change the matrix if needed
		if ( drawSurf->space != backEnd.currentSpace ) {
			SetMVP( drawSurf->space->mvp );

			backEnd.currentSpace = drawSurf->space;
		}

		uint64 surfGLState = 0;

		// set polygon offset if necessary
		if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			surfGLState |= GLS_POLYGON_OFFSET;
			renderSystem->SetPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
		}

		// subviews will just down-modulate the color buffer
		float color[4];
		if ( shader->GetSort() == SS_SUBVIEW ) {
			surfGLState |= GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS;
			color[0] = 1.0f;
			color[1] = 1.0f;
			color[2] = 1.0f;
			color[3] = 1.0f;
		} else {
			// others just draw black
			color[0] = 0.0f;
			color[1] = 0.0f;
			color[2] = 0.0f;
			color[3] = 1.0f;
		}

		renderLog.OpenBlock( shader->GetName() );

		bool drawSolid = false;
		if ( shader->Coverage() == MC_OPAQUE ) {
			drawSolid = true;
		} else if ( shader->Coverage() == MC_PERFORATED ) {
			// we may have multiple alpha tested stages
			// if the only alpha tested stages are condition register omitted,
			// draw a normal opaque surface
			bool didDraw = false;

			// perforated surfaces may have multiple alpha tested stages
			for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {		
				const shaderStage_t *pStage = shader->GetStage(stage);

				if ( !pStage->hasAlphaTest ) {
					continue;
				}

				// check the stage enable condition
				if ( regs[ pStage->conditionRegister ] == 0 ) {
					continue;
				}

				// if we at least tried to draw an alpha tested stage,
				// we won't draw the opaque surface
				didDraw = true;

				// set the alpha modulate
				color[3] = regs[ pStage->color.registers[3] ];

				// skip the entire stage if alpha would be black
				if ( color[3] <= 0.0f ) {
					continue;
				}

				uint64 stageGLState = surfGLState;

				// set privatePolygonOffset if necessary
				if ( pStage->privatePolygonOffset ) {
					renderSystem->SetPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
					stageGLState |= GLS_POLYGON_OFFSET;
				}

				GL_Color( color );

#ifdef USE_CORE_PROFILE
				renderSystem->SetState( stageGLState );
				idVec4 alphaTestValue( regs[ pStage->alphaTestRegister ] );
				SetFragmentParm( RENDERPARM_ALPHA_TEST, alphaTestValue.ToFloatPtr() );
#else
				renderSystem->SetState( stageGLState | GLS_ALPHATEST_FUNC_GREATER | GLS_ALPHATEST_MAKE_REF( idMath::Ftob( 255.0f * regs[ pStage->alphaTestRegister ] ) ) );
#endif

				if ( drawSurf->jointCache ) {
					renderProgManager->BindShader_TextureVertexColorSkinned();
				} else {
					renderProgManager->BindShader_TextureVertexColor();
				}

				SetVertexColorParms( SVC_IGNORE );

				// bind the texture
				GL_SelectTexture( 0 );
				pStage->texture.image->Bind();

				// set texture matrix and texGens
				PrepareStageTexturing( pStage, drawSurf );

				// must render with less-equal for Z-Cull to work properly
				assert( ( GL_GetCurrentState() & GLS_DEPTHFUNC_BITS ) == GLS_DEPTHFUNC_LESS );

				// draw it
				DrawElementsWithCounters( drawSurf );

				// clean up
				FinishStageTexturing( pStage, drawSurf );

				// unset privatePolygonOffset if necessary
				if ( pStage->privatePolygonOffset ) {
					renderSystem->SetPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
				}
			}

			if ( !didDraw ) {
				drawSolid = true;
			}
		}

		// draw the entire surface solid
		if ( drawSolid ) {
			if ( shader->GetSort() == SS_SUBVIEW ) {
				renderProgManager->BindShader_Color();
				GL_Color( color );
				renderSystem->SetState( surfGLState );
			} else {
				if ( drawSurf->jointCache ) {
					renderProgManager->BindShader_DepthSkinned();
				} else {
					renderProgManager->BindShader_Depth();
				}
				renderSystem->SetState( surfGLState | GLS_ALPHAMASK );
			}

			// must render with less-equal for Z-Cull to work properly
			assert( ( GL_GetCurrentState() & GLS_DEPTHFUNC_BITS ) == GLS_DEPTHFUNC_LESS );

			// draw it
			DrawElementsWithCounters( drawSurf );
		}

		renderLog.CloseBlock();
	}

#ifdef USE_CORE_PROFILE
	SetFragmentParm( RENDERPARM_ALPHA_TEST, vec4_zero.ToFloatPtr() );
#endif
}

void idRenderBackendVk::FogPass(const drawSurf_t * drawSurfs, const drawSurf_t * drawSurfs2, const viewLight_t * vLight)
{
	renderLog.OpenBlock( vLight->lightShader->GetName() );

	// find the current color and density of the fog
	const idMaterial * lightShader = vLight->lightShader;
	const float * regs = vLight->shaderRegisters;
	// assume fog shaders have only a single stage
	const shaderStage_t * stage = lightShader->GetStage( 0 );

	float lightColor[4];
	lightColor[0] = regs[ stage->color.registers[0] ];
	lightColor[1] = regs[ stage->color.registers[1] ];
	lightColor[2] = regs[ stage->color.registers[2] ];
	lightColor[3] = regs[ stage->color.registers[3] ];

	GL_Color( lightColor );

	// calculate the falloff planes
	float a;

	// if they left the default value on, set a fog distance of 500
	if ( lightColor[3] <= 1.0f ) {
		a = -0.5f / DEFAULT_FOG_DISTANCE;
	} else {
		// otherwise, distance = alpha color
		a = -0.5f / lightColor[3];
	}

	// texture 0 is the falloff image
	GL_SelectTexture( 0 );
	globalImages->fogImage->Bind();

	// texture 1 is the entering plane fade correction
	GL_SelectTexture( 1 );
	globalImages->fogEnterImage->Bind();

	// S is based on the view origin
	const float s = vLight->fogPlane.Distance( backEnd.viewDef->renderView.vieworg );

	const float FOG_SCALE = 0.001f;

	idPlane fogPlanes[4];

	// S-0
	fogPlanes[0][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[0*4+2];
	fogPlanes[0][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[1*4+2];
	fogPlanes[0][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2*4+2];
	fogPlanes[0][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[3*4+2] + 0.5f;

	// T-0
	fogPlanes[1][0] = 0.0f;//a * backEnd.viewDef->worldSpace.modelViewMatrix[0*4+0];
	fogPlanes[1][1] = 0.0f;//a * backEnd.viewDef->worldSpace.modelViewMatrix[1*4+0];
	fogPlanes[1][2] = 0.0f;//a * backEnd.viewDef->worldSpace.modelViewMatrix[2*4+0];
	fogPlanes[1][3] = 0.5f;//a * backEnd.viewDef->worldSpace.modelViewMatrix[3*4+0] + 0.5f;

	// T-1 will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
	fogPlanes[2][0] = FOG_SCALE * vLight->fogPlane[0];
	fogPlanes[2][1] = FOG_SCALE * vLight->fogPlane[1];
	fogPlanes[2][2] = FOG_SCALE * vLight->fogPlane[2];
	fogPlanes[2][3] = FOG_SCALE * vLight->fogPlane[3] + FOG_ENTER;

	// S-1
	fogPlanes[3][0] = 0.0f;
	fogPlanes[3][1] = 0.0f;
	fogPlanes[3][2] = 0.0f;
	fogPlanes[3][3] = FOG_SCALE * s + FOG_ENTER;

	// draw it
	renderSystem->SetState( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	BasicFog( drawSurfs, fogPlanes, NULL );
	BasicFog( drawSurfs2, fogPlanes, NULL );

	// the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
	// of depthfunc_equal
	renderSystem->SetState( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS );
	renderSystem->SetCull( CT_BACK_SIDED );

	backEnd.zeroOneCubeSurface.space = &backEnd.viewDef->worldSpace;
	backEnd.zeroOneCubeSurface.scissorRect = backEnd.viewDef->scissor;
	BasicFog( &backEnd.zeroOneCubeSurface, fogPlanes, &vLight->inverseBaseLightProject );

	renderSystem->SetCull( CT_FRONT_SIDED );

	GL_SelectTexture( 1 );
	globalImages->BindNull();

	GL_SelectTexture( 0 );

	renderProgManager->Unbind();

	renderLog.CloseBlock();
}

void idRenderBackendVk::GetShaderTextureMatrix(const float *shaderRegisters, const textureStage_t *texture, float matrix[16])
{
	matrix[0*4+0] = shaderRegisters[ texture->matrix[0][0] ];
	matrix[1*4+0] = shaderRegisters[ texture->matrix[0][1] ];
	matrix[2*4+0] = 0.0f;
	matrix[3*4+0] = shaderRegisters[ texture->matrix[0][2] ];

	matrix[0*4+1] = shaderRegisters[ texture->matrix[1][0] ];
	matrix[1*4+1] = shaderRegisters[ texture->matrix[1][1] ];
	matrix[2*4+1] = 0.0f;
	matrix[3*4+1] = shaderRegisters[ texture->matrix[1][2] ];

	// we attempt to keep scrolls from generating incredibly large texture values, but
	// center rotations and center scales can still generate offsets that need to be > 1
	if ( matrix[3*4+0] < -40.0f || matrix[12] > 40.0f ) {
		matrix[3*4+0] -= (int)matrix[3*4+0];
	}
	if ( matrix[13] < -40.0f || matrix[13] > 40.0f ) {
		matrix[13] -= (int)matrix[13];
	}

	matrix[0*4+2] = 0.0f;
	matrix[1*4+2] = 0.0f;
	matrix[2*4+2] = 1.0f;
	matrix[3*4+2] = 0.0f;

	matrix[0*4+3] = 0.0f;
	matrix[1*4+3] = 0.0f;
	matrix[2*4+3] = 0.0f;
	matrix[3*4+3] = 1.0f;
}

void idRenderBackendVk::LoadShaderTextureMatrix(const float *shaderRegisters, const textureStage_t *texture)
{	
	float texS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float texT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

	if ( texture->hasMatrix ) {
		float matrix[16];
		GetShaderTextureMatrix( shaderRegisters, texture, matrix );
		texS[0] = matrix[0*4+0];
		texS[1] = matrix[1*4+0];
		texS[2] = matrix[2*4+0];
		texS[3] = matrix[3*4+0];
	
		texT[0] = matrix[0*4+1];
		texT[1] = matrix[1*4+1];
		texT[2] = matrix[2*4+1];
		texT[3] = matrix[3*4+1];

		RENDERLOG_PRINTF( "Setting Texture Matrix\n");
		renderLog.Indent();
		RENDERLOG_PRINTF( "Texture Matrix S : %4.3f, %4.3f, %4.3f, %4.3f\n", texS[0], texS[1], texS[2], texS[3] );
		RENDERLOG_PRINTF( "Texture Matrix T : %4.3f, %4.3f, %4.3f, %4.3f\n", texT[0], texT[1], texT[2], texT[3] );
		renderLog.Outdent();
	} 

	SetVertexParm( RENDERPARM_TEXTUREMATRIX_S, texS );
	SetVertexParm( RENDERPARM_TEXTUREMATRIX_T, texT );
}

void idRenderBackendVk::MotionBlur()
{
	if ( !backEnd.viewDef->viewEntitys ) {
		// 3D views only
		return;
	}
	if ( r_motionBlur.GetInteger() <= 0 ) {
		return;
	}
	if ( backEnd.viewDef->isSubview ) {
		return;
	}

	const idScreenRect & viewport = backEnd.viewDef->viewport;
	// clear the alpha buffer and draw only the hands + weapon into it so
	// we can avoid blurring them
	renderSystem->SetState( GLS_COLORMASK | GLS_DEPTHMASK );
	GL_Color( 0, 0, 0, 1 );

	idRenderMatrix mat( 1.0f,  0.0f, 0.0f, 0.0f,
						0.0f, -1.0f, 0.0f, 0.0f,
						0.0f,  0.0f, 0.0f, 0.0f,
						0.0f,  0.0f, 0.0f, 1.0f);
	SetVertexParms( RENDERPARM_MVPMATRIX_X, mat[0], 4 );
	renderSystem->SetScissor(viewport);
	renderSystem->SetCull(CT_TWO_SIDED);

	//We can't actually do alpha-clears on Vulkan - vkCmdClearAttachments
	//isn't affected by pipeline state and therefore color masks,
	//so simulate an alpha clear with big ol' friendly quad
	renderProgManager->BindShader_Color();
	DrawElementsWithCounters(&backEnd.unitSquareSurface);

	backEnd.currentSpace = NULL;
	GL_SelectTexture( 0 );
	globalImages->blackImage->Bind();
	GL_Color(0, 0, 0, 0);

	drawSurf_t **drawSurfs = (drawSurf_t **)&backEnd.viewDef->drawSurfs[0];
	for ( int surfNum = 0; surfNum < backEnd.viewDef->numDrawSurfs; surfNum++ ) {
		const drawSurf_t * surf = drawSurfs[ surfNum ];

		if ( !surf->space->weaponDepthHack && !surf->space->skipMotionBlur && !surf->material->HasSubview() ) {
			// Apply motion blur to this object
			continue;
		}

		const idMaterial * shader = surf->material;
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			// muzzle flash, etc
			continue;
		}

		// set mvp matrix
		if ( surf->space != backEnd.currentSpace ) {
			SetMVP( surf->space->mvp );
			backEnd.currentSpace = surf->space;
		}

		// this could just be a color, but we don't have a skinned color-only prog
		if ( surf->jointCache ) {
			renderProgManager->BindShader_TextureVertexColorSkinned();
		} else {
			renderProgManager->BindShader_TextureVertexColor();
		}

		// draw it solid
		DrawElementsWithCounters( surf );
	}
	renderSystem->SetState( GLS_DEPTHFUNC_ALWAYS );

	// copy off the color buffer and the depth buffer for the motion blur prog
	// we use the viewport dimensions for copying the buffers in case resolution scaling is enabled.
	//const idScreenRect & viewport = backEnd.viewDef->viewport;
	globalImages->currentRenderImage->CopyFramebuffer( viewport.x1, viewport.y1, viewport.GetWidth(), viewport.GetHeight() );

	// in stereo rendering, each eye needs to get a separate previous frame mvp
	int mvpIndex = ( backEnd.viewDef->renderView.viewEyeBuffer == 1 ) ? 1 : 0;

	// derive the matrix to go from current pixels to previous frame pixels
	idRenderMatrix	inverseMVP;
	idRenderMatrix::Inverse( backEnd.viewDef->worldSpace.mvp, inverseMVP );

	idRenderMatrix	motionMatrix;
	idRenderMatrix::Multiply( backEnd.prevMVP[mvpIndex], inverseMVP, motionMatrix );

	backEnd.prevMVP[mvpIndex] = backEnd.viewDef->worldSpace.mvp;

	//This is basically just a bunch of data for calculations piggybacking
	//on the MVPMATRIX param so set it directly and don't send it through 
	//SetMVP which would convert it into a Vulkan matrix unnecessarily.
	SetVertexParms( RENDERPARM_MVPMATRIX_X, motionMatrix[0], 4 );

	renderSystem->SetState( GLS_DEPTHFUNC_ALWAYS );
	renderSystem->SetCull( CT_TWO_SIDED );

	const uint32_t msaa = (uint32_t)Vk_SampleCount();

	renderProgManager->BindShader_MotionBlur();

	// let the fragment program know how many samples we are going to use
	idVec4 samples( (float)( 1 << r_motionBlur.GetInteger() ) );
	SetFragmentParm( RENDERPARM_OVERBRIGHT, samples.ToFloatPtr() );

	vkCmdPushConstants(Vk_ActiveCommandBuffer(), Vk_GetPipelineLayout(),
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &msaa);

	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();
	GL_SelectTexture( msaa > 1 ? 1 : 2 );
	globalImages->currentDepthImage->Bind();

	DrawElementsWithCounters( &backEnd.unitSquareSurface );
}

void idRenderBackendVk::RenderInteractions(const drawSurf_t *surfList, const viewLight_t * vLight, int depthFunc, bool performStencilTest, bool useLightDepthBounds)
{
	if ( surfList == NULL ) {
		return;
	}

	// change the scissor if needed, it will be constant across all the surfaces lit by the light
	if ( !backEnd.currentScissor.Equals( vLight->scissorRect ) && r_useScissor.GetBool() ) {
		renderSystem->SetScissor( backEnd.viewDef->viewport.x1 + vLight->scissorRect.x1, 
					backEnd.viewDef->viewport.y1 + vLight->scissorRect.y1,
					vLight->scissorRect.x2 + 1 - vLight->scissorRect.x1,
					vLight->scissorRect.y2 + 1 - vLight->scissorRect.y1 );
		backEnd.currentScissor = vLight->scissorRect;
	}

	// perform setup here that will be constant for all interactions
	if ( performStencilTest ) {
		renderSystem->SetState( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc | GLS_STENCIL_FUNC_EQUAL | GLS_STENCIL_MAKE_REF( STENCIL_SHADOW_TEST_VALUE ) | GLS_STENCIL_MAKE_MASK( STENCIL_SHADOW_MASK_VALUE ) );

	} else {
		renderSystem->SetState( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc | GLS_STENCIL_FUNC_ALWAYS );
	}

	// some rare lights have multiple animating stages, loop over them outside the surface list
	const idMaterial * lightShader = vLight->lightShader;
	const float * lightRegs = vLight->shaderRegisters;

	drawInteraction_t inter = {};
	inter.ambientLight = lightShader->IsAmbientLight();

	//---------------------------------
	// Split out the complex surfaces from the fast-path surfaces
	// so we can do the fast path ones all in a row.
	// The surfaces should already be sorted by space because they
	// are added single-threaded, and there is only a negligable amount
	// of benefit to trying to sort by materials.
	//---------------------------------
	static const int MAX_INTERACTIONS_PER_LIGHT = 1024;
	static const int MAX_COMPLEX_INTERACTIONS_PER_LIGHT = 128;
	idStaticList< const drawSurf_t *, MAX_INTERACTIONS_PER_LIGHT > allSurfaces;
	idStaticList< const drawSurf_t *, MAX_COMPLEX_INTERACTIONS_PER_LIGHT > complexSurfaces;
	for ( const drawSurf_t * walk = surfList; walk != NULL; walk = walk->nextOnLight ) {

		// make sure the triangle culling is done
		if ( walk->shadowVolumeState != SHADOWVOLUME_DONE ) {
			assert( walk->shadowVolumeState == SHADOWVOLUME_UNFINISHED || walk->shadowVolumeState == SHADOWVOLUME_DONE );

			uint64 start = Sys_Microseconds();
			while ( walk->shadowVolumeState == SHADOWVOLUME_UNFINISHED ) {
				Sys_Yield();
			}
			uint64 end = Sys_Microseconds();

			backEnd.pc.shadowMicroSec += end - start;
		}

		const idMaterial * surfaceShader = walk->material;
		if ( surfaceShader->GetFastPathBumpImage() ) {
			allSurfaces.Append( walk );
		} else {
			complexSurfaces.Append( walk );
		}
	}
	for ( int i = 0; i < complexSurfaces.Num(); i++ ) {
		allSurfaces.Append( complexSurfaces[i] );
	}

	bool lightDepthBoundsDisabled = false;

	for ( int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++ ) {
		const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

		// ignore stages that fail the condition
		if ( !lightRegs[ lightStage->conditionRegister ] ) {
			continue;
		}

		const float lightScale = r_lightScale.GetFloat();
		const idVec4 lightColor(
			lightScale * lightRegs[ lightStage->color.registers[0] ],
			lightScale * lightRegs[ lightStage->color.registers[1] ],
			lightScale * lightRegs[ lightStage->color.registers[2] ],
			lightRegs[ lightStage->color.registers[3] ] );
		// apply the world-global overbright and the 2x factor for specular
		const idVec4 diffuseColor = lightColor;
		const idVec4 specularColor = lightColor * 2.0f;

		float lightTextureMatrix[16];
		if ( lightStage->texture.hasMatrix ) {
			GetShaderTextureMatrix( lightRegs, &lightStage->texture, lightTextureMatrix );
		}

		// texture 1 will be the light falloff texture
		GL_SelectTexture( INTERACTION_TEXUNIT_FALLOFF );
		vLight->falloffImage->Bind();

		// texture 2 will be the light projection texture
		GL_SelectTexture( INTERACTION_TEXUNIT_PROJECTION );
		lightStage->texture.image->Bind();

		// force the light textures to not use anisotropic filtering, which is wasted on them
		// all of the texture sampler parms should be constant for all interactions, only
		// the actual texture image bindings will change

		//----------------------------------
		// For all surfaces on this light list, generate an interaction for this light stage
		//----------------------------------

		// setup renderparms assuming we will be drawing trivial surfaces first
		SetupForFastPathInteractions( diffuseColor, specularColor );

		// even if the space does not change between light stages, each light stage may need a different lightTextureMatrix baked in
		backEnd.currentSpace = NULL;

		for ( int sortedSurfNum = 0; sortedSurfNum < allSurfaces.Num(); sortedSurfNum++ ) {
			const drawSurf_t * const surf = allSurfaces[ sortedSurfNum ];

			// select the render prog
			if ( lightShader->IsAmbientLight() ) {
				if ( surf->jointCache ) {
					renderProgManager->BindShader_InteractionAmbientSkinned();
				} else {
					renderProgManager->BindShader_InteractionAmbient();
				}
			} else {
				if ( surf->jointCache ) {
					renderProgManager->BindShader_InteractionSkinned();
				} else {
					renderProgManager->BindShader_Interaction();
				}
			}

			const idMaterial * surfaceShader = surf->material;
			const float * surfaceRegs = surf->shaderRegisters;

			inter.surf = surf;

			// change the MVP matrix, view/light origin and light projection vectors if needed
			if ( surf->space != backEnd.currentSpace ) {
				backEnd.currentSpace = surf->space;

				// turn off the light depth bounds test if this model is rendered with a depth hack
				if ( useLightDepthBounds ) {
					if ( !surf->space->weaponDepthHack && surf->space->modelDepthHack == 0.0f ) {
						if ( lightDepthBoundsDisabled ) {
							renderSystem->SetDepthBoundsTest( vLight->scissorRect.zmin, vLight->scissorRect.zmax );
							lightDepthBoundsDisabled = false;
						}
					} else {
						if ( !lightDepthBoundsDisabled ) {
							renderSystem->SetDepthBoundsTest( 0.0f, 0.0f );
							lightDepthBoundsDisabled = true;
						}
					}
				}

				// model-view-projection
				SetMVP( surf->space->mvp );

				// tranform the light/view origin into model local space
				idVec4 localLightOrigin( 0.0f );
				idVec4 localViewOrigin( 1.0f );
				R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, localLightOrigin.ToVec3() );
				R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );

				// set the local light/view origin
				SetVertexParm( RENDERPARM_LOCALLIGHTORIGIN, localLightOrigin.ToFloatPtr() );
				SetVertexParm( RENDERPARM_LOCALVIEWORIGIN, localViewOrigin.ToFloatPtr() );

				// transform the light project into model local space
				idPlane lightProjection[4];
				for ( int i = 0; i < 4; i++ ) {
					R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[i], lightProjection[i] );
				}

				// optionally multiply the local light projection by the light texture matrix
				if ( lightStage->texture.hasMatrix ) {
					BakeTextureMatrixIntoTexgen( lightProjection, lightTextureMatrix );
				}

				// set the light projection
				SetVertexParm( RENDERPARM_LIGHTPROJECTION_S, lightProjection[0].ToFloatPtr() );
				SetVertexParm( RENDERPARM_LIGHTPROJECTION_T, lightProjection[1].ToFloatPtr() );
				SetVertexParm( RENDERPARM_LIGHTPROJECTION_Q, lightProjection[2].ToFloatPtr() );
				SetVertexParm( RENDERPARM_LIGHTFALLOFF_S, lightProjection[3].ToFloatPtr() );
			}

			// check for the fast path
			if ( surfaceShader->GetFastPathBumpImage() && !r_skipInteractionFastPath.GetBool() ) {
				renderLog.OpenBlock( surf->material->GetName() );

				// texture 0 will be the per-surface bump map
				GL_SelectTexture( INTERACTION_TEXUNIT_BUMP );
				surfaceShader->GetFastPathBumpImage()->Bind();

				// texture 3 is the per-surface diffuse map
				GL_SelectTexture( INTERACTION_TEXUNIT_DIFFUSE );
				surfaceShader->GetFastPathDiffuseImage()->Bind();

				// texture 4 is the per-surface specular map
				GL_SelectTexture( INTERACTION_TEXUNIT_SPECULAR );
				surfaceShader->GetFastPathSpecularImage()->Bind();

				DrawElementsWithCounters( surf );

				renderLog.CloseBlock();
				continue;
			}
			
			renderLog.OpenBlock( surf->material->GetName() );

			inter.bumpImage = NULL;
			inter.specularImage = NULL;
			inter.diffuseImage = NULL;
			inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
			inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

			// go through the individual surface stages
			//
			// This is somewhat arcane because of the old support for video cards that had to render
			// interactions in multiple passes.
			//
			// We also have the very rare case of some materials that have conditional interactions
			// for the "hell writing" that can be shined on them.
			for ( int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++ ) {
				const shaderStage_t	*surfaceStage = surfaceShader->GetStage( surfaceStageNum );

				switch( surfaceStage->lighting ) {
					case SL_COVERAGE: {
						// ignore any coverage stages since they should only be used for the depth fill pass
						// for diffuse stages that use alpha test.
						break;
					}
					case SL_AMBIENT: {
						// ignore ambient stages while drawing interactions
						break;
					}
					case SL_BUMP: {
						// ignore stage that fails the condition
						if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
							break;
						}
						// draw any previous interaction
						if ( inter.bumpImage != NULL ) {
							DrawSingleInteraction( &inter );
						}
						inter.bumpImage = surfaceStage->texture.image;
						inter.diffuseImage = NULL;
						inter.specularImage = NULL;
						SetupInteractionStage( surfaceStage, surfaceRegs, NULL,
												inter.bumpMatrix, NULL );
						break;
					}
					case SL_DIFFUSE: {
						// ignore stage that fails the condition
						if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
							break;
						}
						// draw any previous interaction
						if ( inter.diffuseImage != NULL ) {
							DrawSingleInteraction( &inter );
						}
						inter.diffuseImage = surfaceStage->texture.image;
						inter.vertexColor = surfaceStage->vertexColor;
						SetupInteractionStage( surfaceStage, surfaceRegs, diffuseColor.ToFloatPtr(),
												inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
						break;
					}
					case SL_SPECULAR: {
						// ignore stage that fails the condition
						if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
							break;
						}
						// draw any previous interaction
						if ( inter.specularImage != NULL ) {
							DrawSingleInteraction( &inter );
						}
						inter.specularImage = surfaceStage->texture.image;
						inter.vertexColor = surfaceStage->vertexColor;
						SetupInteractionStage( surfaceStage, surfaceRegs, specularColor.ToFloatPtr(),
												inter.specularMatrix, inter.specularColor.ToFloatPtr() );
						break;
					}
				}
			}

			// draw the final interaction
			DrawSingleInteraction( &inter );

			renderLog.CloseBlock();
		}
	}

	if ( useLightDepthBounds && lightDepthBoundsDisabled ) {
		renderSystem->SetDepthBoundsTest( vLight->scissorRect.zmin, vLight->scissorRect.zmax );
	}

	renderProgManager->Unbind();
}

void idRenderBackendVk::SetupForFastPathInteractions(const idVec4 & diffuseColor, const idVec4 & specularColor)
{
	const idVec4 sMatrix( 1, 0, 0, 0 );
	const idVec4 tMatrix( 0, 1, 0, 0 );

	// bump matrix
	SetVertexParm( RENDERPARM_BUMPMATRIX_S, sMatrix.ToFloatPtr() );
	SetVertexParm( RENDERPARM_BUMPMATRIX_T, tMatrix.ToFloatPtr() );

	// diffuse matrix
	SetVertexParm( RENDERPARM_DIFFUSEMATRIX_S, sMatrix.ToFloatPtr() );
	SetVertexParm( RENDERPARM_DIFFUSEMATRIX_T, tMatrix.ToFloatPtr() );

	// specular matrix
	SetVertexParm( RENDERPARM_SPECULARMATRIX_S, sMatrix.ToFloatPtr() );
	SetVertexParm( RENDERPARM_SPECULARMATRIX_T, tMatrix.ToFloatPtr() );

	SetVertexColorParms( SVC_IGNORE );

	SetFragmentParm( RENDERPARM_DIFFUSEMODIFIER, diffuseColor.ToFloatPtr() );
	SetFragmentParm( RENDERPARM_SPECULARMODIFIER, specularColor.ToFloatPtr() );
}

void idRenderBackendVk::SetupInteractionStage(const shaderStage_t *surfaceStage, const float *surfaceRegs, const float lightColor[4], idVec4 matrix[2], float color[4])
{

	if ( surfaceStage->texture.hasMatrix ) {
		matrix[0][0] = surfaceRegs[surfaceStage->texture.matrix[0][0]];
		matrix[0][1] = surfaceRegs[surfaceStage->texture.matrix[0][1]];
		matrix[0][2] = 0.0f;
		matrix[0][3] = surfaceRegs[surfaceStage->texture.matrix[0][2]];

		matrix[1][0] = surfaceRegs[surfaceStage->texture.matrix[1][0]];
		matrix[1][1] = surfaceRegs[surfaceStage->texture.matrix[1][1]];
		matrix[1][2] = 0.0f;
		matrix[1][3] = surfaceRegs[surfaceStage->texture.matrix[1][2]];

		// we attempt to keep scrolls from generating incredibly large texture values, but
		// center rotations and center scales can still generate offsets that need to be > 1
		if ( matrix[0][3] < -40.0f || matrix[0][3] > 40.0f ) {
			matrix[0][3] -= idMath::Ftoi( matrix[0][3] );
		}
		if ( matrix[1][3] < -40.0f || matrix[1][3] > 40.0f ) {
			matrix[1][3] -= idMath::Ftoi( matrix[1][3] );
		}
	} else {
		matrix[0][0] = 1.0f;
		matrix[0][1] = 0.0f;
		matrix[0][2] = 0.0f;
		matrix[0][3] = 0.0f;

		matrix[1][0] = 0.0f;
		matrix[1][1] = 1.0f;
		matrix[1][2] = 0.0f;
		matrix[1][3] = 0.0f;
	}

	if ( color != NULL ) {
		for ( int i = 0; i < 4; i++ ) {
			// clamp here, so cards with a greater range don't look different.
			// we could perform overbrighting like we do for lights, but
			// it doesn't currently look worth it.
			color[i] = idMath::ClampFloat( 0.0f, 1.0f, surfaceRegs[surfaceStage->color.registers[i]] ) * lightColor[i];
		}
	}
}

void idRenderBackendVk::StencilSelectLight(const viewLight_t * vLight)
{
	renderLog.OpenBlock( "Stencil Select" );

	// enable the light scissor
	if ( !backEnd.currentScissor.Equals( vLight->scissorRect ) && r_useScissor.GetBool() ) {
		renderSystem->SetScissor( backEnd.viewDef->viewport.x1 + vLight->scissorRect.x1, 
					backEnd.viewDef->viewport.y1 + vLight->scissorRect.y1,
					vLight->scissorRect.x2 + 1 - vLight->scissorRect.x1,
					vLight->scissorRect.y2 + 1 - vLight->scissorRect.y1 );
		backEnd.currentScissor = vLight->scissorRect;
	}

	// clear stencil buffer to 0 (not drawable)
	uint64 glStateMinusStencil = GL_GetCurrentStateMinusStencil();
	renderSystem->SetState( glStateMinusStencil | GLS_STENCIL_FUNC_ALWAYS | GLS_STENCIL_MAKE_REF( STENCIL_SHADOW_TEST_VALUE ) | GLS_STENCIL_MAKE_MASK( STENCIL_SHADOW_MASK_VALUE ) );	// make sure stencil mask passes for the clear
	renderSystem->Clear( false, false, true, 0, 0.0f, 0.0f, 0.0f, 0.0f );	// clear to 0 for stencil select

	// set the depthbounds
	renderSystem->SetDepthBoundsTest( vLight->scissorRect.zmin, vLight->scissorRect.zmax );


	renderSystem->SetState( GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHMASK | GLS_DEPTHFUNC_LESS | GLS_STENCIL_FUNC_ALWAYS | GLS_STENCIL_MAKE_REF( STENCIL_SHADOW_TEST_VALUE ) | GLS_STENCIL_MAKE_MASK( STENCIL_SHADOW_MASK_VALUE ) );
	renderSystem->SetCull( CT_TWO_SIDED );

	renderProgManager->BindShader_Depth();

	// set the matrix for deforming the 'zeroOneCubeModel' into the frustum to exactly cover the light volume
	idRenderMatrix invProjectMVPMatrix;
	idRenderMatrix::Multiply( backEnd.viewDef->worldSpace.mvp, vLight->inverseBaseLightProject, invProjectMVPMatrix );
	SetMVP( invProjectMVPMatrix );

	// two-sided stencil test
	backEnd.glState.shadowStencilFront = 
		GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_REPLACE | GLS_STENCIL_OP_PASS_ZERO;
	backEnd.glState.shadowStencilBack =
		GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_ZERO | GLS_STENCIL_OP_PASS_REPLACE;

	DrawElementsWithCounters( &backEnd.zeroOneCubeSurface );

	// reset stencil state

	renderSystem->SetCull( CT_FRONT_SIDED );

	renderProgManager->Unbind();


	// unset the depthbounds
	renderSystem->SetDepthBoundsTest( 0.0f, 0.0f );

	renderLog.CloseBlock();
}

void idRenderBackendVk::StencilShadowPass(const drawSurf_t *drawSurfs, const viewLight_t * vLight)
{
	if ( r_skipShadows.GetBool() ) {
		return;
	}

	if ( drawSurfs == NULL ) {
		return;
	}

	RENDERLOG_PRINTF( "---------- RB_StencilShadowPass ----------\n" );

	renderProgManager->BindShader_Shadow();

	GL_SelectTexture( 0 );
	globalImages->BindNull();

	uint64 glState = 0;

	// for visualizing the shadows
	if ( r_showShadows.GetInteger() ) {
		// set the debug shadow color
		SetFragmentParm( RENDERPARM_COLOR, colorMagenta.ToFloatPtr() );
		if ( r_showShadows.GetInteger() == 2 ) {
			// draw filled in
			glState = GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS;
		} else {
			// draw as lines, filling the depth buffer
			glState = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS;
		}
	} else {
		// don't write to the color or depth buffer, just the stencil buffer
		glState = GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS;
	}

	renderSystem->SetPolygonOffset( r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat() );

	// the actual stencil func will be set in the draw code, but we need to make sure it isn't
	// disabled here, and that the value will get reset for the interactions without looking
	// like a no-change-required
	renderSystem->SetState( glState | GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_KEEP | GLS_STENCIL_OP_PASS_INCR | 
		GLS_STENCIL_MAKE_REF( STENCIL_SHADOW_TEST_VALUE ) | GLS_STENCIL_MAKE_MASK( STENCIL_SHADOW_MASK_VALUE ) | GLS_POLYGON_OFFSET );

	// Two Sided Stencil reduces two draw calls to one for slightly faster shadows
	renderSystem->SetCull( CT_TWO_SIDED );


	// process the chain of shadows with the current rendering state
	backEnd.currentSpace = NULL;


	for ( const drawSurf_t * drawSurf = drawSurfs; drawSurf != NULL; drawSurf = drawSurf->nextOnLight ) {
		if ( drawSurf->scissorRect.IsEmpty() ) {
			continue;	// !@# FIXME: find out why this is sometimes being hit!
						// temporarily jump over the scissor and draw so the gl error callback doesn't get hit
		}

		// make sure the shadow volume is done
		if ( drawSurf->shadowVolumeState != SHADOWVOLUME_DONE ) {
			assert( drawSurf->shadowVolumeState == SHADOWVOLUME_UNFINISHED || drawSurf->shadowVolumeState == SHADOWVOLUME_DONE );

			uint64 start = Sys_Microseconds();
			while ( drawSurf->shadowVolumeState == SHADOWVOLUME_UNFINISHED ) {
				Sys_Yield();
			}
			uint64 end = Sys_Microseconds();

			backEnd.pc.shadowMicroSec += end - start;
		}

		if ( drawSurf->numIndexes == 0 ) {
			continue;	// a job may have created an empty shadow volume
		}

		if ( !backEnd.currentScissor.Equals( drawSurf->scissorRect ) && r_useScissor.GetBool() ) {
			// change the scissor
			renderSystem->SetScissor( backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1,
						backEnd.viewDef->viewport.y1 + drawSurf->scissorRect.y1,
						drawSurf->scissorRect.x2 + 1 - drawSurf->scissorRect.x1,
						drawSurf->scissorRect.y2 + 1 - drawSurf->scissorRect.y1 );
			backEnd.currentScissor = drawSurf->scissorRect;
		}

		if ( drawSurf->space != backEnd.currentSpace ) {
			// change the matrix
			SetMVP( drawSurf->space->mvp );

			// set the local light position to allow the vertex program to project the shadow volume end cap to infinity
			idVec4 localLight( 0.0f );
			R_GlobalPointToLocal( drawSurf->space->modelMatrix, vLight->globalLightOrigin, localLight.ToVec3() );
			SetVertexParm( RENDERPARM_LOCALLIGHTORIGIN, localLight.ToFloatPtr() );

			backEnd.currentSpace = drawSurf->space;
		}

		if ( r_showShadows.GetInteger() == 0 ) {
			if ( drawSurf->jointCache ) {
				renderProgManager->BindShader_ShadowSkinned();
			} else {
				renderProgManager->BindShader_Shadow();
			}
		} else {
			if ( drawSurf->jointCache ) {
				renderProgManager->BindShader_ShadowDebugSkinned();
			} else {
				renderProgManager->BindShader_ShadowDebug();
			}
		}

		// set depth bounds per shadow
		if ( r_useShadowDepthBounds.GetBool() ) {
			renderSystem->SetDepthBoundsTest( drawSurf->scissorRect.zmin, drawSurf->scissorRect.zmax );
		}

		// Determine whether or not the shadow volume needs to be rendered with Z-pass or
		// Z-fail. It is worthwhile to spend significant resources to reduce the number of
		// cases where shadow volumes need to be rendered with Z-fail because Z-fail
		// rendering can be significantly slower even on today's hardware. For instance,
		// on NVIDIA hardware Z-fail rendering causes the Z-Cull to be used in reverse:
		// Z-near becomes Z-far (trivial accept becomes trivial reject). Using the Z-Cull
		// in reverse is far less efficient because the Z-Cull only stores Z-near per 16x16
		// pixels while the Z-far is stored per 4x2 pixels. (The Z-near coallesce buffer
		// which has 4x4 granularity is only used when updating the depth which is not the
		// case for shadow volumes.) Note that it is also important to NOT use a Z-Cull
		// reconstruct because that would clear the Z-near of the Z-Cull which results in
		// no trivial rejection for Z-fail stencil shadow rendering.

		const bool renderZPass = ( drawSurf->renderZFail == 0 ) || r_forceZPassStencilShadows.GetBool();


		if ( renderZPass ) {
			// Z-pass
			backEnd.glState.shadowStencilFront = 
				GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_KEEP | GLS_STENCIL_OP_PASS_INCR;
			backEnd.glState.shadowStencilBack =
				GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_KEEP | GLS_STENCIL_OP_PASS_DECR;

		} else if ( r_useStencilShadowPreload.GetBool() ) {
			// preload + Z-pass
			backEnd.glState.shadowStencilFront =
				GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_DECR | GLS_STENCIL_OP_PASS_DECR;
			backEnd.glState.shadowStencilBack =
				GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_INCR | GLS_STENCIL_OP_PASS_INCR;
		} else {
			// Z-fail
		}


		// get vertex buffer
		const vertCacheHandle_t vbHandle = drawSurf->shadowCache;
		idVertexBuffer * vertexBuffer;
		if ( vertexCache->CacheIsStatic( vbHandle ) ) {
			vertexBuffer = (idVertexBuffer*)vertexCache->staticData.vertexBuffer;
		} else {
			const uint64 frameNum = (int)( vbHandle >> VERTCACHE_FRAME_SHIFT ) & VERTCACHE_FRAME_MASK;
			if ( frameNum != ( ( vertexCache->currentFrame - 1 ) & VERTCACHE_FRAME_MASK ) ) {
				idLib::Warning( "RB_DrawElementsWithCounters, vertexBuffer == NULL" );
				continue;
			}
			vertexBuffer = (idVertexBuffer*)vertexCache->frameData[vertexCache->drawListNum].vertexBuffer;
		}
		const int vertOffset = (int)( vbHandle >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;

		// get index buffer
		const vertCacheHandle_t ibHandle = drawSurf->indexCache;
		idIndexBuffer * indexBuffer;
		if ( vertexCache->CacheIsStatic( ibHandle ) ) {
			indexBuffer = (idIndexBuffer*)vertexCache->staticData.indexBuffer;
		} else {
			const uint64 frameNum = (int)( ibHandle >> VERTCACHE_FRAME_SHIFT ) & VERTCACHE_FRAME_MASK;
			if ( frameNum != ( ( vertexCache->currentFrame - 1 ) & VERTCACHE_FRAME_MASK ) ) {
				idLib::Warning( "RB_DrawElementsWithCounters, indexBuffer == NULL" );
				continue;
			}
			indexBuffer = (idIndexBuffer*)vertexCache->frameData[vertexCache->drawListNum].indexBuffer;
		}
		const uint64 indexOffset = (int)( ibHandle >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;

		RENDERLOG_PRINTF( "Binding Buffers: %p %p\n", vertexBuffer, indexBuffer );


		if ( backEnd.glState.currentIndexBuffer != (GLuint)indexBuffer->GetAPIObject() || !r_useStateCaching.GetBool() ) {
			backEnd.glState.currentIndexBuffer = (GLuint)indexBuffer->GetAPIObject();
		}

		if ( drawSurf->jointCache ) {
			assert( renderProgManager->ShaderUsesJoints() );
			backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT_SKINNED;
		} else {
			backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT;
		}

		BindAndSubmitDrawcall(drawSurf);

		if ( !renderZPass && r_useStencilShadowPreload.GetBool() ) {
			// render again with Z-pass

			backEnd.glState.shadowStencilFront = 
				GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_KEEP | GLS_STENCIL_OP_PASS_INCR;
			backEnd.glState.shadowStencilBack =
				GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_KEEP | GLS_STENCIL_OP_PASS_DECR;

			BindAndSubmitDrawcall(drawSurf);
		}
	}	

	// cleanup the shadow specific rendering state

	renderSystem->SetCull( CT_FRONT_SIDED );

	// reset depth bounds
	if ( r_useShadowDepthBounds.GetBool() ) {
		if ( r_useLightDepthBounds.GetBool() ) {
			renderSystem->SetDepthBoundsTest( vLight->scissorRect.zmin, vLight->scissorRect.zmax );
		} else {
			renderSystem->SetDepthBoundsTest( 0.0f, 0.0f );
		}
	}
}

#endif
