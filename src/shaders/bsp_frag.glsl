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

#ifdef TEXTURE_ARRAY
	varying vec3 fTex;
#else
	varying vec2 fTex;
#endif

uniform sampler2D sLightmapTex;
uniform sampler2D pTex;

#if defined(TEXTURE_ARRAY) && !defined(TEXTURE_ATLAS)
	#extension GL_EXT_texture_array : enable
	uniform sampler2DArray sTex;
#else
	uniform sampler2D sTex;
#endif

void main()
{	
	if (fColor.a == 0.0) // faces marked hidden
		discard;
		
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
	
	if (alphaTest != 0.0) { // solid texture mode
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

	vec3 color = texel.rgb * fColor.rgb;
	
	if (fColor.a >= 0.9f) { // transparent faces are "special" and have no lighting
		vec3 lightmap = texture2D(sLightmapTex, fLightmapTex01.xy).rgb * lightmapMult.x;
		lightmap += texture2D(sLightmapTex, fLightmapTex01.zw).rgb * lightmapMult.y;
		lightmap += texture2D(sLightmapTex, fLightmapTex23.xy).rgb * lightmapMult.z;
		lightmap += texture2D(sLightmapTex, fLightmapTex23.zw).rgb * lightmapMult.w;
		color = color * lightmap;
	}
	
	vec4 texColor = vec4(pow(color, vec3(1.0/gamma)), fColor.a*texel.a);
	
	#if defined(WIREFRAME)
		vec3 d = fwidth(fBary);
		
		// disable edges
		vec3 edgeDistPx = fBary / d;
		edgeDistPx = mix(vec3(1e6), edgeDistPx, fEdgeEnable); 
		
		float edge = min(min(edgeDistPx.x, edgeDistPx.y), edgeDistPx.z); // distance from edge
		float mask = smoothstep(0.0, wireframeThickness, edge); // constant thickness in pixels

		if (mask < 1.0) {
			float lum = dot(texColor.rgb, vec3(0.2126, 0.7152, 0.0722));
			gl_FragColor = (lum > 0.25) && wireframeOnly < 1.0f ? wireframeColorBright : wireframeColorDark;
			return;
		}
	
		if (wireframeOnly > 0 || !gl_FrontFacing) {
			discard;
		}
	#endif
	
	gl_FragColor = texColor;
}