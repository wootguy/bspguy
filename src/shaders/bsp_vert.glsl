// object variables
uniform mat4 modelViewProjection;
uniform vec4 colorMult;
uniform float lightmapAtlasScale;

// vertex variables
attribute vec3 vPosition;
attribute vec2 vTex;
attribute vec4 vAtlas;
attribute float vEdges;
attribute vec4 vLightmapTex01;
attribute vec4 vLightmapTex23;
attribute vec4 vLightmapBright;
attribute vec4 vColor;

// fragment variables (no more than 32 floats for max compatibility)
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

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fTex = vec3(vTex, vAtlas.x + vAtlas.y*256);
	fLightmapTex0 = vLightmapTex01.xy*lightmapAtlasScale;
	fLightmapTex1 = vLightmapTex01.zw*lightmapAtlasScale;
	fLightmapTex2 = vLightmapTex23.xy*lightmapAtlasScale;
	fLightmapTex3 = vLightmapTex23.zw*lightmapAtlasScale;
	fLightmapBright = vLightmapBright;
	fColor = vColor * colorMult;
	fAtlas = vec4(vAtlas.x*16, vAtlas.y*16, vAtlas.z*16, vAtlas.w*16);

	#ifdef WIREFRAME
		float v = vEdges;
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