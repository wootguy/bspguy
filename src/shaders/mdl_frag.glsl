#version 120
varying vec2 fTex;
varying vec4 fColor;

uniform vec4 colorMult;
uniform int additiveEnable;
uniform sampler2D sTex;

void main()
{
	vec4 texel = texture2D(sTex, fTex);
	
	if (texel.a < 0.5)
		discard; // GL_ALPHA_TEST alternative for WebGL (TODO: expensive. Don't use this for every mdl draw call)
	
	if (additiveEnable != 0 && texel.xyz == vec3(0,0,0))
		discard; // additive black textures mess up the transparent background
	
	gl_FragColor = texel * fColor * colorMult;
}