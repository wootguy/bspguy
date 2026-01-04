// object variables
uniform mat4 modelViewProjection;
uniform vec4 color;

// vertex variables
attribute vec3 vPosition;
attribute vec2 vTex;

// fragment variables
varying vec2 fTex;
varying vec4 fColor;

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fTex = vTex;
	fColor = color;
}