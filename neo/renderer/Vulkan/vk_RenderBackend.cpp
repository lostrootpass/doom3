#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "../tr_local.h"
#include "../RenderParms.h"

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

}

void idRenderBackendVk::DrawElementsWithCounters(const drawSurf_t* surf)
{
	VkCommandBuffer cmd = Vk_ActiveCommandBuffer();

	// get vertex buffer
	const vertCacheHandle_t vbHandle = surf->ambientCache;
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

	//TODO: move the sync call elsewhere -- too expensive to do this for every call
	//but needs to be moved to somewhere we can guarantee that we're "done" with the buffer
	//need to check if the buffer gets updated after draw calls start, might need
	//to do partial buffer syncs each call. slow, but better than full syncs each call.
	//((idVertexBufferVk*)vertexBuffer)->Sync();

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

	//((idIndexBufferVk*)indexBuffer)->Sync();
	//GL indices are byte though
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


	if ( surf->jointCache ) {
		idJointBuffer jointBuffer;
		if ( !vertexCache->GetJointBuffer( surf->jointCache, &jointBuffer ) ) {
			idLib::Warning( "RB_DrawElementsWithCounters, jointBuffer == NULL" );
			return;
		}
		assert( ( jointBuffer.GetOffset() & ( glConfig.uniformBufferOffsetAlignment - 1 ) ) == 0 );

		const GLuint ubo = reinterpret_cast< GLuint >( jointBuffer.GetAPIObject() );
		//qglBindBufferRange( GL_UNIFORM_BUFFER, 0, ubo, jointBuffer.GetOffset(), jointBuffer.GetNumJoints() * sizeof( idJointMat ) );

		//if (vertexCache->CacheIsStatic(surf->jointCache))
		//{
		//	idJointBufferVk* jb = (idJointBufferVk*)vertexCache->staticData.jointBuffer;
		//	jb->Sync();
		//}
		//else
		//{
		//	idJointBufferVk* jb = (idJointBufferVk*)vertexCache->frameData[vertexCache->drawListNum].jointBuffer;
		//	jb->Sync();
		//}
	}

	//TODO: find out how to set this properly
	//backEnd.glState.vertexLayout = LAYOUT_DRAW_VERT;

	idRenderProgManagerVk* rpm = (idRenderProgManagerVk*)renderProgManager;
	Vk_UsePipeline(rpm->GetPipelineForState(backEnd.glState.glStateBits));

	rpm->CommitUniforms();
	VkDescriptorSet sets[] = { Vk_UniformDescriptorSet() };

	if (backEnd.glState.vertexLayout == LAYOUT_DRAW_VERT)
	{
		uint32_t dynamicOffsetCount = 3;
		const uint32_t offsets[] = {
			rpm->GetCurrentVertUniformOffset(), rpm->GetCurrentFragUniformOffset(), 0
		};

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Vk_GetPipelineLayout(),
			0, 1, sets, dynamicOffsetCount, offsets);
	}
	else if (backEnd.glState.vertexLayout == LAYOUT_DRAW_SHADOW_VERT_SKINNED)
	{
		//TODO
		return;

		uint32 dynamicOffsetCount = 3;
		const uint32_t offsets[] = {
			rpm->GetCurrentVertUniformOffset(), 0, 0
		};

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Vk_GetPipelineLayout(),
			0, 1, sets, dynamicOffsetCount, offsets);
	}
	else if (backEnd.glState.vertexLayout == LAYOUT_DRAW_SHADOW_VERT)
	{
		//TODO
		return;
	}

	vkCmdDrawIndexed(cmd, r_singleTriangle.GetBool() ? 3 : surf->numIndexes,
		1, 0, 0, 0);
}

void idRenderBackendVk::DrawInteractions()
{
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
			/*GL_Scissor( backEnd.viewDef->viewport.x1 + surf->scissorRect.x1, 
						backEnd.viewDef->viewport.y1 + surf->scissorRect.y1,
						surf->scissorRect.x2 + 1 - surf->scissorRect.x1,
						surf->scissorRect.y2 + 1 - surf->scissorRect.y1 );*/
			backEnd.currentScissor = surf->scissorRect;
		}

		// get the expressions for conditionals / color / texcoords
		const float	*regs = surf->shaderRegisters;

		// set face culling appropriately
		/*if ( surf->space->isGuiSurface ) {
			GL_Cull( CT_TWO_SIDED );
		} else {
			GL_Cull( shader->GetCullType() );
		}*/

		uint64 surfGLState = surf->extraGLState;

		// set polygon offset if necessary
		if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
			GL_PolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
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

				GL_State( stageGLState );
			
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
						//renderProgManager->BindShader_BinkGUI();
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
				GL_PolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
				stageGLState |= GLS_POLYGON_OFFSET;
			}

			// set the state
			GL_State( stageGLState );

			PrepareStageTexturing( pStage, surf );

			// draw it
			backEnd.glState.vertexLayout = LAYOUT_DRAW_VERT;
			DrawElementsWithCounters( surf );

			FinishStageTexturing( pStage, surf );

			// unset privatePolygonOffset if necessary
			if ( pStage->privatePolygonOffset ) {
				GL_PolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
			}
			renderLog.CloseBlock();
		}

		renderLog.CloseBlock();
	}

	//GL_Cull( CT_FRONT_SIDED );
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
		//GL_SetDefaultState();
	}

	// optionally draw a box colored based on the eye number
	if ( r_drawEyeColor.GetBool() ) {
		const idScreenRect & r = backEnd.viewDef->viewport;
		//GL_Scissor( ( r.x1 + r.x2 ) / 2, ( r.y1 + r.y2 ) / 2, 32, 32 );
		switch ( stereoEye ) {
			case -1:
				//GL_Clear( true, false, false, 0, 1.0f, 0.0f, 0.0f, 1.0f );
				break;
			case 1:
				//GL_Clear( true, false, false, 0, 0.0f, 1.0f, 0.0f, 1.0f );
				break;
			default:
				//GL_Clear( true, false, false, 0, 0.5f, 0.5f, 0.5f, 1.0f );
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
	VkViewport viewport = { 
		viewDef->viewport.x1,
		viewDef->viewport.y1,
		//viewDef->viewport.y2 + 1,
		viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
		viewDef->viewport.y2 + 1 - viewDef->viewport.y1,
		-1.0f, 1.0f 
	};

	// the scissor may be smaller than the viewport for subviews
	VkRect2D scissor = { backEnd.viewDef->viewport.x1 + viewDef->scissor.x1,
				backEnd.viewDef->viewport.y1 + viewDef->scissor.y1,
				//viewDef->scissor.y2 + 1,
				viewDef->scissor.x2 + 1 - viewDef->scissor.x1,
				viewDef->scissor.y2 + 1 - viewDef->scissor.y1 };

	//VkViewport viewport = { 0, 0, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
	//VkRect2D scissor = { 0, 0, extent.width, extent.height };

	VkCommandBuffer cmd = Vk_ActiveCommandBuffer();
	//vkCmdSetViewport(cmd, 0, 1, &viewport);
	//vkCmdSetScissor(cmd, 0, 1, &scissor);

	backEnd.currentScissor = viewDef->scissor;

	backEnd.glState.faceCulling = -1;		// force face culling to set next time

	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );


	// Clear the depth buffer and clear the stencil to 128 for stencil shadows as well as gui masking
	//GL_Clear( false, true, true, STENCIL_SHADOW_TEST_VALUE, 0.0f, 0.0f, 0.0f, 0.0f );

	// normal face culling
	//GL_Cull( CT_FRONT_SIDED );

#ifdef USE_CORE_PROFILE
	// bind one global Vertex Array Object (VAO)
	//qglBindVertexArray( glConfig.global_vao );
#endif

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

	GL_StartDepthPass( backEnd.viewDef->scissor );

	// force MVP change on first surface
	backEnd.currentSpace = NULL;

	// draw all the subview surfaces, which will already be at the start of the sorted list,
	// with the general purpose path
	GL_State( GLS_DEFAULT );

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
	GL_State( GLS_DEFAULT );

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
			backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT_SKINNED;
			renderProgManager->BindShader_DepthSkinned();
		} else {
			backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT;
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
	GL_FinishDepthPass();

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();
}

void idRenderBackendVk::FinishStageTexturing(const shaderStage_t *pStage, const drawSurf_t *surf)
{

}

void idRenderBackendVk::FogAllLights()
{

}

void idRenderBackendVk::PostProcess(const void* data)
{

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
			GL_Scissor( backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1,
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

		GL_State( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL );

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
			GL_PolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
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
					GL_PolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
					stageGLState |= GLS_POLYGON_OFFSET;
				}

				GL_Color( color );

#ifdef USE_CORE_PROFILE
				GL_State( stageGLState );
				idVec4 alphaTestValue( regs[ pStage->alphaTestRegister ] );
				SetFragmentParm( RENDERPARM_ALPHA_TEST, alphaTestValue.ToFloatPtr() );
#else
				GL_State( stageGLState | GLS_ALPHATEST_FUNC_GREATER | GLS_ALPHATEST_MAKE_REF( idMath::Ftob( 255.0f * regs[ pStage->alphaTestRegister ] ) ) );
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
					GL_PolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
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
				GL_State( surfGLState );
			} else {
				if ( drawSurf->jointCache ) {
					backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT_SKINNED;
					renderProgManager->BindShader_DepthSkinned();
				} else {
					backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT;
					renderProgManager->BindShader_Depth();
				}
				GL_State( surfGLState | GLS_ALPHAMASK );
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
}

void idRenderBackendVk::RenderInteractions(const drawSurf_t *surfList, const viewLight_t * vLight, int depthFunc, bool performStencilTest, bool useLightDepthBounds)
{
	if ( surfList == NULL ) {
		return;
	}

	// change the scissor if needed, it will be constant across all the surfaces lit by the light
	if ( !backEnd.currentScissor.Equals( vLight->scissorRect ) && r_useScissor.GetBool() ) {
		GL_Scissor( backEnd.viewDef->viewport.x1 + vLight->scissorRect.x1, 
					backEnd.viewDef->viewport.y1 + vLight->scissorRect.y1,
					vLight->scissorRect.x2 + 1 - vLight->scissorRect.x1,
					vLight->scissorRect.y2 + 1 - vLight->scissorRect.y1 );
		backEnd.currentScissor = vLight->scissorRect;
	}

	// perform setup here that will be constant for all interactions
	if ( performStencilTest ) {
		GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc | GLS_STENCIL_FUNC_EQUAL | GLS_STENCIL_MAKE_REF( STENCIL_SHADOW_TEST_VALUE ) | GLS_STENCIL_MAKE_MASK( STENCIL_SHADOW_MASK_VALUE ) );

	} else {
		GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc | GLS_STENCIL_FUNC_ALWAYS );
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
							GL_DepthBoundsTest( vLight->scissorRect.zmin, vLight->scissorRect.zmax );
							lightDepthBoundsDisabled = false;
						}
					} else {
						if ( !lightDepthBoundsDisabled ) {
							GL_DepthBoundsTest( 0.0f, 0.0f );
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
		GL_DepthBoundsTest( vLight->scissorRect.zmin, vLight->scissorRect.zmax );
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
}

void idRenderBackendVk::StencilShadowPass(const drawSurf_t *drawSurfs, const viewLight_t * vLight)
{
}
