#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D s, vec4 texcoord ) { return textureLod( s, texcoord.xy, texcoord.w ); }


layout(set = 0, binding = 0) uniform UBO {
	vec4 _va_[4];
};

float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }
vec4 swizzleColor (vec4 c ) {return c ; }

layout(location = 0) in vec4 in_Position;
layout(location = 1) in vec2 in_TexCoord;
layout(location = 2) in vec4 in_Normal;
layout(location = 3) in vec4 in_Tangent;
layout(location = 4) in vec4 in_Color;
layout(location = 5) in vec4 in_Color2;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec2 vofi_TexCoord0;
layout(location = 2) out vec4 vofi_TexCoord1;
layout(location = 3) out vec4 outColor;

void main() {
	outPosition . x = dot4 ( in_Position , _va_[0 /* rpMVPmatrixX */] ) ;
	outPosition . y = dot4 ( in_Position , _va_[1 /* rpMVPmatrixY */] ) ;
	outPosition . z = dot4 ( in_Position , _va_[2 /* rpMVPmatrixZ */] ) ;
	outPosition . w = dot4 ( in_Position , _va_[3 /* rpMVPmatrixW */] ) ;
	vofi_TexCoord0 . xy = in_TexCoord . xy ;
	vofi_TexCoord1 = ( swizzleColor ( in_Color2 ) * 2 ) - 1 ;
	outColor = swizzleColor ( in_Color ) ;
}