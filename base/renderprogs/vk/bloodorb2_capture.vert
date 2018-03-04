#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xy, texcoord.w ); }

layout(set = 0, binding = 0) uniform UBO {
	vec4 _va_[7];
};

float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }
vec2 CenterScale (vec2 inTC , vec2 centerScale ) {
	float scaleX = centerScale. x ;
	float scaleY = centerScale. y ;
	vec4 tc0 = vec4 ( scaleX , 0 , 0 , 0.5 - ( 0.5 * scaleX ) ) ;
	vec4 tc1 = vec4 ( 0 , scaleY , 0 , 0.5 - ( 0.5 * scaleY ) ) ;
	vec2 finalTC ;
	finalTC. x = dot4 ( inTC , tc0 ) ;
	finalTC. y = dot4 ( inTC , tc1 ) ;
	return finalTC ;
}
vec2 Rotate2D (vec2 inTC , vec2 cs ) {
	float sinValue = cs. y ;
	float cosValue = cs. x ;
	vec4 tc0 = vec4 ( cosValue , - sinValue , 0 , ( - 0.5 * cosValue ) + ( 0.5 * sinValue ) + 0.5 ) ;
	vec4 tc1 = vec4 ( sinValue , cosValue , 0 , ( - 0.5 * sinValue ) + ( - 0.5 * cosValue ) + 0.5 ) ;
	vec2 finalTC ;
	finalTC. x = dot4 ( inTC , tc0 ) ;
	finalTC. y = dot4 ( inTC , tc1 ) ;
	return finalTC ;
}

layout(location = 0) in vec4 in_Position;
layout(location = 1) in mediump vec2 in_TexCoord;
layout(location = 2) in lowp vec4 in_Normal;
layout(location = 3) in lowp vec4 in_Tangent;
layout(location = 4) in lowp vec4 in_Color;

out gl_PerVertex
{
	vec4 gl_Position;
};

layout(location = 0) out vec2 vofi_TexCoord0;
layout(location = 1) out vec2 vofi_TexCoord1;
layout(location = 2) out vec2 vofi_TexCoord2;

void main() {
	gl_Position . x = dot4 ( in_Position , _va_[0 /* rpMVPmatrixX */] ) ;
	gl_Position . y = dot4 ( in_Position , _va_[1 /* rpMVPmatrixY */] ) ;
	gl_Position . z = dot4 ( in_Position , _va_[2 /* rpMVPmatrixZ */] ) ;
	gl_Position . w = dot4 ( in_Position , _va_[3 /* rpMVPmatrixW */] ) ;
	vec4 centerScaleTex0 = _va_[4 /* rpUser0 */] ;
	vec4 rotateTex0 = _va_[5 /* rpUser1 */] ;
	vec4 centerScaleTex1 = _va_[6 /* rpUser2 */] ;
	vec2 tc0 = CenterScale ( in_TexCoord , centerScaleTex0. xy ) ;
	vofi_TexCoord0 = Rotate2D ( tc0 , rotateTex0. xy ) ;
	vofi_TexCoord1 = CenterScale ( in_TexCoord , centerScaleTex1. xy ) ;
	vofi_TexCoord2 = in_TexCoord ;
}
