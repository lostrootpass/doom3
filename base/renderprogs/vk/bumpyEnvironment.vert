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

layout(location = 0) in vec4 in_Position;
layout(location = 1) in mediump vec2 in_TexCoord;
layout(location = 2) in lowp vec4 in_Normal;
layout(location = 3) in lowp vec4 in_Tangent;
layout(location = 4) in lowp vec4 in_Color;

layout(location = 0) out vec4 out_Position;
layout(location = 1) out vec2 vofi_TexCoord0;
layout(location = 2) out vec3 vofi_TexCoord1;
layout(location = 3) out vec3 vofi_TexCoord2;
layout(location = 4) out vec3 vofi_TexCoord3;
layout(location = 5) out vec3 vofi_TexCoord4;
layout(location = 6) out vec4 out_FrontColor;

void main() {
	vec4 normal = in_Normal * 2.0 - 1.0 ;
	vec4 tangent = in_Tangent * 2.0 - 1.0 ;
	vec3 binormal = cross ( normal. xyz , tangent. xyz ) * tangent. w ;
	gl_Position . x = dot4 ( in_Position , _va_[2 /* rpMVPmatrixX */] ) ;
	gl_Position . y = dot4 ( in_Position , _va_[3 /* rpMVPmatrixY */] ) ;
	gl_Position . z = dot4 ( in_Position , _va_[4 /* rpMVPmatrixZ */] ) ;
	gl_Position . w = dot4 ( in_Position , _va_[5 /* rpMVPmatrixW */] ) ;

    out_Position = gl_Position;

	vofi_TexCoord0 = in_TexCoord . xy ;
	vec4 toEye = _va_[0 /* rpLocalViewOrigin */] - in_Position ;
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
	out_FrontColor = _va_[1 /* rpColor */] ;
}