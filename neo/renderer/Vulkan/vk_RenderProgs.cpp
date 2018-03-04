#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "../tr_local.h"

#ifdef DOOM3_VULKAN

/**********************************************
				Vulkan
**********************************************/

const int UNIFORM_BUFFER_SIZE = 65536 * 16;

extern idCVar r_skipStripDeadCode;
extern idCVar r_useUniformArrays;

extern const char* GLSLParmNames[];

/*
================================================================================================
idRenderProgManagerVk::KillAllShaders()
================================================================================================
*/
void idRenderProgManagerVk::KillAllShaders() {
	Unbind();

	DestroyPipelines();

	for (int i = 0; i < vertexShaders.Num(); ++i)
	{
		vkDestroyShaderModule(Vk_GetDevice(), vertexShaders[i].progId, nullptr);
	}

	for (int i = 0; i < fragmentShaders.Num(); ++i)
	{
		vkDestroyShaderModule(Vk_GetDevice(), fragmentShaders[i].progId, nullptr);
	}
}

/*
================================================================================================
idRenderProgManagerVk::idRenderProgManagerVk
================================================================================================
*/
idRenderProgManagerVk::idRenderProgManagerVk() : idRenderProgManager(),
currentVertOffset(0), currentFragOffset(0), nextVertOffset(0), uniformPtr(nullptr) {

}

/*
================================================================================================
idRenderProgManagerVk::~idRenderProgManagerVk
================================================================================================
*/
idRenderProgManagerVk::~idRenderProgManagerVk() {

}

void idRenderProgManagerVk::BindShader(int vIndex, int fIndex) {
	if ( currentVertexShader == vIndex && currentFragmentShader == fIndex ) {
		return;
	}
	currentVertexShader = vIndex;
	currentFragmentShader = fIndex;

	// vIndex denotes the GLSL program
	if ( vIndex >= 0 && vIndex < glslPrograms.Num() ) {
		currentRenderProgram = vIndex;
		RENDERLOG_PRINTF( "Binding GLSL Program %s\n", glslPrograms[vIndex].name.c_str() );
	}
}

void idRenderProgManagerVk::Unbind() {
	currentVertexShader = -1;
	currentFragmentShader = -1;
	currentRenderProgram = INVALID_PROGID;
}

const char* idRenderProgManagerVk::GetParmName(int rp) const {
	if ( rp >= RENDERPARM_USER ) {
		int userParmIndex = rp - RENDERPARM_USER;
		return va("rpUser%d", userParmIndex );
	}
	assert( rp < RENDERPARM_TOTAL );
	return GLSLParmNames[ rp ];
}

void idRenderProgManagerVk::SetUniformValue(const renderParm_t rp, const float * value) {
	for ( int i = 0; i < 4; i++ ) {
		glslUniforms[rp][i] = value[i];
	}
}

void idRenderProgManagerVk::CommitUniforms() {
	ALIGNTYPE16 idVec4 localVectors[RENDERPARM_USER + MAX_GLSL_USER_PARMS];
	
	currentVertOffset = nextVertOffset;
	
	const glslProgram_t & prog = glslPrograms[currentRenderProgram];
	
	size_t thisUniform = 0;
	const idList<int> & vertexUniforms = vertexShaders[prog.vertexShaderIndex].uniforms;
	if (vertexUniforms.Num())
	{
		for (int i = 0; i < vertexUniforms.Num(); ++i)
		{
			localVectors[i] = glslUniforms[vertexUniforms[i]];
		}


		thisUniform = vertexUniforms.Num() * sizeof(idVec4);
		memcpy(uniformPtr, localVectors->ToFloatPtr(), thisUniform);

		thisUniform = (thisUniform + 0x100) & -0x100;
		uniformPtr = (char*)uniformPtr + thisUniform;
	}

	currentFragOffset = currentVertOffset + thisUniform;
	thisUniform = 0;

	//We're always guaranteed to have a vertex shader, but shadow passes don't use fragshaders
	if (prog.fragmentShaderIndex > -1)
	{
		const idList<int> & fragmentUniforms = fragmentShaders[prog.fragmentShaderIndex].uniforms;
		if (fragmentUniforms.Num())
		{
			for (int i = 0; i < fragmentUniforms.Num(); ++i)
			{
				localVectors[i] = glslUniforms[fragmentUniforms[i]];
			}


			thisUniform = fragmentUniforms.Num() * sizeof(idVec4);
			memcpy(uniformPtr, localVectors->ToFloatPtr(), thisUniform);

			thisUniform = (thisUniform + 0x100) & -0x100;
			uniformPtr = (char*)uniformPtr + thisUniform;
		}
	}
	
	nextVertOffset = currentFragOffset + thisUniform;

	assert(nextVertOffset < UNIFORM_BUFFER_SIZE);
}

int idRenderProgManagerVk::FindProgram(const char* name, int vIndex, int fIndex) {
	for ( int i = 0; i < glslPrograms.Num(); ++i ) {
		if ( ( glslPrograms[i].vertexShaderIndex == vIndex ) && ( glslPrograms[i].fragmentShaderIndex == fIndex ) ) {
			LoadProgram( i, vIndex, fIndex );
			return i;
		}
	}

	glslProgram_t program;
	program.name = name;
	int index = glslPrograms.Append( program );
	LoadProgram( index, vIndex, fIndex );
	return index;
}

void idRenderProgManagerVk::ZeroUniforms() {
	memset( glslUniforms.Ptr(), 0, glslUniforms.Allocated() );
}

void idRenderProgManagerVk::LoadVertexShader(int index) {
	if ( vertexShaders[index].progId != INVALID_PROGID ) {
		return; // Already loaded
	}

	vertexShaders[index].progId = ( GLuint ) LoadGLSLShader( GL_VERTEX_SHADER, vertexShaders[index].name, vertexShaders[index].uniforms );
}

void idRenderProgManagerVk::LoadFragmentShader(int index) {
	if ( fragmentShaders[index].progId != INVALID_PROGID ) {
		return; // Already loaded
	}

	fragmentShaders[index].progId = ( GLuint ) LoadGLSLShader( GL_FRAGMENT_SHADER, fragmentShaders[index].name, fragmentShaders[index].uniforms );
}

GLuint idRenderProgManagerVk::LoadShader(GLenum target, const char * name, const char * startToken) {
	idStr fullPath = "renderprogs\\vk\\";
	fullPath += name;

	common->Printf("%s", fullPath.c_str());

	char* fileBuffer = nullptr;
	fileSystem->ReadFile(fullPath.c_str(), (void**)&fileBuffer, NULL);
	if ( fileBuffer == NULL ) {
		common->Printf( ": File not found\n" );
		return INVALID_PROGID;
	}
	if ( !R_IsInitialized() ) {
		common->Printf( ": Renderer not initialized\n" );
		fileSystem->FreeFile( fileBuffer );
		return INVALID_PROGID;
	}

	return INVALID_PROGID;
}

bool idRenderProgManagerVk::Compile(GLenum target, const char * name) {
	return false;
}

GLuint idRenderProgManagerVk::LoadGLSLShader(GLenum target, const char * name, idList<int> & uniforms) {
#if 0
{
	//used to read in the shipped versions of GLSL shaders and save them
	idStr outFileGLSL;
	outFileGLSL.Format( "renderprogs\\glsl\\%s", name );
	outFileGLSL.StripFileExtension();
	if (target == GL_FRAGMENT_SHADER) {
		outFileGLSL += "_fragment.glsl";
	}
	else {
		outFileGLSL += "_vertex.glsl";
	}

	idStr programGLSL;
	idStr programUniforms;

	void * fileBufferGLSL = NULL;
	int lengthGLSL = fileSystem->ReadFile(outFileGLSL.c_str(), &fileBufferGLSL);
	if (lengthGLSL <= 0) {
		idLib::Error("GLSL file %s could not be loaded and may be corrupt", outFileGLSL.c_str());
	}
	programGLSL = (const char *)fileBufferGLSL;
	Mem_Free(fileBufferGLSL);

	fileSystem->WriteFile(outFileGLSL, programGLSL.c_str(), programGLSL.Length(), "fs_basepath");
}

#endif

	idStr vkFile, outFileUniforms, programUniforms;
	outFileUniforms.Format( "renderprogs\\glsl\\%s", name );
	outFileUniforms.StripFileExtension();
	vkFile.Format("renderprogs\\vk\\spv\\%s", name);
	vkFile.StripFileExtension();
	if (target == GL_FRAGMENT_SHADER) {
		vkFile += ".frag.spv";
		outFileUniforms += "_fragment.uniforms";
	} else {
		vkFile += ".vert.spv";
		outFileUniforms += "_vertex.uniforms";
	}
	
	void* fileBuffer = nullptr;
	int vkLength = fileSystem->ReadFile(vkFile.c_str(), &fileBuffer);

	if (vkLength == -1)
		return INVALID_PROGID;

	VkShaderModule module = Vk_CreateShaderModule((const char*)fileBuffer, vkLength);

	Mem_Free(fileBuffer);

	if ( r_useUniformArrays.GetBool() ) {
		// read in the uniform file
		void * fileBufferUniforms = NULL;
		int lengthUniforms = fileSystem->ReadFile( outFileUniforms.c_str(), &fileBufferUniforms );
		if ( lengthUniforms <= 0 ) {
			idLib::Error( "uniform file %s could not be loaded and may be corrupt", outFileUniforms.c_str() );
		}
		
		programUniforms = ( const char* ) fileBufferUniforms;

		Mem_Free( fileBufferUniforms );
	}

	// find the uniforms locations in either the vertex or fragment uniform array
	if ( r_useUniformArrays.GetBool() ) {
		uniforms.Clear();

		idLexer src( programUniforms, programUniforms.Length(), "uniforms" );
		idToken token;
		while ( src.ReadToken( &token ) ) {
			int index = -1;
			for ( int i = 0; i < RENDERPARM_TOTAL && index == -1; i++ ) {
				const char * parmName = GetParmName( i );
				if ( token == parmName ) {
					index = i;
				}
			}
			for ( int i = 0; i < MAX_GLSL_USER_PARMS && index == -1; i++ ) {
				const char * parmName = GetParmName( RENDERPARM_USER + i );
				if ( token == parmName ) {
					index = RENDERPARM_USER + i;
				}
			}
			if ( index == -1 ) {
				//idLib::Error( "couldn't find uniform %s for %s", token.c_str(), outFileGLSL.c_str() );
			}
			uniforms.Append( index );
		}
	}

	return module;
}

void idRenderProgManagerVk::LoadProgram(const int programIndex, const int vertexShaderIndex, const int fragmentShaderIndex) {
	glslProgram_t & prog = glslPrograms[programIndex];

	if (prog.progId != INVALID_PROGID) {
		return; // Already loaded
	}


	//These are going to be set to INVALD_PROGID which is >0
	//so the other commitUniform etc. checks are going to false-positive
	GLuint vertexProgID = ( vertexShaderIndex != -1 ) ? vertexShaders[ vertexShaderIndex ].progId : INVALID_PROGID;
	GLuint fragmentProgID = ( fragmentShaderIndex != -1 ) ? fragmentShaders[ fragmentShaderIndex ].progId : INVALID_PROGID;

	idStr programName = vertexShaders[ vertexShaderIndex ].name;
	programName.StripFileExtension();
	prog.name = programName;
	prog.progId = programIndex;
	prog.fragmentShaderIndex = fragmentShaderIndex;
	prog.vertexShaderIndex = vertexShaderIndex;
	prog.fragmentUniformArray = uniformBuffer;
	prog.vertexUniformArray = uniformBuffer;
}

static VkStencilOp StencilFailOp(uint64_t stateBits)
{
	VkStencilOp sFail = VK_STENCIL_OP_KEEP;

	switch ( stateBits & GLS_STENCIL_OP_FAIL_BITS ) {
		case GLS_STENCIL_OP_FAIL_KEEP:		sFail = VK_STENCIL_OP_KEEP; break;
		case GLS_STENCIL_OP_FAIL_ZERO:		sFail = VK_STENCIL_OP_ZERO; break;
		case GLS_STENCIL_OP_FAIL_REPLACE:	sFail = VK_STENCIL_OP_REPLACE; break;
		case GLS_STENCIL_OP_FAIL_INCR:		sFail = VK_STENCIL_OP_INCREMENT_AND_CLAMP; break;
		case GLS_STENCIL_OP_FAIL_DECR:		sFail = VK_STENCIL_OP_DECREMENT_AND_CLAMP; break;
		case GLS_STENCIL_OP_FAIL_INVERT:	sFail = VK_STENCIL_OP_INVERT; break;
		case GLS_STENCIL_OP_FAIL_INCR_WRAP: sFail = VK_STENCIL_OP_INCREMENT_AND_WRAP; break;
		case GLS_STENCIL_OP_FAIL_DECR_WRAP: sFail = VK_STENCIL_OP_DECREMENT_AND_WRAP; break;
	}

	return sFail;
}

static VkStencilOp DepthFailOp(uint64_t stateBits)
{
	VkStencilOp zFail = VK_STENCIL_OP_KEEP;

	switch ( stateBits & GLS_STENCIL_OP_ZFAIL_BITS ) {
		case GLS_STENCIL_OP_ZFAIL_KEEP:		zFail = VK_STENCIL_OP_KEEP; break;
		case GLS_STENCIL_OP_ZFAIL_ZERO:		zFail = VK_STENCIL_OP_ZERO; break;
		case GLS_STENCIL_OP_ZFAIL_REPLACE:	zFail = VK_STENCIL_OP_REPLACE; break;
		case GLS_STENCIL_OP_ZFAIL_INCR:		zFail = VK_STENCIL_OP_INCREMENT_AND_CLAMP; break;
		case GLS_STENCIL_OP_ZFAIL_DECR:		zFail = VK_STENCIL_OP_DECREMENT_AND_CLAMP; break;
		case GLS_STENCIL_OP_ZFAIL_INVERT:	zFail = VK_STENCIL_OP_INVERT; break;
		case GLS_STENCIL_OP_ZFAIL_INCR_WRAP:zFail = VK_STENCIL_OP_INCREMENT_AND_WRAP; break;
		case GLS_STENCIL_OP_ZFAIL_DECR_WRAP:zFail = VK_STENCIL_OP_DECREMENT_AND_WRAP; break;
	}

	return zFail;
}

static VkStencilOp StencilPassOp(uint64_t stateBits)
{
	VkStencilOp pass = VK_STENCIL_OP_KEEP;

	switch ( stateBits & GLS_STENCIL_OP_PASS_BITS ) {
		case GLS_STENCIL_OP_PASS_KEEP:		pass = VK_STENCIL_OP_KEEP; break;
		case GLS_STENCIL_OP_PASS_ZERO:		pass = VK_STENCIL_OP_ZERO; break;
		case GLS_STENCIL_OP_PASS_REPLACE:	pass = VK_STENCIL_OP_REPLACE; break;
		case GLS_STENCIL_OP_PASS_INCR:		pass = VK_STENCIL_OP_INCREMENT_AND_CLAMP; break;
		case GLS_STENCIL_OP_PASS_DECR:		pass = VK_STENCIL_OP_DECREMENT_AND_CLAMP; break;
		case GLS_STENCIL_OP_PASS_INVERT:	pass = VK_STENCIL_OP_INVERT; break;
		case GLS_STENCIL_OP_PASS_INCR_WRAP:	pass = VK_STENCIL_OP_INCREMENT_AND_WRAP; break;
		case GLS_STENCIL_OP_PASS_DECR_WRAP:	pass = VK_STENCIL_OP_DECREMENT_AND_WRAP; break;
	}

	return pass;
}

void idRenderProgManagerVk::Init()
{
	idRenderProgManager::Init();

	totalUniformCount = 0;
	size_t bufferSize = 0;
	size_t vertexSize = 0;
	for (int i = 0; i < vertexShaders.Num(); ++i)
	{
		if(vertexShaders[i].uniforms.Num())
		{
			totalUniformCount += vertexShaders[i].uniforms.Num();
			bufferSize += ((vertexShaders[i].uniforms.Num() * sizeof(idVec4)) + 0x100) & -0x100;
		}
	}

	vertexSize = bufferSize;

	for (int i = 0; i < fragmentShaders.Num(); ++i)
	{
		if(fragmentShaders[i].uniforms.Num())
		{
			totalUniformCount += fragmentShaders[i].uniforms.Num();
			bufferSize += ((fragmentShaders[i].uniforms.Num() * sizeof(idVec4)) + 0x100) & -0x100;
		}
	}

	Vk_CreateUniformBuffer(uniformStagingMemory, uniformStagingBuffer, 
		uniformMemory, uniformBuffer, UNIFORM_BUFFER_SIZE);

	VkDescriptorBufferInfo bufferInfo[2];
	bufferInfo[0] = {};
	bufferInfo[0].buffer = uniformBuffer;
	bufferInfo[0].offset = 0;
	bufferInfo[0].range = VK_WHOLE_SIZE;

	bufferInfo[1] = {};
	bufferInfo[1].buffer = uniformBuffer;
	bufferInfo[1].offset = 0;
	bufferInfo[1].range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet write[2];
	write[0] = {};
	write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write[0].descriptorCount = 1;
	write[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	write[0].dstBinding = 0;
	write[0].pBufferInfo = &bufferInfo[0];
	write[0].dstSet = Vk_UniformDescriptorSet();

	write[1] = {};
	write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write[1].descriptorCount = 1;
	write[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	write[1].dstBinding = 1;
	write[1].pBufferInfo = &bufferInfo[1];
	write[1].dstSet = Vk_UniformDescriptorSet();
	
	vkUpdateDescriptorSets(Vk_GetDevice(), 2, write, 0, 0);
}

VkPipeline idRenderProgManagerVk::GetPipelineForState(uint64 stateBits)
{
    if(currentRenderProgram == INVALID_PROGID) return VK_NULL_HANDLE;

	const glslProgram_t& prog = glslPrograms[currentRenderProgram];
	if (prog.progId == INVALID_PROGID ||
	vertexShaders[prog.vertexShaderIndex].progId == INVALID_PROGID)
	{
		return VK_NULL_HANDLE;
	}

	for (CachedPipeline p : pipelines)
	{
		if (p.progId == currentRenderProgram && p.stateBits == stateBits 
			&& p.cullType == backEnd.glState.faceCulling
			&& p.isMirror == backEnd.viewDef->isMirror)
		{
			if(backEnd.glState.vertexLayout == LAYOUT_DRAW_VERT)
				return p.pipeline;
			else
			{
				if (p.stencilBack == backEnd.glState.shadowStencilBack &&
					p.stencilFront == backEnd.glState.shadowStencilFront)
				{
					return p.pipeline;
				}
			}
		}
	}

	//
	// check depthFunc bits
	//
	VkCompareOp depthCompareOp;
	switch ( stateBits & GLS_DEPTHFUNC_BITS ) {
	case GLS_DEPTHFUNC_EQUAL:	depthCompareOp = VK_COMPARE_OP_EQUAL; break;
	case GLS_DEPTHFUNC_ALWAYS:	depthCompareOp = VK_COMPARE_OP_ALWAYS; break;
	case GLS_DEPTHFUNC_LESS: depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
	case GLS_DEPTHFUNC_GREATER:	depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
	}
	
	//
	// check blend bits
	//
	VkBlendFactor srcFactor = VK_BLEND_FACTOR_ONE;
	VkBlendFactor dstFactor = VK_BLEND_FACTOR_ZERO;

	switch ( stateBits & GLS_SRCBLEND_BITS ) {
		case GLS_SRCBLEND_ZERO:					srcFactor = VK_BLEND_FACTOR_ZERO; break;
		case GLS_SRCBLEND_ONE:					srcFactor = VK_BLEND_FACTOR_ONE; break;
		case GLS_SRCBLEND_DST_COLOR:			srcFactor = VK_BLEND_FACTOR_DST_COLOR; break;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	srcFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR; break;
		case GLS_SRCBLEND_SRC_ALPHA:			srcFactor = VK_BLEND_FACTOR_SRC_ALPHA; break;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	srcFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
		case GLS_SRCBLEND_DST_ALPHA:			srcFactor = VK_BLEND_FACTOR_DST_ALPHA; break;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	srcFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; break;
		default:
			assert( !"VK_BLEND_FACTOR_State: invalid src blend state bits\n" );
			break;
	}

	switch ( stateBits & GLS_DSTBLEND_BITS ) {
		case GLS_DSTBLEND_ZERO:					dstFactor = VK_BLEND_FACTOR_ZERO; break;
		case GLS_DSTBLEND_ONE:					dstFactor = VK_BLEND_FACTOR_ONE; break;
		case GLS_DSTBLEND_SRC_COLOR:			dstFactor = VK_BLEND_FACTOR_SRC_COLOR; break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; break;
		case GLS_DSTBLEND_SRC_ALPHA:			dstFactor = VK_BLEND_FACTOR_SRC_ALPHA; break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
		case GLS_DSTBLEND_DST_ALPHA:			dstFactor = VK_BLEND_FACTOR_DST_ALPHA; break;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:  dstFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; break;
		default:
			assert( !"VK_BLEND_FACTOR_State: invalid dst blend state bits\n" );
			break;
	}

	// Only actually update GL's blend func if blending is enabled.
	VkBool32 blendEnable = VK_TRUE;
	if ( srcFactor == VK_BLEND_FACTOR_ONE && dstFactor == VK_BLEND_FACTOR_ZERO ) {
		blendEnable = VK_FALSE;
	}

	//
	// check depthmask
	//
	VkBool32 depthWriteEnable = VK_TRUE;
   	if ( stateBits & GLS_DEPTHMASK ) {
		depthWriteEnable = VK_FALSE;
   	} 

	//
	// check colormask
	//
	VkColorComponentFlags colorWriteMask = 0;
   	colorWriteMask |= ( stateBits & GLS_REDMASK ) ? 0 : VK_COLOR_COMPONENT_R_BIT;
   	colorWriteMask |= ( stateBits & GLS_GREENMASK ) ? 0 : VK_COLOR_COMPONENT_G_BIT;
   	colorWriteMask |= ( stateBits & GLS_BLUEMASK ) ? 0 : VK_COLOR_COMPONENT_B_BIT;
   	colorWriteMask |= ( stateBits & GLS_ALPHAMASK ) ? 0 : VK_COLOR_COMPONENT_A_BIT;

	//
	// fill/line mode
	//
	VkPolygonMode polygonMode;
   	if ( stateBits & GLS_POLYMODE_LINE ) {
		polygonMode = VK_POLYGON_MODE_LINE;
   	} else {
		polygonMode = VK_POLYGON_MODE_FILL;
   	}

	//
	// polygon offset
	//
	VkBool32 depthBiasEnable = VK_FALSE;
	float constantFactor = 0.0f;
	float depthSlope = 0.0f;
	if (stateBits & GLS_POLYGON_OFFSET)
	{
		depthBiasEnable = VK_TRUE;
		constantFactor = backEnd.glState.polyOfsBias;
		depthSlope = backEnd.glState.polyOfsScale;
	}

    //
	// stencil
	//
	VkBool32 stencilEnabled = VK_FALSE;
	uint32_t writeMask = 0;
	if ( ( stateBits & ( GLS_STENCIL_FUNC_BITS | GLS_STENCIL_OP_BITS ) ) != 0 ) {
		stencilEnabled = VK_TRUE;
		writeMask = 0xff;
	} 

	uint32_t ref = uint32_t( ( stateBits & GLS_STENCIL_FUNC_REF_BITS ) >> GLS_STENCIL_FUNC_REF_SHIFT );
	uint32_t mask = uint32_t( ( stateBits & GLS_STENCIL_FUNC_MASK_BITS ) >> GLS_STENCIL_FUNC_MASK_SHIFT );
	VkCompareOp func = VK_COMPARE_OP_NEVER;

	switch ( stateBits & GLS_STENCIL_FUNC_BITS ) {
		case GLS_STENCIL_FUNC_NEVER:		func = VK_COMPARE_OP_NEVER; break;
		case GLS_STENCIL_FUNC_LESS:			func = VK_COMPARE_OP_LESS; break;
		case GLS_STENCIL_FUNC_EQUAL:		func = VK_COMPARE_OP_EQUAL; break;
		case GLS_STENCIL_FUNC_LEQUAL:		func = VK_COMPARE_OP_LESS_OR_EQUAL; break;
		case GLS_STENCIL_FUNC_GREATER:		func = VK_COMPARE_OP_GREATER; break;
		case GLS_STENCIL_FUNC_NOTEQUAL:		func = VK_COMPARE_OP_NOT_EQUAL; break;
		case GLS_STENCIL_FUNC_GEQUAL:		func = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
		case GLS_STENCIL_FUNC_ALWAYS:		func = VK_COMPARE_OP_ALWAYS; break;
	}

	VkPipeline pipeline = VK_NULL_HANDLE;

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].module = vertexShaders[prog.vertexShaderIndex].progId;

	uint32_t stageCount = 1;
	if (backEnd.glState.vertexLayout == LAYOUT_DRAW_VERT || r_showShadows.GetInteger() > 0)
	{
		stageCount = 2;

		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].pName = "main";
		stages[1].module = fragmentShaders[prog.fragmentShaderIndex].progId;
	}

	VkPipelineColorBlendAttachmentState cba = {};
	cba.blendEnable = blendEnable;
	cba.colorWriteMask = colorWriteMask;
	
	cba.colorBlendOp = VK_BLEND_OP_ADD;
	cba.srcColorBlendFactor = srcFactor;
	cba.dstColorBlendFactor = dstFactor;

	cba.alphaBlendOp = VK_BLEND_OP_ADD;
	cba.srcAlphaBlendFactor = srcFactor;
	cba.dstAlphaBlendFactor = dstFactor;

	VkPipelineColorBlendStateCreateInfo cbs = {};
	cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cbs.attachmentCount = 1;
	cbs.pAttachments = &cba;
	cbs.logicOp = VK_LOGIC_OP_COPY;

	VkPipelineInputAssemblyStateCreateInfo ias = {};
	ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	ias.primitiveRestartEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo mss = {};
	mss.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	mss.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkCullModeFlags cullType = VK_CULL_MODE_NONE;
	if (backEnd.glState.faceCulling == CT_TWO_SIDED)
	{
		cullType = VK_CULL_MODE_NONE;
	}
	else
	{
		if (backEnd.glState.faceCulling == CT_BACK_SIDED)
		{
			if (backEnd.viewDef->isMirror)
				cullType = VK_CULL_MODE_FRONT_BIT;
			else
				cullType = VK_CULL_MODE_BACK_BIT;
		}
		else
		{
			if (backEnd.viewDef->isMirror)
				cullType = VK_CULL_MODE_BACK_BIT;
			else
				cullType = VK_CULL_MODE_FRONT_BIT;
		}
	}

	VkPipelineRasterizationStateCreateInfo rs = {};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.cullMode = cullType;
	rs.polygonMode = polygonMode;
	rs.lineWidth = 1.0f;
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.depthBiasEnable = depthBiasEnable;
	rs.depthBiasConstantFactor = constantFactor;
	rs.depthBiasClamp = 0.0f;
	rs.depthBiasSlopeFactor = depthSlope;

	VkVertexInputBindingDescription vbs = {};
	vbs.binding = 0;
	vbs.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	
	const int MAX_VTX_ATTRS = 6;
	size_t sz[MAX_VTX_ATTRS];
	VkFormat fmt[MAX_VTX_ATTRS];
	size_t offsets[MAX_VTX_ATTRS];
	size_t stride = 0;
	int vtxAttrCount = -1;

	switch (backEnd.glState.vertexLayout)
	{
	case LAYOUT_DRAW_VERT:
		vtxAttrCount = 6;
		stride = sizeof(idDrawVert);

		sz[0] = sizeof(idDrawVert::xyz);
		fmt[0] = VK_FORMAT_R32G32B32_SFLOAT;
		offsets[0] = DRAWVERT_XYZ_OFFSET;

		sz[1] = sizeof(idDrawVert::st);
		fmt[1] = VK_FORMAT_R16G16_SFLOAT;
		offsets[1] = DRAWVERT_ST_OFFSET;
		
		sz[2] = sizeof(idDrawVert::normal);
		fmt[2] = VK_FORMAT_R8G8B8A8_UNORM;
		offsets[2] = DRAWVERT_NORMAL_OFFSET;
		
		sz[3] = sizeof(idDrawVert::tangent);
		fmt[3] = VK_FORMAT_R8G8B8A8_UNORM;
		offsets[3] = DRAWVERT_TANGENT_OFFSET;
		
		sz[4] = sizeof(idDrawVert::color);
		fmt[4] = VK_FORMAT_R8G8B8A8_UNORM;
		offsets[4] = DRAWVERT_COLOR_OFFSET;
		
		sz[5] = sizeof(idDrawVert::color2);
		fmt[5] = VK_FORMAT_R8G8B8A8_UNORM;
		offsets[5] = DRAWVERT_COLOR2_OFFSET;
		break;

	case LAYOUT_DRAW_SHADOW_VERT:
		vtxAttrCount = 1;
		stride = sizeof(idShadowVert);

		sz[0] = sizeof(idShadowVert::xyzw);
		fmt[0] = VK_FORMAT_R32G32B32A32_SFLOAT;
		offsets[0] = SHADOWVERT_XYZW_OFFSET;
		break;

	case LAYOUT_DRAW_SHADOW_VERT_SKINNED:
		vtxAttrCount = 3;
		stride = sizeof(idShadowVertSkinned);

		sz[0] = sizeof(idShadowVertSkinned::xyzw);
		fmt[0] = VK_FORMAT_R32G32B32A32_SFLOAT;
		offsets[0] = SHADOWVERTSKINNED_XYZW_OFFSET;

		sz[1] = sizeof(idShadowVertSkinned::color);
		fmt[1] = VK_FORMAT_R8G8B8A8_UNORM;
		offsets[1] = SHADOWVERTSKINNED_COLOR_OFFSET;

		sz[2] = sizeof(idShadowVertSkinned::color2);
		fmt[2] = VK_FORMAT_R8G8B8A8_UNORM;
		offsets[2] = SHADOWVERTSKINNED_COLOR2_OFFSET;
		break;

	default:
		//will never happen.
		break;
	}

	vbs.stride = stride;
	vbs.stride = vbs.stride + 15 & -16;
	
	VkVertexInputAttributeDescription vtxAttrs[MAX_VTX_ATTRS] = {};
	for (int i = 0; i < vtxAttrCount; ++i) {
		vtxAttrs[i].binding = 0;
		vtxAttrs[i].location = i;
		vtxAttrs[i].format = fmt[i];
		vtxAttrs[i].offset = offsets[i];
	}

	VkPipelineVertexInputStateCreateInfo vis = {};
	vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vis.vertexBindingDescriptionCount = 1;
	vis.pVertexBindingDescriptions = &vbs;
	vis.vertexAttributeDescriptionCount = vtxAttrCount;
	vis.pVertexAttributeDescriptions = vtxAttrs;

	//Set dynamically.
	VkRect2D sc = {};
	VkViewport vp = {};

	VkPipelineViewportStateCreateInfo vps = {};
	vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vps.viewportCount = 1;
	vps.scissorCount = 1;
	vps.pViewports = &vp;
	vps.pScissors = &sc;

	VkStencilOpState stencilState = {};
	stencilState.compareOp = func;
	stencilState.compareMask = mask;
	stencilState.writeMask = writeMask;
	stencilState.reference = ref;

	VkPipelineDepthStencilStateCreateInfo dss = {};
	dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	dss.depthTestEnable = VK_TRUE;
	dss.depthCompareOp = depthCompareOp;
	dss.depthWriteEnable = depthWriteEnable;
	dss.stencilTestEnable = stencilEnabled;

	if (backEnd.glState.vertexLayout == LAYOUT_DRAW_SHADOW_VERT ||
		backEnd.glState.vertexLayout == LAYOUT_DRAW_SHADOW_VERT_SKINNED)
	{
		dss.depthBoundsTestEnable = VK_TRUE;
	}

	if (backEnd.glState.vertexLayout == LAYOUT_DRAW_VERT)
	{
		stencilState.depthFailOp = DepthFailOp(stateBits);
		stencilState.passOp = StencilPassOp(stateBits);
		stencilState.failOp = StencilFailOp(stateBits);

		dss.front = stencilState;
		dss.back = stencilState;
	}
	else
	{
		stencilState.depthFailOp = DepthFailOp(backEnd.glState.shadowStencilFront);
		stencilState.passOp = StencilPassOp(backEnd.glState.shadowStencilFront);
		stencilState.failOp = StencilFailOp(backEnd.glState.shadowStencilFront);
		dss.front = stencilState;

		stencilState.depthFailOp = DepthFailOp(backEnd.glState.shadowStencilBack);
		stencilState.passOp = StencilPassOp(backEnd.glState.shadowStencilBack);
		stencilState.failOp = StencilFailOp(backEnd.glState.shadowStencilBack);
		dss.back = stencilState;
	}
	
	VkDynamicState dynStates[] = { 
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		//VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
		//VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		//VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
		VK_DYNAMIC_STATE_DEPTH_BOUNDS,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};
	VkPipelineDynamicStateCreateInfo dys = {};
	dys.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dys.dynamicStateCount = 4;
	dys.pDynamicStates = dynStates;

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.stageCount = stageCount;
	info.subpass = 0;
	info.pStages = stages;
	info.pColorBlendState = &cbs;
	info.pInputAssemblyState = &ias;
	info.pMultisampleState = &mss;
	info.pRasterizationState = &rs;
	info.pVertexInputState = &vis;
	info.pViewportState = &vps;
	info.pDepthStencilState = &dss;
	info.pDynamicState = &dys;

	switch (backEnd.glState.vertexLayout)
	{
	case LAYOUT_DRAW_VERT:
		info.layout = Vk_GetPipelineLayout();
		break;

	case LAYOUT_DRAW_SHADOW_VERT:
	case LAYOUT_DRAW_SHADOW_VERT_SKINNED:
		info.layout = Vk_GetPipelineLayout();
		break;
	default:
		return VK_NULL_HANDLE;
		break;
	}

	pipeline = Vk_CreatePipeline(info);
	CachedPipeline p;
	p.stateBits = stateBits;
	p.pipeline = pipeline;
	p.progId = currentRenderProgram;
	p.cullType = backEnd.glState.faceCulling;
	p.isMirror = backEnd.viewDef->isMirror;

	if (backEnd.glState.vertexLayout == LAYOUT_DRAW_SHADOW_VERT ||
		backEnd.glState.vertexLayout == LAYOUT_DRAW_SHADOW_VERT_SKINNED)
	{
		p.stencilBack = backEnd.glState.shadowStencilBack;
		p.stencilFront = backEnd.glState.shadowStencilFront;
	}

	pipelines.push_back(p);

	return pipeline;
}

void idRenderProgManagerVk::DestroyPipelines()
{
	for (CachedPipeline p : pipelines)
	{
		vkDestroyPipeline(Vk_GetDevice(), p.pipeline, nullptr);
	}

	pipelines.clear();
}

size_t idRenderProgManagerVk::GetCurrentVertUniformOffset() const
{
	return currentVertOffset;
}

size_t idRenderProgManagerVk::GetCurrentFragUniformOffset() const
{
	return currentFragOffset;
}

void idRenderProgManagerVk::BeginFrame()
{
	uniformPtr = Vk_MapMemory(uniformStagingMemory, 0, UNIFORM_BUFFER_SIZE, 0);
	currentFragOffset = currentVertOffset = nextVertOffset = 0;
}

void idRenderProgManagerVk::EndFrame()
{
	if (uniformPtr != nullptr)
	{
		Vk_UnmapMemory(uniformStagingMemory);
		uniformPtr = nullptr;

		VkBufferCopy copy = {};
		copy.size = UNIFORM_BUFFER_SIZE;
		VkCommandBuffer b = Vk_StartOneShotCommandBuffer();
		vkCmdCopyBuffer(b, uniformStagingBuffer, uniformBuffer, 1, &copy);
		//vkCmdCopyBuffer(Vk_ActiveCommandBuffer(), uniformStagingBuffer, uniformBuffer, 1, &copy);
		Vk_SubmitOneShotCommandBuffer(b);
	}
}

#endif