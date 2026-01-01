#version 120
#line 1
// object variables
uniform mat4 modelViewProjection;
uniform vec4 colorMult;

// vertex variables
attribute vec3 vPosition;
attribute vec3 vTex;
attribute vec4 vAtlas;
attribute vec3 vBary;
attribute vec3 vEdgeEnable;
attribute vec3 vLightmapTex0;
attribute vec3 vLightmapTex1;
attribute vec3 vLightmapTex2;
attribute vec3 vLightmapTex3;
attribute vec4 vColor;

// fragment variables
varying vec3 fTex;
varying vec4 fAtlas;
varying vec3 fLightmapTex0;
varying vec3 fLightmapTex1;
varying vec3 fLightmapTex2;
varying vec3 fLightmapTex3;
varying vec4 fColor;
varying vec3 fBary;
varying vec3 fEdgeEnable;

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fTex = vTex;
	fLightmapTex0 = vLightmapTex0;
	fLightmapTex1 = vLightmapTex1;
	fLightmapTex2 = vLightmapTex2;
	fLightmapTex3 = vLightmapTex3;
	fColor = vColor * colorMult;
	fAtlas = vAtlas;
	fBary = vBary;
    fEdgeEnable = vEdgeEnable;
}