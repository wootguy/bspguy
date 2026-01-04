uniform float alphaTest;
uniform float gamma;
uniform vec4 wireframeColorDark;
uniform vec4 wireframeColorBright;
uniform float wireframeThickness;
uniform float textureAtlasScale;
uniform vec2 paletteAtlasScale;

varying vec4 fAtlas;
varying vec4 fLightmapTex01;
varying vec4 fLightmapTex23;
varying vec4 fLightmapBright;
varying vec4 fColor;
varying vec3 fBary;
varying vec3 fEdgeEnable;
varying vec2 fPal;

#ifdef TEXTURE_ARRAY
	varying vec3 fTex;
#else
	varying vec2 fTex;
#endif

uniform sampler2D sLightmapTex0;
uniform sampler2D sLightmapTex1;
uniform sampler2D sLightmapTex2;
uniform sampler2D sLightmapTex3;
uniform sampler2D pTex;

#if defined(TEXTURE_ARRAY) && !defined(TEXTURE_ATLAS)
	#extension GL_EXT_texture_array : enable
	uniform sampler2DArray sTex;
#else
	uniform sampler2D sTex;
#endif

void main()
{
	vec4 texel;
	
	#if defined(TEXTURE_ATLAS)
		vec2 texCoord = fTex.xy;
		texCoord.x = fract(texCoord.x) * fAtlas.z*textureAtlasScale + fAtlas.x*textureAtlasScale;
		texCoord.y = fract(texCoord.y) * fAtlas.w*textureAtlasScale + fAtlas.y*textureAtlasScale;
		texel = texture2D(sTex, texCoord);
	#elif defined(TEXTURE_ARRAY)
		texel = texture2DArray(sTex, fTex);
	#else
		texel = texture2D(sTex, fTex.xy);
	#endif
	
	#if defined(TEXTURE_PAL)
		float palIdx = texel.r;
		texel = texture2D(pTex, vec2((fPal.x + palIdx*255) * paletteAtlasScale.x, fPal.y));
	#endif
	
	if (alphaTest != 0.0) {
		#if defined(TEXTURE_PAL)
			if (palIdx == 1.0) {
				discard;
			}
		#else
			if (texel.a == 0.0) {
				discard;
			}
		#endif
	}
	else {
		texel.a = 1.0;
	}
	if (fColor.a == 0.0)
		discard;

	vec3 lightmap = texture2D(sLightmapTex0, fLightmapTex01.xy).rgb * fLightmapBright.x;
	lightmap += texture2D(sLightmapTex1, fLightmapTex01.zw).rgb * fLightmapBright.y;
	lightmap += texture2D(sLightmapTex2, fLightmapTex23.xy).rgb * fLightmapBright.z;
	lightmap += texture2D(sLightmapTex3, fLightmapTex23.zw).rgb * fLightmapBright.w;
	vec3 color = texel.rgb * lightmap * fColor.rgb;

	vec4 texColor = vec4(pow(color, vec3(1.0/gamma)), fColor.a*texel.a);
	
	#if defined(WIREFRAME)
		vec3 a = smoothstep(vec3(0.0), fwidth(fBary) * wireframeThickness, fBary);
		a = mix(vec3(1.0), a, fEdgeEnable);
		float dist = min(min(a.x, a.y), a.z);
		
		if (dist < 1.0) {
			float lum = dot(texColor.rgb, vec3(0.2126, 0.7152, 0.0722));
			gl_FragColor = (lum > 0.25) ? wireframeColorBright : wireframeColorDark;
			return;
		}
	
		if (!gl_FrontFacing) {
			discard;
		}
	#endif
	
	gl_FragColor = texColor;
}