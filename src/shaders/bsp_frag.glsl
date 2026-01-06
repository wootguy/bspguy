#if defined(TEX_ARRAY) && !defined(TEX_ATLAS)
	#extension GL_EXT_texture_array : enable
#endif

uniform float alphaTest;
uniform float gamma;
uniform vec4 wireframeColorDark;
uniform vec4 wireframeColorBright;
uniform float wireframeThickness;
uniform float textureAtlasScale;
uniform vec2 paletteAtlasScale;
uniform vec4 lightmapMult;
uniform float wireframeOnly;

varying vec4 fAtlas;
varying vec4 fLightmapTex01;
varying vec4 fLightmapTex23;
varying vec4 fColor;
varying vec3 fBary;
varying vec3 fEdgeEnable;
varying vec2 fPal;

#ifdef TEX_ARRAY
	varying vec3 fTex;
#else
	varying vec2 fTex;
#endif

uniform sampler2D sLightmapTex;
uniform sampler2D pTex;

#if defined(TEX_ARRAY) && !defined(TEX_ATLAS)
	uniform sampler2DArray sTex;
#else
	uniform sampler2D sTex;
#endif

void main()
{	
	if (fColor.a == 0.0) // faces marked hidden
		discard;
		
	vec4 texel;
	
	#if defined(TEX_ATLAS)
		vec2 texCoord = fTex.xy;
		texCoord.x = fract(texCoord.x) * fAtlas.z*textureAtlasScale + fAtlas.x*textureAtlasScale;
		texCoord.y = fract(texCoord.y) * fAtlas.w*textureAtlasScale + fAtlas.y*textureAtlasScale;
		texel = texture2D(sTex, texCoord);
	#elif defined(TEX_ARRAY)
		texel = texture2DArray(sTex, fTex);
	#else
		texel = texture2D(sTex, fTex.xy);
	#endif
	
	#if defined(TEX_PAL)
		float palIdx = texel.r;
		texel = texture2D(pTex, vec2((fPal.x + palIdx*255) * paletteAtlasScale.x, fPal.y));
	#endif
	
	if (alphaTest != 0.0) { // solid texture mode
		#if defined(TEX_PAL)
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

	vec3 color = texel.rgb * fColor.rgb;
	vec3 lightmap = texture2D(sLightmapTex, fLightmapTex01.xy).rgb * lightmapMult.x;
	
	// "special" faces have no light styles other than a base style which is bound to a solid color in the editor (white/red)
	if (fColor.a >= 0.9f) {
		lightmap += texture2D(sLightmapTex, fLightmapTex01.zw).rgb * lightmapMult.y;
		lightmap += texture2D(sLightmapTex, fLightmapTex23.xy).rgb * lightmapMult.z;
		lightmap += texture2D(sLightmapTex, fLightmapTex23.zw).rgb * lightmapMult.w;
	}
	
	vec4 texColor = vec4(pow(color * lightmap, vec3(1.0/gamma)), fColor.a*texel.a);
	
	#if defined(WIREFRAME)
		vec3 d = fwidth(fBary);
		
		// disable edges
		vec3 edgeDistPx = fBary / d;
		edgeDistPx = mix(vec3(1e6), edgeDistPx, fEdgeEnable); 
		
		float edge = min(min(edgeDistPx.x, edgeDistPx.y), edgeDistPx.z); // distance from edge
		float mask = smoothstep(0.0, wireframeThickness, edge); // constant thickness in pixels

		if (mask < 1.0) {
			float lum = dot(texColor.rgb, vec3(0.2126, 0.7152, 0.0722));
			gl_FragColor = (lum > 0.25) && wireframeOnly < 0.5f ? wireframeColorBright : wireframeColorDark;
			return;
		}
	
		if (wireframeOnly > 0.0f || !gl_FrontFacing) {
			discard;
		}
	#endif
	
	gl_FragColor = texColor;
}