#pragma hdrstop
#include "../idlib/precompiled.h"

// For GLSL we need to have the names for the renderparms so we can look up their run time indices within the renderprograms
extern const char* GLSLParmNames[];
const char * GLSLParmNames[] = {
	"rpScreenCorrectionFactor",
	"rpWindowCoord",
	"rpDiffuseModifier",
	"rpSpecularModifier",

	"rpLocalLightOrigin",
	"rpLocalViewOrigin",

	"rpLightProjectionS",
	"rpLightProjectionT",
	"rpLightProjectionQ",
	"rpLightFalloffS",

	"rpBumpMatrixS",
	"rpBumpMatrixT",

	"rpDiffuseMatrixS",
	"rpDiffuseMatrixT",

	"rpSpecularMatrixS",
	"rpSpecularMatrixT",

	"rpVertexColorModulate",
	"rpVertexColorAdd",

	"rpColor",
	"rpViewOrigin",
	"rpGlobalEyePos",

	"rpMVPmatrixX",
	"rpMVPmatrixY",
	"rpMVPmatrixZ",
	"rpMVPmatrixW",

	"rpModelMatrixX",
	"rpModelMatrixY",
	"rpModelMatrixZ",
	"rpModelMatrixW",

	"rpProjectionMatrixX",
	"rpProjectionMatrixY",
	"rpProjectionMatrixZ",
	"rpProjectionMatrixW",

	"rpModelViewMatrixX",
	"rpModelViewMatrixY",
	"rpModelViewMatrixZ",
	"rpModelViewMatrixW",

	"rpTextureMatrixS",
	"rpTextureMatrixT",

	"rpTexGen0S",
	"rpTexGen0T",
	"rpTexGen0Q",
	"rpTexGen0Enabled",

	"rpTexGen1S",
	"rpTexGen1T",
	"rpTexGen1Q",
	"rpTexGen1Enabled",

	"rpWobbleSkyX",
	"rpWobbleSkyY",
	"rpWobbleSkyZ",

	"rpOverbright",
	"rpEnableSkinning",
	"rpAlphaTest"
};

