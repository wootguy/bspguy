varying vec2 fTex;

uniform sampler2D sTex;

void main()
{
	gl_FragColor = texture2D(sTex, fTex);
}