#version 120
// object variables
uniform mat4 modelViewProjection;
uniform vec4 colorMult;

// vertex variables
attribute vec3 vPosition;
attribute vec4 vColor;
attribute float vEdges;

// fragment variables
varying vec4 fColor;
varying vec3 fBary;
varying vec3 fEdgeEnable;

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fColor = vColor * colorMult;
	
	float v = vEdges;
	float bit0 = mod(v, 2.0);
	float bit1 = mod(floor(v * 0.5), 2.0);
	float bit2 = mod(floor(v * 0.25), 2.0);
	float bit3 = mod(floor(v * 0.125), 2.0);
	float bit4 = mod(floor(v * 0.0625), 2.0);
	float bit5 = mod(floor(v * 0.03125), 2.0);
	fEdgeEnable = vec3(bit0, bit1, bit2);
	fBary = vec3(bit3, bit4, bit5);
}