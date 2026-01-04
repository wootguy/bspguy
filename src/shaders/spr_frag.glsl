varying vec2 fTex;
varying vec4 fColor;

uniform sampler2D sTex;

void main()
{
	gl_FragColor = texture2D(sTex, fTex) * fColor;
}