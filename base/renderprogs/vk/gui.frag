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

vec4 tex2D( sampler2D s, vec2 texcoord ) { return texture( s, texcoord.xy ); }
vec4 tex2D( sampler2DShadow s, vec3 texcoord ) { return vec4( texture( s, texcoord.xyz ) ); }

vec4 tex2D( sampler2D s, vec2 texcoord, vec2 dx, vec2 dy ) { return textureGrad( s, texcoord.xy, dx, dy ); }
vec4 tex2D( sampler2DShadow s, vec3 texcoord, vec2 dx, vec2 dy ) { return vec4( textureGrad( s, texcoord.xyz, dx, dy ) ); }

vec4 texCUBE( samplerCube s, vec3 texcoord ) { return texture( s, texcoord.xyz ); }
vec4 texCUBE( samplerCubeShadow s, vec4 texcoord ) { return vec4( texture( s, texcoord.xyzw ) ); }

vec4 tex1Dproj( sampler1D s, vec2 texcoord ) { return textureProj( s, texcoord ); }
vec4 tex2Dproj( sampler2D s, vec3 texcoord ) { return textureProj( s, texcoord ); }
vec4 tex3Dproj( sampler3D s, vec4 texcoord ) { return textureProj( s, texcoord ); }

vec4 tex1Dbias( sampler1D s, vec4 texcoord ) { return texture( s, texcoord.x, texcoord.w ); }
vec4 tex2Dbias( sampler2D s, vec4 texcoord ) { return texture( s, texcoord.xy, texcoord.w ); }
vec4 tex3Dbias( sampler3D s, vec4 texcoord ) { return texture( s, texcoord.xyz, texcoord.w ); }
vec4 texCUBEbias( samplerCube s, vec4 texcoord ) { return texture( s, texcoord.xyz, texcoord.w ); }

vec4 tex1Dlod( sampler1D s, vec4 texcoord ) { return textureLod( s, texcoord.x, texcoord.w ); }
vec4 tex2Dlod( sampler2D s, vec4 texcoord ) { return textureLod( s, texcoord.xy, texcoord.w ); }
vec4 tex3Dlod( sampler3D s, vec4 texcoord ) { return textureLod( s, texcoord.xyz, texcoord.w ); }
vec4 texCUBElod( samplerCube s, vec4 texcoord ) { return textureLod( s, texcoord.xyz, texcoord.w ); }

layout(set = 0, binding = 1) uniform sampler2D samp0;

layout(location = 0) in vec4 inFragCoord;
layout(location = 1) in vec2 vofi_TexCoord0;
layout(location = 2) in vec4 vofi_TexCoord1;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec4 outFragColor;

void main() {
	vec4 color = ( tex2D ( samp0 , vofi_TexCoord0 ) * inColor ) + vofi_TexCoord1 ;
	outFragColor . xyz = color. xyz * color. w ;
	outFragColor . w = color. w ;
}