#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xy, texcoord.w ); }


layout(set = 0, binding = 0) uniform UBO {
	vec4 _va_[6];
};


float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }

layout(location = 0) in vec4 in_Position;
layout(location = 2) in lowp vec4 in_Normal;
layout(location = 4) in lowp vec4 in_Color;

layout(location = 0) out vec4 out_Position;
layout(location = 1) out vec3 vofi_TexCoord0;
layout(location = 2) out vec3 vofi_TexCoord1;
layout(location = 3) out vec4 out_FrontColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main() {
	vec4 vNormal = in_Normal * 2.0 - 1.0 ;
	gl_Position . x = dot4 ( in_Position , _va_[2 /* rpMVPmatrixX */] ) ;
	gl_Position . y = dot4 ( in_Position , _va_[3 /* rpMVPmatrixY */] ) ;
	gl_Position . z = dot4 ( in_Position , _va_[4 /* rpMVPmatrixZ */] ) ;
	gl_Position . w = dot4 ( in_Position , _va_[5 /* rpMVPmatrixW */] ) ;
    out_Position = gl_Position;
	vec4 toEye = _va_[0 /* rpLocalViewOrigin */] - in_Position ;
	vofi_TexCoord0 = toEye. xyz ;
	vofi_TexCoord1 = vNormal. xyz ;
	out_FrontColor = _va_[1 /* rpColor */] ;
}
