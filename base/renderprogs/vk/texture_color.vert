#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D s, vec4 texcoord ) { return textureLod( s, texcoord.xy, texcoord.w ); }


layout(set = 0, binding = 0) uniform UBO {
    vec4 _va_[12];
};

float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }
vec4 swizzleColor (vec4 c ) {return c ; }

layout(location = 0) in vec4 in_Position;
layout(location = 1) in mediump vec2 in_TexCoord;
layout(location = 2) in lowp vec4 in_Normal;
layout(location = 3) in lowp vec4 in_Tangent;
layout(location = 4) in lowp vec4 in_Color;

layout(location = 0) out vec4 out_Position;
layout(location = 1) out vec2 vofi_TexCoord0;
layout(location = 2) out vec4 out_FrontColor;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() {
	out_Position . x = dot4 ( in_Position , _va_[3 /* rpMVPmatrixX */] ) ;
	out_Position . y = dot4 ( in_Position , _va_[4 /* rpMVPmatrixY */] ) ;
	out_Position . z = dot4 ( in_Position , _va_[5 /* rpMVPmatrixZ */] ) ;
	out_Position . w = dot4 ( in_Position , _va_[6 /* rpMVPmatrixW */] ) ;

	gl_Position = out_Position;

	if ( _va_[11 /* rpTexGen0Enabled */] . x > 0.0 ) {
		vofi_TexCoord0 . x = dot4 ( in_Position , _va_[9 /* rpTexGen0S */] ) ;
		vofi_TexCoord0 . y = dot4 ( in_Position , _va_[10 /* rpTexGen0T */] ) ;
	} else {
		vofi_TexCoord0 . x = dot4 ( in_TexCoord . xy , _va_[7 /* rpTextureMatrixS */] ) ;
		vofi_TexCoord0 . y = dot4 ( in_TexCoord . xy , _va_[8 /* rpTextureMatrixT */] ) ;
	}
	vec4 vertexColor = ( swizzleColor ( in_Color ) * _va_[0 /* rpVertexColorModulate */] ) + _va_[1 /* rpVertexColorAdd */] ;
    out_FrontColor = vertexColor * _va_[2 /* rpColor */] ;
}
