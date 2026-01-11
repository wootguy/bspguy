// object variables
uniform mat4 modelViewProjection;
uniform vec4 colorMult;

// vertex variables
attribute vec3 vPosition;
attribute vec4 vColor;
attribute float vDist;
attribute float vDir;

// fragment variables
varying vec4 fColor;
varying float fDist;
varying float fDir;

void main()
{
	gl_Position = modelViewProjection * vec4(vPosition, 1);
	fColor = vColor * colorMult;
	fDist = vDist;
	fDir = vDir;
}