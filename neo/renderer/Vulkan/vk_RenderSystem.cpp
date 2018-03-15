#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "../tr_local.h"
#include "framework/Common_local.h"
#include "vk_RenderBackend.h"

#ifdef DOOM3_VULKAN

idRenderSystemVk::idRenderSystemVk() : idRenderSystemLocal()
{
	renderBackend = new idRenderBackendVk();
}

idRenderSystemVk::~idRenderSystemVk() {
}

void idRenderSystemVk::RenderCommandBuffers(const emptyCommand_t * const cmdHead) {
	bool	hasView = false;
	for ( const emptyCommand_t * cmd = cmdHead ; cmd ; cmd = (const emptyCommand_t *)cmd->next ) {
		if ( cmd->commandId == RC_DRAW_VIEW_3D || cmd->commandId == RC_DRAW_VIEW_GUI ) {
			hasView = true;
			break;
		}
	}

	if ( !hasView ) {
		return;
	}

	if (!r_skipBackEnd.GetBool()) {
		int c_draw3d = 0;
		int c_draw2d = 0;
		int c_setBuffers = 0;
		int c_copyRenders = 0;

		resolutionScale.SetCurrentGPUFrameTime( commonLocal.GetRendererGPUMicroseconds() );

		renderLog.StartFrame();

		const emptyCommand_t* cmds = cmdHead;
		if ( cmds->commandId == RC_NOP && !cmds->next ) {
			return;
		}

		uint64 backEndStartTime = Sys_Microseconds();
		Vk_StartFrameTimeCounter();

		// needed for editor rendering
		renderSystem->SetDefaultState();

		for ( ; cmds != NULL; cmds = (const emptyCommand_t *)cmds->next ) {
			switch ( cmds->commandId ) {
			case RC_NOP:
				break;
			case RC_DRAW_VIEW_3D:
			case RC_DRAW_VIEW_GUI:
				renderBackend->DrawView(cmds, 0);
				if ( ((const drawSurfsCommand_t *)cmds)->viewDef->viewEntitys ) {
					c_draw3d++;
				} else {
					c_draw2d++;
				}
				break;
			case RC_SET_BUFFER:
				c_setBuffers++;
				break;
			case RC_COPY_RENDER:
				renderBackend->CopyRender(cmds);
				c_copyRenders++;
				break;
			case RC_POST_PROCESS:
				renderBackend->PostProcess(cmds);
				break;
			default:
				common->Error( "RB_ExecuteBackEndCommands: bad commandId" );
				break;
			}
		}

		// stop rendering on this thread
		uint64 backEndFinishTime = Sys_Microseconds();
		backEnd.pc.totalMicroSec = backEndFinishTime - backEndStartTime;

		Vk_EndFrameTimeCounter();

		if ( r_debugRenderToTexture.GetInteger() == 1 ) {
			common->Printf( "3d: %i, 2d: %i, SetBuf: %i, CpyRenders: %i, CpyFrameBuf: %i\n", c_draw3d, c_draw2d, c_setBuffers, c_copyRenders, backEnd.pc.c_copyFrameBuffer );
			backEnd.pc.c_copyFrameBuffer = 0;
		}
		renderLog.EndFrame();
	}

	resolutionScale.InitForMap(nullptr);
}

void idRenderSystemVk::SwapCommandBuffers_FinishRendering( 
												uint64 * frontEndMicroSec,
												uint64 * backEndMicroSec,
												uint64 * shadowMicroSec,
												uint64 * gpuMicroSec )  {
	VkCommandBuffer cmd = Vk_CurrentBackendCommandBuffer();
	if (cmd != VK_NULL_HANDLE)
	{
		geoBufferSet_t gbs = vertexCache->frameData[vertexCache->listNum];
		static_cast<idBufferObjectVk*>(gbs.vertexBuffer)->Sync(cmd);
		static_cast<idBufferObjectVk*>(gbs.indexBuffer)->Sync(cmd);
		static_cast<idBufferObjectVk*>(gbs.jointBuffer)->Sync(cmd);

		gbs = vertexCache->staticData;
		static_cast<idBufferObjectVk*>(gbs.vertexBuffer)->Sync(cmd);
		static_cast<idBufferObjectVk*>(gbs.indexBuffer)->Sync(cmd);
	}

	Vk_EndRenderPass();
	Vk_EndFrame();

	if (gpuMicroSec)
		*gpuMicroSec = Vk_GetFrameTimeCounter();

	//Need to do this here to avoid invalidating the command buffers
	for (int i = 0; i < purgeQueue.Num(); ++i)
	{
		purgeQueue[i]->ActuallyPurgeImage();
	}

	purgeQueue.Clear();

	Vk_FlipPresent();
}

const emptyCommand_t * idRenderSystemVk::SwapCommandBuffers_FinishCommandBuffers() {
	const emptyCommand_t* cmd = idRenderSystemLocal::SwapCommandBuffers_FinishCommandBuffers();

	Vk_StartFrame();
	Vk_StartRenderPass();
	Vk_ClearAttachments(VK_IMAGE_ASPECT_COLOR_BIT);

	renderProgManager->BeginFrame();

	return cmd;
}


void idRenderSystemVk::CaptureRenderToImage( const char *imageName, bool clearColorAfterCopy ) {
	idRenderSystemLocal::CaptureRenderToImage(imageName, clearColorAfterCopy);
}


void idRenderSystemVk::CaptureRenderToFile( const char *fileName, bool fixAlpha ) {
}

void idRenderSystemVk::TakeScreenshot(int width, int height, const char *fileName, int blends, renderView_t *ref) {
}


void idRenderSystemVk::Clear() {
}

void idRenderSystemVk::Shutdown() {
	common->Printf( "idRenderSystem::Shutdown()\n" );

	fonts.DeleteContents();

	if ( R_IsInitialized() ) {
		globalImages->PurgeAllImages();
	}

	for (int i = 0; i < purgeQueue.Num(); ++i)
	{
		purgeQueue[i]->ActuallyPurgeImage();
	}

	purgeQueue.Clear();


	renderModelManager->Shutdown();

	idCinematic::ShutdownCinematic( );

	globalImages->Shutdown();
	
	// free frame memory
	R_ShutdownFrameData();

	// free the vertex cache, which should have nothing allocated now
	vertexCache->Shutdown();
	delete vertexCache;

	RB_ShutdownDebugTools();

	delete guiModel;

	parallelJobManager->FreeJobList( frontEndJobList );

	Clear();

	ShutdownRenderBackend();
}

void idRenderSystemVk::InitRenderBackend() {
	if (!R_IsInitialized()) {
		R_InitVulkan();

		globalImages->ReloadImages(true);
	}
}

void idRenderSystemVk::ShutdownRenderBackend() {
	R_ShutdownFrameData();
	VkImp_Shutdown();
}

stereo3DMode_t idRenderSystemVk::GetStereo3DMode() const {
	return STEREO3D_OFF;
}

bool idRenderSystemVk::IsStereoScopicRenderingSupported() const {
	return false;
}

bool idRenderSystemVk::HasQuadBufferSupport() const {
	return false;
}

stereo3DMode_t idRenderSystemVk::GetStereoScopicRenderingMode() const {
	return STEREO3D_OFF;
}

void idRenderSystemVk::EnableStereoScopicRendering( const stereo3DMode_t mode ) const {
}

float idRenderSystemVk::GetPhysicalScreenWidthInCentimeters() const {
	return 0.0f;
}

void idRenderSystemVk::QueueImagePurge(idImage* image)
{
	purgeQueue.Append(image);
}

void idRenderSystemVk::PopulateAAOptions(idList<int>& aaOptions)
{
	const VkSampleCountFlagBits msaa = Vk_MaxSupportedSampleCount();

	aaOptions.Append(0);

	if(msaa >= VK_SAMPLE_COUNT_2_BIT)
		aaOptions.Append(2);

	if(msaa >= VK_SAMPLE_COUNT_4_BIT)
		aaOptions.Append(4);

	if(msaa >= VK_SAMPLE_COUNT_8_BIT)
		aaOptions.Append(8);

	if (msaa >= VK_SAMPLE_COUNT_16_BIT)
		aaOptions.Append(16);

	if (msaa >= VK_SAMPLE_COUNT_32_BIT)
		aaOptions.Append(32);

	if (msaa >= VK_SAMPLE_COUNT_64_BIT)
		aaOptions.Append(64);
}

#endif
