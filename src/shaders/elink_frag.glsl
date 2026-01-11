uniform float time;

varying vec4 fColor;
varying float fDist;
varying float fDir;

void main()
{
	float t;
	
	if (fDir == 0) {
		t = sin(time*4)*20; // bidirectional link
	} else {
		t = time * fDir * 50;
	}
	
	float d = fract((fDist + t) * 0.05);
	d = 0.5 + abs(d - 0.5);
	d = d*d*d;
	gl_FragColor = vec4(fColor.rgb*d, fColor.a);
}