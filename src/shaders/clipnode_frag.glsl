#version 120
uniform float wireframeThickness;
uniform float opacity;

varying vec4 fColor;
varying vec3 fBary;
varying vec3 fEdgeEnable;

void main()
{
	float gamma = 1.5;
	if (fColor.a == 0.0) discard;
	gl_FragColor = vec4(pow(fColor.rgb, vec3(1.0/gamma)), fColor.a*opacity);
	
	vec3 a = smoothstep(vec3(0.0), fwidth(fBary) * wireframeThickness, fBary);
	a = mix(vec3(1.0), a, fEdgeEnable);
	float dist = min(min(a.x, a.y), a.z);
	
	if (dist < 1.0) {
		gl_FragColor = vec4(0,0,0,1);
		return;
	}
	
	if (!gl_FrontFacing) {
		discard;
	}
}