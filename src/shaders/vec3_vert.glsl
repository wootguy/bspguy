// object variables
uniform mat4 modelViewProjection;
uniform vec4 color;

// vertex variables
attribute vec3 vPosition;

// fragment variables
varying vec4 fColor;

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fColor = color;
}