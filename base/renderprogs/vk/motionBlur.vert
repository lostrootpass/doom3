#version 450
#extension GL_ARB_separate_shader_objects : enable

float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }
vec2 saturate( vec2 v ) { return clamp( v, 0.0, 1.0 ); }
vec3 saturate( vec3 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }
vec4 tex2Dlod( sampler2D samp, vec4 texcoord ) { return textureLod( samp, texcoord.xy, texcoord.w ); }



layout(location = 0) in vec4 in_Position;
layout(location = 1) in mediump vec2 in_TexCoord;

layout(location = 0) out vec2 vofi_TexCoord0;

out gl_PerVertex
{
	vec4 gl_Position;
};


void main() {
	gl_Position = in_Position ;
	vofi_TexCoord0 = in_TexCoord ;
}
