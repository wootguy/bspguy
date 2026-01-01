#version 120
// object variables
uniform mat4 modelViewProjection;
uniform vec4 colorMult;

// vertex variables
attribute vec3 vPosition;
attribute vec4 vColor;
attribute vec3 vBary;
attribute vec3 vEdgeEnable;

// fragment variables
varying vec4 fColor;
varying vec3 fBary;
varying vec3 fEdgeEnable;

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fColor = vColor * colorMult;
	fBary = vBary;
    fEdgeEnable = vEdgeEnable;
}