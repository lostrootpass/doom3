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

layout(location = 0) in vec2 vofi_TexCoord0;
layout(location = 1) in vec2 vofi_TexCoord1;
layout(location = 2) in vec2 vofi_TexCoord2;

layout(location = 0) out vec4 fragColor;

void main() {
	vec4 redTint = vec4 ( 1 , 0.98 , 0.98 , 1 ) ;
	vec4 accumSample = tex2D ( samp0 , vofi_TexCoord0 ) * redTint ;
	vec4 maskSample = tex2D ( samp2 , vofi_TexCoord1 ) ;
	vec4 tint = vec4 ( 1.0 , 0.8 , 0.8 , 1 ) ;
	vec4 currentRenderSample = tex2D ( samp1 , vofi_TexCoord2 ) * tint ;
	fragColor = mix ( accumSample , currentRenderSample , maskSample. a ) ;
}
