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
	vec4 _fa_[2];
};

float dot3 (vec3 a , vec3 b ) {return dot ( a , b ) ; }
float dot3 (vec3 a , vec4 b ) {return dot ( a , b. xyz ) ; }
float dot3 (vec4 a , vec3 b ) {return dot ( a. xyz , b ) ; }
float dot3 (vec4 a , vec4 b ) {return dot ( a. xyz , b. xyz ) ; }
float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }
float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }
const vec4 matrixCoCg1YtoRGB1X = vec4( 1.0, -1.0, 0.0, 1.0 );
const vec4 matrixCoCg1YtoRGB1Y = vec4( 0.0, 1.0, -0.50196078, 1.0 );
const vec4 matrixCoCg1YtoRGB1Z = vec4( -1.0, -1.0, 1.00392156, 1.0 );
vec3 ConvertYCoCgToRGB (vec4 YCoCg ) {
	vec3 rgbColor ;
	YCoCg. z = ( YCoCg. z * 31.875 ) + 1.0 ;
	YCoCg. z = 1.0 / YCoCg. z ;
	YCoCg. xy *= YCoCg. z ;
	rgbColor. x = dot4 ( YCoCg , matrixCoCg1YtoRGB1X ) ;
	rgbColor. y = dot4 ( YCoCg , matrixCoCg1YtoRGB1Y ) ;
	rgbColor. z = dot4 ( YCoCg , matrixCoCg1YtoRGB1Z ) ;
	return rgbColor ;
}
vec4 idtex2Dproj (sampler2D samp , vec4 texCoords ) {return tex2Dproj ( samp , texCoords. xyw ) ; }
layout(set = 1, binding = 0) uniform sampler2D samp0;
layout(set = 2, binding = 0) uniform sampler2D samp1;
layout(set = 3, binding = 0) uniform sampler2D samp2;
layout(set = 4, binding = 0) uniform sampler2D samp3;
layout(set = 5, binding = 0) uniform sampler2D samp4;

layout(location = 0) in vec4 fragCoord;
layout(location = 1) in vec4 vofi_TexCoord0;
layout(location = 2) in vec4 vofi_TexCoord1;
layout(location = 3) in vec4 vofi_TexCoord2;
layout(location = 4) in vec4 vofi_TexCoord3;
layout(location = 5) in vec4 vofi_TexCoord4;
layout(location = 6) in vec4 vofi_TexCoord5;
layout(location = 7) in vec4 vofi_TexCoord6;
layout(location = 8) in vec4 color;

layout(location = 0) out vec4 fragColor;

void main() {
	vec4 bumpMap = tex2D ( samp0 , vofi_TexCoord1 . xy ) ;
	vec4 lightFalloff = idtex2Dproj ( samp1 , vofi_TexCoord2 ) ;
	vec4 lightProj = idtex2Dproj ( samp2 , vofi_TexCoord3 ) ;
	vec4 YCoCG = tex2D ( samp3 , vofi_TexCoord4 . xy ) ;
	vec4 specMap = tex2D ( samp4 , vofi_TexCoord5 . xy ) ;
	vec3 lightVector = normalize ( vofi_TexCoord0 . xyz ) ;
	vec3 diffuseMap = ConvertYCoCgToRGB ( YCoCG ) ;
	vec3 localNormal ;
	localNormal. xy = bumpMap. wy - 0.5 ;
	localNormal. z = sqrt ( abs ( dot ( localNormal. xy , localNormal. xy ) - 0.25 ) ) ;
	localNormal = normalize ( localNormal ) ;
	float specularPower = 10.0 ;
	float hDotN = dot3 ( normalize ( vofi_TexCoord6 . xyz ) , localNormal ) ;
	vec3 specularContribution = vec3 ( pow ( hDotN , specularPower ) ) ;
	vec3 diffuseColor = diffuseMap * _fa_[0 /* rpDiffuseModifier */] . xyz ;
	vec3 specularColor = specMap. xyz * specularContribution * _fa_[1 /* rpSpecularModifier */] . xyz ;
	vec3 lightColor = dot3 ( lightVector , localNormal ) * lightProj. xyz * lightFalloff. xyz ;
	fragColor . xyz = ( diffuseColor + specularColor ) * lightColor * color . xyz ;
	fragColor . w = 1.0 ;
}
