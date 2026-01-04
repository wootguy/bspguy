// object variables
uniform mat4 modelViewProjection;
uniform vec4 colorMult;
uniform float lightmapAtlasScale;
uniform vec2 paletteAtlasScale;

// vertex variables
attribute vec3 vPosition;
attribute vec2 vTex;
attribute vec4 vAtlas;
attribute vec4 vCustom;
attribute vec4 vLightmapTex01;
attribute vec4 vLightmapTex23;
attribute vec4 vLightmapBright;
attribute vec4 vColor;

// fragment variables (no more than 32 floats for max compatibility)
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

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fLightmapTex01 = vLightmapTex01*lightmapAtlasScale;
	fLightmapTex23 = vLightmapTex23*lightmapAtlasScale;
	fLightmapBright = vLightmapBright;
	fColor = vColor * colorMult;
	
	#ifdef TEXTURE_ARRAY
		fTex = vec3(vTex, vAtlas.x + vAtlas.y*256);
	#else
		fTex = vTex;
	#endif
	
	#ifdef TEXTURE_ATLAS
		fAtlas = vec4(vAtlas.x*16, vAtlas.y*16, vAtlas.z*16, vAtlas.w*16);
	#endif

	#ifdef TEXTURE_PAL
		fPal = vec2(vCustom.y*256 + 0.5, (vCustom.z*256 + vCustom.w + 0.5) * paletteAtlasScale.y);
	#endif

	#ifdef WIREFRAME
		float v = vCustom.x;
		float bit0 = mod(v, 2.0);
		float bit1 = mod(floor(v * 0.5), 2.0);
		float bit2 = mod(floor(v * 0.25), 2.0);
		float bit3 = mod(floor(v * 0.125), 2.0);
		float bit4 = mod(floor(v * 0.0625), 2.0);
		float bit5 = mod(floor(v * 0.03125), 2.0);
		fEdgeEnable = vec3(bit0, bit1, bit2);
		fBary = vec3(bit3, bit4, bit5);
	#endif
}