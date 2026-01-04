// object variables
uniform mat4 modelViewProjection;

// vertex variables
attribute vec3 vPosition;
attribute vec2 vTex;

// fragment variables
varying vec2 fTex;

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fTex = vTex;
}