#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xy, texcoord.w ); }

layout(set = 0, binding = 0) uniform UBO {
	vec4 _va_[5];
};

float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }

layout(set = 1, binding = 0) readonly buffer matrixbuffer {
	vec4 matrices [ 408 ];
};

layout(location = 0) in vec4 in_Position;
layout(location = 1) in lowp vec4 in_Color;
layout(location = 2) in lowp vec4 in_Color2;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() {
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
	vec4 vertexPosition = in_Position ;
	vertexPosition. w = 1.0 ;
	vec4 modelPosition ;
	modelPosition. x = dot4 ( matX , vertexPosition ) ;
	modelPosition. y = dot4 ( matY , vertexPosition ) ;
	modelPosition. z = dot4 ( matZ , vertexPosition ) ;
	modelPosition. w = in_Position . w ;
	vec4 vPos = modelPosition - _va_[0 /* rpLocalLightOrigin */] ;
	vPos = ( vPos. wwww * _va_[0 /* rpLocalLightOrigin */] ) + vPos ;
	gl_Position . x = dot4 ( vPos , _va_[1 /* rpMVPmatrixX */] ) ;
	gl_Position . y = dot4 ( vPos , _va_[2 /* rpMVPmatrixY */] ) ;
	gl_Position . z = dot4 ( vPos , _va_[3 /* rpMVPmatrixZ */] ) ;
	gl_Position . w = dot4 ( vPos , _va_[4 /* rpMVPmatrixW */] ) ;
}
