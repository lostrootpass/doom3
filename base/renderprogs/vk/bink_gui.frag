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

layout(set = 2, binding = 0) uniform sampler2D samp0;
layout(set = 3, binding = 0) uniform sampler2D samp1;
layout(set = 4, binding = 0) uniform sampler2D samp2;

layout(location = 0) in vec4 fragCoord;
layout(location = 1) in vec2 vofi_TexCoord0;
layout(location = 2) in vec4 vofi_TexCoord1;
layout(location = 3) in vec4 color;

layout(location = 0) out vec4 fragColor;

void main() {
	vec3 crc = vec3 ( 1.595794678 , - 0.813476563 , 0 ) ;
	vec3 crb = vec3 ( 0 , - 0.391448975 , 2.017822266 ) ;
	vec3 adj = vec3 ( - 0.87065506 , 0.529705048 , - 1.081668854 ) ;
	vec3 YScalar = vec3 ( 1.164123535 , 1.164123535 , 1.164123535 ) ;
	float Y = tex2D ( samp0 , vofi_TexCoord0 . xy ). x ;
	float Cr = tex2D ( samp1 , vofi_TexCoord0 . xy ). x ;
	float Cb = tex2D ( samp2 , vofi_TexCoord0 . xy ). x ;
	vec3 p = ( YScalar * Y ) ;
	p += ( crc * Cr ) + ( crb * Cb ) + adj ;
	vec4 binkImage ;
	binkImage. xyz = p ;
	binkImage. w = 1.0 ;
	vec4 color = ( binkImage * color ) + vofi_TexCoord1 ;
	fragColor . xyz = color. xyz * color. w ;
	fragColor . w = color. w ;
}
