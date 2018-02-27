#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xy, texcoord.w ); }

layout(set = 0, binding = 0) uniform UBO {
	vec4 _va_[18];
};

float dot3 (vec3 a , vec3 b ) {return dot ( a , b ) ; }
float dot3 (vec3 a , vec4 b ) {return dot ( a , b. xyz ) ; }
float dot3 (vec4 a , vec3 b ) {return dot ( a. xyz , b ) ; }
float dot3 (vec4 a , vec4 b ) {return dot ( a. xyz , b. xyz ) ; }
float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }
vec4 swizzleColor (vec4 c ) {return c ; }
layout(set = 1, binding = 0) readonly buffer matrixbuffer {
	vec4 matrices[408];
};

layout(location = 0) in vec4 in_Position;
layout(location = 1) in mediump vec2 in_TexCoord;
layout(location = 2) in lowp vec4 in_Normal;
layout(location = 3) in lowp vec4 in_Tangent;
layout(location = 4) in lowp vec4 in_Color;
layout(location = 5) in lowp vec4 in_Color2;

layout(location = 0) out vec4 out_Position;
layout(location = 1) out vec4 vofi_TexCoord1;
layout(location = 2) out vec4 vofi_TexCoord2;
layout(location = 3) out vec4 vofi_TexCoord3;
layout(location = 4) out vec4 vofi_TexCoord4;
layout(location = 5) out vec4 vofi_TexCoord5;
layout(location = 6) out vec4 vofi_TexCoord6;
layout(location = 7) out vec4 frontColor;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() {
	vec4 vNormal = in_Normal * 2.0 - 1.0 ;
	vec4 vTangent = in_Tangent * 2.0 - 1.0 ;
	vec3 vBinormal = cross ( vNormal. xyz , vTangent. xyz ) * vTangent. w ;
	float w0 = in_Color2 . x ;
	float w1 = in_Color2 . y ;
	float w2 = in_Color2 . z ;
	float w3 = in_Color2 . w ;
	vec4 matX , matY , matZ ;
	float joint = in_Color . x * 255.1 * 3 ;
	matX = matrices [ int ( joint + 0 ) ] * w0 ;
	matY = matrices [ int ( joint + 1 ) ] * w0 ;
	matZ = matrices [ int ( joint + 2 ) ] * w0 ;
	joint = in_Color . y * 255.1 * 3 ;
	matX += matrices [ int ( joint + 0 ) ] * w1 ;
	matY += matrices [ int ( joint + 1 ) ] * w1 ;
	matZ += matrices [ int ( joint + 2 ) ] * w1 ;
	joint = in_Color . z * 255.1 * 3 ;
	matX += matrices [ int ( joint + 0 ) ] * w2 ;
	matY += matrices [ int ( joint + 1 ) ] * w2 ;
	matZ += matrices [ int ( joint + 2 ) ] * w2 ;
	joint = in_Color . w * 255.1 * 3 ;
	matX += matrices [ int ( joint + 0 ) ] * w3 ;
	matY += matrices [ int ( joint + 1 ) ] * w3 ;
	matZ += matrices [ int ( joint + 2 ) ] * w3 ;
	vec3 normal ;
	normal. x = dot3 ( matX , vNormal ) ;
	normal. y = dot3 ( matY , vNormal ) ;
	normal. z = dot3 ( matZ , vNormal ) ;
	normal = normalize ( normal ) ;
	vec3 tangent ;
	tangent. x = dot3 ( matX , vTangent ) ;
	tangent. y = dot3 ( matY , vTangent ) ;
	tangent. z = dot3 ( matZ , vTangent ) ;
	tangent = normalize ( tangent ) ;
	vec3 binormal ;
	binormal. x = dot3 ( matX , vBinormal ) ;
	binormal. y = dot3 ( matY , vBinormal ) ;
	binormal. z = dot3 ( matZ , vBinormal ) ;
	binormal = normalize ( binormal ) ;
	vec4 modelPosition ;
	modelPosition. x = dot4 ( matX , in_Position ) ;
	modelPosition. y = dot4 ( matY , in_Position ) ;
	modelPosition. z = dot4 ( matZ , in_Position ) ;
	modelPosition. w = 1.0 ;
	out_Position . x = dot4 ( modelPosition , _va_[14 /* rpMVPmatrixX */] ) ;
	out_Position . y = dot4 ( modelPosition , _va_[15 /* rpMVPmatrixY */] ) ;
	out_Position . z = dot4 ( modelPosition , _va_[16 /* rpMVPmatrixZ */] ) ;
	out_Position . w = dot4 ( modelPosition , _va_[17 /* rpMVPmatrixW */] ) ;
	vec4 defaultTexCoord = vec4 ( 0.0 , 0.5 , 0.0 , 1.0 ) ;
	vec4 toLight = _va_[0 /* rpLocalLightOrigin */] - modelPosition ;
	vofi_TexCoord1 = defaultTexCoord ;
	vofi_TexCoord1 . x = dot4 ( in_TexCoord . xy , _va_[6 /* rpBumpMatrixS */] ) ;
	vofi_TexCoord1 . y = dot4 ( in_TexCoord . xy , _va_[7 /* rpBumpMatrixT */] ) ;
	vofi_TexCoord2 = defaultTexCoord ;
	vofi_TexCoord2 . x = dot4 ( modelPosition , _va_[5 /* rpLightFalloffS */] ) ;
	vofi_TexCoord3 . x = dot4 ( modelPosition , _va_[2 /* rpLightProjectionS */] ) ;
	vofi_TexCoord3 . y = dot4 ( modelPosition , _va_[3 /* rpLightProjectionT */] ) ;
	vofi_TexCoord3 . z = 0.0 ;
	vofi_TexCoord3 . w = dot4 ( modelPosition , _va_[4 /* rpLightProjectionQ */] ) ;
	vofi_TexCoord4 = defaultTexCoord ;
	vofi_TexCoord4 . x = dot4 ( in_TexCoord . xy , _va_[8 /* rpDiffuseMatrixS */] ) ;
	vofi_TexCoord4 . y = dot4 ( in_TexCoord . xy , _va_[9 /* rpDiffuseMatrixT */] ) ;
	vofi_TexCoord5 = defaultTexCoord ;
	vofi_TexCoord5 . x = dot4 ( in_TexCoord . xy , _va_[10 /* rpSpecularMatrixS */] ) ;
	vofi_TexCoord5 . y = dot4 ( in_TexCoord . xy , _va_[11 /* rpSpecularMatrixT */] ) ;
	toLight = normalize ( toLight ) ;
	vec4 toView = normalize ( _va_[1 /* rpLocalViewOrigin */] - modelPosition ) ;
	vec4 halfAngleVector = toLight + toView ;
	vofi_TexCoord6 . x = dot3 ( tangent , halfAngleVector ) ;
	vofi_TexCoord6 . y = dot3 ( binormal , halfAngleVector ) ;
	vofi_TexCoord6 . z = dot3 ( normal , halfAngleVector ) ;
	vofi_TexCoord6 . w = 1.0 ;
	frontColor = ( swizzleColor ( in_Color ) * _va_[12 /* rpVertexColorModulate */] ) + _va_[13 /* rpVertexColorAdd */] ;
	gl_Position = out_Position;
}
