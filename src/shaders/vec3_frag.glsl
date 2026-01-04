varying vec4 fColor;

void main() 
{
	gl_FragColor = fColor;
	gl_FragDepth = gl_FragCoord.z;
}