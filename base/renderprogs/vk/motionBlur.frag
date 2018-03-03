#version 450
#extension GL_ARB_separate_shader_objects : enable

void clip( float v ) { if ( v < 0.0 ) { discard; } }
void clip( vec2 v ) { if ( any( lessThan( v, vec2( 0.0 ) ) ) ) { discard; } }
void clip( vec3 v ) { if ( any( lessThan( v, vec3( 0.0 ) ) ) ) { discard; } }
void clip( vec4 v ) { if ( any( lessThan( v, vec4( 0.0 ) ) ) ) { discard; } }

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }

vec4 tex2D( sampler2D samp, vec2 texcoord ) { return texture( samp, texcoord.xy ); }
vec4 tex2D( sampler2DShadow samp, vec3 texcoord ) { return vec4( texture( samp, texcoord.xyz ) ); }

vec4 tex2D( sampler2D samp, vec2 texcoord, vec2 dx, vec2 dy ) { return textureGrad( samp, texcoord.xy, dx, dy ); }
vec4 tex2D( sampler2DShadow samp, vec3 texcoord, vec2 dx, vec2 dy ) { return vec4( textureGrad( samp, texcoord.xyz, dx, dy ) ); }

vec4 texCUBE( samplerCube samp, vec3 texcoord ) { return texture( samp, texcoord.xyz ); }
vec4 texCUBE( samplerCubeShadow samp, vec4 texcoord ) { return vec4( texture( samp, texcoord.xyzw ) ); }

vec4 tex1Dproj( sampler1D samp, vec2 texcoord ) { return textureProj( samp, texcoord ); }
vec4 tex2Dproj( sampler2D samp, vec3 texcoord ) { return textureProj( samp, texcoord ); }
vec4 tex3Dproj( sampler3D samp, vec4 texcoord ) { return textureProj( samp, texcoord ); }

vec4 tex1Dbias( sampler1D samp, vec4 texcoord ) { return texture( samp, texcoord.x, texcoord.w ); }
vec4 tex2Dbias( sampler2D samp, vec4 texcoord ) { return texture( samp, texcoord.xy, texcoord.w ); }
vec4 tex3Dbias( sampler3D samp, vec4 texcoord ) { return texture( samp, texcoord.xyz, texcoord.w ); }
vec4 texCUBEbias( samplerCube samp, vec4 texcoord ) { return texture( samp, texcoord.xyz, texcoord.w ); }

vec4 tex1Dlod( sampler1D samp, vec4 texcoord ) { return textureLod( samp, texcoord.x, texcoord.w ); }
vec4 tex2Dlod( sampler2D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xy, texcoord.w ); }
vec4 tex3Dlod( sampler3D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xyz, texcoord.w ); }
vec4 texCUBElod( samplerCube samp, vec4 texcoord ) { return textureLod( samp, texcoord.xyz, texcoord.w ); }

layout(set = 0, binding = 1) uniform UBO {
	vec4 _fa_[6];
};

layout(set = 2, binding = 0) uniform sampler2D samp0;
layout(set = 3, binding = 0) uniform sampler2D samp1;

layout(location = 0) in vec2 vofi_TexCoord0;

layout(location = 0) out vec4 fragColor;

void main() {
	if ( tex2D ( samp0 , vofi_TexCoord0 ). w == 0.0 ) {
		discard ;
	}
	float windowZ = 1.0- tex2D ( samp1 , vofi_TexCoord0 ). x ;
	vec3 ndc = vec3 ( vofi_TexCoord0 * 2.0 - 1.0 , windowZ * 2.0 - 1.0 ) ;
	float clipW = - _fa_[4 /* rpProjectionMatrixZ */] . w / ( - _fa_[4 /* rpProjectionMatrixZ */] . z - ndc. z ) ;
	vec4 clip = vec4 ( ndc * clipW , clipW ) ;
	vec4 reClip ;
	reClip. x = dot ( _fa_[0 /* rpMVPmatrixX */] , clip ) ;
	reClip. y = dot ( _fa_[1 /* rpMVPmatrixY */] , clip ) ;
	reClip. z = dot ( _fa_[2 /* rpMVPmatrixZ */] , clip ) ;
	reClip. w = dot ( _fa_[3 /* rpMVPmatrixW */] , clip ) ;
	vec2 prevTexCoord ;
	prevTexCoord. x = ( reClip. x / reClip. w ) * 0.5 + 0.5 ;
	prevTexCoord. y = ( reClip. y / reClip. w ) * 0.5 + 0.5 ;
	vec2 texCoord = prevTexCoord ;
	vec2 delta = ( vofi_TexCoord0 - prevTexCoord ) ;
	vec3 sum = vec3 ( 0.0 ) ;
	float goodSamples = 0 ;
	float samples = _fa_[5 /* rpOverbright */] . x ;
	for ( float i = 0 ; i < samples ; i = i + 1 ) {
		vec2 pos = vofi_TexCoord0 + delta * ( ( i / ( samples - 1 ) ) - 0.5 ) ;
		vec4 color = tex2D ( samp0 , pos ) ;
		sum += color. xyz * color. w ;
		goodSamples += color. w ;
	}
	float invScale = 1.0 / goodSamples ;
	fragColor = vec4 ( sum * invScale , 1.0 ) ;
}
