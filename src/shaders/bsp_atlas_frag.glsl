#version 120
uniform float alphaTest;
uniform float gamma;
uniform float wireframeEnable;
uniform vec4 wireframeColorDark;
uniform vec4 wireframeColorBright;
uniform float wireframeThickness;
uniform float textureAtlasScale;

varying vec3 fTex;
varying vec4 fAtlas;
varying vec2 fLightmapTex0;
varying vec2 fLightmapTex1;
varying vec2 fLightmapTex2;
varying vec2 fLightmapTex3;
varying vec4 fLightmapBright;
varying vec4 fColor;
varying vec3 fBary;
varying vec3 fEdgeEnable;

uniform sampler2D sTex;
uniform sampler2D sLightmapTex0;
uniform sampler2D sLightmapTex1;
uniform sampler2D sLightmapTex2;
uniform sampler2D sLightmapTex3;

void main()
{
	// atlas texture wrapping
	vec2 texCoord = fTex.xy;
	texCoord.x = fract(texCoord.x) * fAtlas.z*textureAtlasScale + fAtlas.x*textureAtlasScale;
	texCoord.y = fract(texCoord.y) * fAtlas.w*textureAtlasScale + fAtlas.y*textureAtlasScale;

	vec4 texel = texture2D(sTex, texCoord);
	if (alphaTest != 0.0) {
		if (texel.a == 0.0) {
			discard;
		}
	}
	else {
		texel.a = 1.0;
	}
	if (fColor.a == 0.0)
		discard;

	vec3 lightmap = texture2D(sLightmapTex0, fLightmapTex0.xy).rgb * fLightmapBright.x;
	lightmap += texture2D(sLightmapTex1, fLightmapTex1).rgb * fLightmapBright.y;
	lightmap += texture2D(sLightmapTex2, fLightmapTex2).rgb * fLightmapBright.z;
	lightmap += texture2D(sLightmapTex3, fLightmapTex3).rgb * fLightmapBright.w;
	vec3 color = texel.rgb * lightmap * fColor.rgb;

	vec4 texColor = vec4(pow(color, vec3(1.0/gamma)), fColor.a*texel.a);
	
	if (wireframeEnable != 0) {
		vec3 a = smoothstep(vec3(0.0), fwidth(fBary) * wireframeThickness, fBary);
		a = mix(vec3(1.0), a, fEdgeEnable);
		float dist = min(min(a.x, a.y), a.z);
		
		if (dist < 1.0) {
			float lum = dot(texColor.rgb, vec3(0.2126, 0.7152, 0.0722));
			gl_FragColor = (lum > 0.25) ? wireframeColorBright : wireframeColorDark;
			return;
		}
	}
	
	if (!gl_FrontFacing) {
		discard;
	}
	
	gl_FragColor = texColor;
}