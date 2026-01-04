varying vec4 fColor;

void main()
{
	gl_FragColor = fColor;
	
	#ifdef DEPTH_HACK
		gl_FragDepth = gl_FragCoord.z - 0.00001f; // smol hack to fix z fighting with polys and outlines (sprites)
	#endif
}