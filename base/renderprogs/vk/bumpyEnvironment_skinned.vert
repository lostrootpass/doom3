#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xy, texcoord.w ); }


layout(set = 0, binding = 0) uniform UBO {
	vec4 _va_[9];
};

float dot3 (vec3 a , vec3 b ) {return dot ( a , b ) ; }
float dot3 (vec3 a , vec4 b ) {return dot ( a , b. xyz ) ; }
float dot3 (vec4 a , vec3 b ) {return dot ( a. xyz , b ) ; }
float dot3 (vec4 a , vec4 b ) {return dot ( a. xyz , b. xyz ) ; }
float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }
layout(set = 0, binding = 2) readonly buffer matrixbuffer {
	vec4 matrices[408];
};

layout(location = 0) in vec4 in_Position;
layout(location = 1) in mediump vec2 in_TexCoord;
layout(location = 2) in lowp vec4 in_Normal;
layout(location = 3) in lowp vec4 in_Tangent;
layout(location = 4) in lowp vec4 in_Color;
layout(location = 5) in lowp vec4 in_Color2;

layout(location = 0) out vec4 out_Position;
layout(location = 1) out vec2 vofi_TexCoord0;
layout(location = 2) out vec3 vofi_TexCoord1;
layout(location = 3) out vec3 vofi_TexCoord2;
layout(location = 4) out vec3 vofi_TexCoord3;
layout(location = 5) out vec3 vofi_TexCoord4;
layout(location = 6) out vec4 frontColor;

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
	out_Position . x = dot4 ( modelPosition , _va_[2 /* rpMVPmatrixX */] ) ;
	out_Position . y = dot4 ( modelPosition , _va_[3 /* rpMVPmatrixY */] ) ;
	out_Position . z = dot4 ( modelPosition , _va_[4 /* rpMVPmatrixZ */] ) ;
	out_Position . w = dot4 ( modelPosition , _va_[5 /* rpMVPmatrixW */] ) ;
	gl_Position = out_Position;
	vofi_TexCoord0 = in_TexCoord . xy ;
	vec4 toEye = _va_[0 /* rpLocalViewOrigin */] - modelPosition ;
	vofi_TexCoord1 . x = dot3 ( toEye , _va_[6 /* rpModelMatrixX */] ) ;
	vofi_TexCoord1 . y = dot3 ( toEye , _va_[7 /* rpModelMatrixY */] ) ;
	vofi_TexCoord1 . z = dot3 ( toEye , _va_[8 /* rpModelMatrixZ */] ) ;
	vofi_TexCoord2 . x = dot3 ( tangent , _va_[6 /* rpModelMatrixX */] ) ;
	vofi_TexCoord3 . x = dot3 ( tangent , _va_[7 /* rpModelMatrixY */] ) ;
	vofi_TexCoord4 . x = dot3 ( tangent , _va_[8 /* rpModelMatrixZ */] ) ;
	vofi_TexCoord2 . y = dot3 ( binormal , _va_[6 /* rpModelMatrixX */] ) ;
	vofi_TexCoord3 . y = dot3 ( binormal , _va_[7 /* rpModelMatrixY */] ) ;
	vofi_TexCoord4 . y = dot3 ( binormal , _va_[8 /* rpModelMatrixZ */] ) ;
	vofi_TexCoord2 . z = dot3 ( normal , _va_[6 /* rpModelMatrixX */] ) ;
	vofi_TexCoord3 . z = dot3 ( normal , _va_[7 /* rpModelMatrixY */] ) ;
	vofi_TexCoord4 . z = dot3 ( normal , _va_[8 /* rpModelMatrixZ */] ) ;
	frontColor = _va_[1 /* rpColor */] ;
}
