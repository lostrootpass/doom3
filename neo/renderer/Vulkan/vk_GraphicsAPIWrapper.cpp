
#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "../tr_local.h"

#ifdef DOOM3_VULKAN

void GL_State(uint64 stateBits, bool forceGlState) {
	backEnd.glState.glStateBits = stateBits;
}

#endif