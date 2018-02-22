#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xy, texcoord.w ); }


layout(set = 0, binding = 0) uniform UBO {
	vec4 _va_[10];
};

float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }

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
layout(location = 1) out vec4 vofi_TexCoord0;
layout(location = 2) out vec4 vofi_TexCoord1;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main() {vec4 modelPosition = in_Position ; if ( _va_[7 /* rpEnableSkinning */] . x > 0.0 ) {
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
		modelPosition. x = dot4 ( matX , in_Position ) ;
		modelPosition. y = dot4 ( matY , in_Position ) ;
		modelPosition. z = dot4 ( matZ , in_Position ) ;
		modelPosition. w = 1.0 ;
	}
	gl_Position . x = dot4 ( modelPosition , _va_[0 /* rpMVPmatrixX */] ) ;
	gl_Position . y = dot4 ( modelPosition , _va_[1 /* rpMVPmatrixY */] ) ;
	gl_Position . z = dot4 ( modelPosition , _va_[2 /* rpMVPmatrixZ */] ) ;
	gl_Position . w = dot4 ( modelPosition , _va_[3 /* rpMVPmatrixW */] ) ;
    out_Position = gl_Position;
	vec4 textureScroll = _va_[8 /* rpUser0 */] ;
	vofi_TexCoord0 = vec4 ( in_TexCoord . xy , 0 , 0 ) + textureScroll ;
	vec4 vec = vec4 ( 0 , 1 , 0 , 1 ) ;
	vec. z = dot4 ( modelPosition , _va_[6 /* rpModelViewMatrixZ */] ) ;
	float magicProjectionAdjust = 0.43 ;
	float x = dot4 ( vec , _va_[4 /* rpProjectionMatrixY */] ) * magicProjectionAdjust ;
	float w = dot4 ( vec , _va_[5 /* rpProjectionMatrixW */] ) ;
	w = max ( w , 1.0 ) ;
	x /= w ;
	x = min ( x , 0.02 ) ;
	vec4 deformMagnitude = _va_[9 /* rpUser1 */] ;
	vofi_TexCoord1 = x * deformMagnitude ;
}
