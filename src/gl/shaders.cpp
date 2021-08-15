#include "shaders.h"
namespace Shaders
{
	// IMPORTANT: 
	// if you have an Nvidia GPU, compile shader code in GPU ShaderAnalyzer to be sure it works for AMD too.
	// AMD has a stricter GLSL compiler than Nvidia does.

	const char* g_shader_cVert_vertex =
		// object variables
		"uniform mat4 modelViewProjection;\n"
		"uniform vec4 colorMult;\n"

		// vertex variables
		"attribute vec3 vPosition;\n"
		"attribute vec4 vColor;\n"

		// fragment variables
		"varying vec4 fColor;\n"

		"void main()\n"
		"{\n"
		"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
		"	fColor = vColor * colorMult;\n"
		"}\n";


	const char* g_shader_cVert_fragment =
		"varying vec4 fColor;\n"

		"void main()\n"
		"{\n"
		"	float gamma = 1.5;\n"
		"	gl_FragColor = vec4(pow(fColor.rgb, vec3(1.0/gamma)), fColor.a);\n"
		"}\n";


	const char* g_shader_tVert_vertex =
		// object variables
		"uniform mat4 modelViewProjection;\n"

		// vertex variables
		"attribute vec3 vPosition;\n"
		"attribute vec2 vTex;\n"

		// fragment variables
		"varying vec2 fTex;\n"

		"void main()\n"
		"{\n"
		"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
		"	fTex = vTex;\n"
		"}\n";


	const char* g_shader_tVert_fragment =
		"varying vec2 fTex;\n"

		"uniform sampler2D sTex;\n"

		"void main()\n"
		"{\n"
		"	gl_FragColor = texture2D(sTex, fTex);\n"
		"}\n";



	const char* g_shader_multitexture_vertex =
		// object variables
		"uniform mat4 modelViewProjection;\n"

		// vertex variables
		"attribute vec3 vPosition;\n"
		"attribute vec2 vTex;\n"
		"attribute vec3 vLightmapTex0;\n"
		"attribute vec3 vLightmapTex1;\n"
		"attribute vec3 vLightmapTex2;\n"
		"attribute vec3 vLightmapTex3;\n"
		"attribute vec4 vColor;\n"

		// fragment variables
		"varying vec2 fTex;\n"
		"varying vec3 fLightmapTex0;\n"
		"varying vec3 fLightmapTex1;\n"
		"varying vec3 fLightmapTex2;\n"
		"varying vec3 fLightmapTex3;\n"
		"varying vec4 fColor;\n"

		"void main()\n"
		"{\n"
		"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
		"	fTex = vTex;\n"
		"	fLightmapTex0 = vLightmapTex0;\n"
		"	fLightmapTex1 = vLightmapTex1;\n"
		"	fLightmapTex2 = vLightmapTex2;\n"
		"	fLightmapTex3 = vLightmapTex3;\n"
		"	fColor = vColor;\n"
		"}\n";


	const char* g_shader_multitexture_fragment =
		"varying vec2 fTex;\n"
		"varying vec3 fLightmapTex0;\n"
		"varying vec3 fLightmapTex1;\n"
		"varying vec3 fLightmapTex2;\n"
		"varying vec3 fLightmapTex3;\n"
		"varying vec4 fColor;\n"

		"uniform sampler2D sTex;\n"
		"uniform sampler2D sLightmapTex0;\n"
		"uniform sampler2D sLightmapTex1;\n"
		"uniform sampler2D sLightmapTex2;\n"
		"uniform sampler2D sLightmapTex3;\n"

		"void main()\n"
		"{\n"
		"	vec3 lightmap = texture2D(sLightmapTex0, fLightmapTex0.xy).rgb * fLightmapTex0.z;\n"
		"	lightmap += texture2D(sLightmapTex1, fLightmapTex1.xy).rgb * fLightmapTex1.z;\n"
		"	lightmap += texture2D(sLightmapTex2, fLightmapTex2.xy).rgb * fLightmapTex2.z;\n"
		"	lightmap += texture2D(sLightmapTex3, fLightmapTex3.xy).rgb * fLightmapTex3.z;\n"
		"	vec3 color = texture2D(sTex, fTex).rgb * lightmap * fColor.rgb;\n"

		"	float gamma = 1.5;\n"
		"	gl_FragColor = vec4(pow(color, vec3(1.0/gamma)), fColor.a);\n"
		"}\n";

	const char* g_shader_fullbright_vertex =
		// object variables
		"uniform mat4 modelViewProjection;\n"

		// vertex variables
		"attribute vec3 vPosition;\n"
		"attribute vec2 vTex;\n"
		"attribute vec4 vColor;\n"

		// fragment variables
		"varying vec2 fTex;\n"
		"varying vec4 fColor;\n"

		"void main()\n"
		"{\n"
		"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
		"	fTex = vTex;\n"
		"	fColor = vColor;\n"
		"}\n";

	// vec4(0.86f, 0, 0, 1.0f); //highlight color (texture color*2)

	const char* g_shader_fullbright_fragment =
		"varying vec2 fTex;\n"
		"varying vec4 fColor;\n"

		"uniform sampler2D sTex;\n"

		"void main()\n"
		"{\n"
		"	gl_FragColor = texture2D(sTex, fTex) * fColor;\n"
		"}\n";
}