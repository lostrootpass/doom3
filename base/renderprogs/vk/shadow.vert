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

layout(location = 0) in vec4 in_Position;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() {
	vec4 vPos = in_Position - _va_[0 /* rpLocalLightOrigin */] ;
	vPos = ( vPos. wwww * _va_[0 /* rpLocalLightOrigin */] ) + vPos ;
	gl_Position . x = dot4 ( vPos , _va_[1 /* rpMVPmatrixX */] ) ;
	gl_Position . y = dot4 ( vPos , _va_[2 /* rpMVPmatrixY */] ) ;
	gl_Position . z = dot4 ( vPos , _va_[3 /* rpMVPmatrixZ */] ) ;
	gl_Position . w = dot4 ( vPos , _va_[4 /* rpMVPmatrixW */] ) ;
}
