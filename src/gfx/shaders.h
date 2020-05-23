
const char* g_shader_cVert_vertex =
// object variables
"uniform mat4 modelViewProjection;\n"

// vertex variables
"attribute vec3 vPosition;\n"
"attribute vec4 vColor;\n"

// fragment variables
"varying vec4 fColor;\n"

"void main()\n"
"{\n"
"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
"	fColor = vColor;\n"
"}\n";


const char* g_shader_cVert_fragment =
"varying vec4 fColor;\n"

"void main()\n"
"{\n"
"	gl_FragColor = fColor;\n"
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
"attribute vec2 vLightmapTex0;\n"
"attribute vec2 vLightmapTex1;\n"
"attribute vec2 vLightmapTex2;\n"
"attribute vec2 vLightmapTex3;\n"

// fragment variables
"varying vec2 fTex;\n"
"varying vec2 fLightmapTex0;\n"
"varying vec2 fLightmapTex1;\n"
"varying vec2 fLightmapTex2;\n"
"varying vec2 fLightmapTex3;\n"

"void main()\n"
"{\n"
"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
"	fTex = vTex;\n"
"	fLightmapTex0 = vLightmapTex0;\n"
"	fLightmapTex1 = vLightmapTex1;\n"
"	fLightmapTex2 = vLightmapTex2;\n"
"	fLightmapTex3 = vLightmapTex3;\n"
"}\n";


const char* g_shader_multitexture_fragment =
"uniform float lightmapScale0;\n"
"uniform float lightmapScale1;\n"
"uniform float lightmapScale2;\n"
"uniform float lightmapScale3;\n"

"varying vec2 fTex;\n"
"varying vec2 fLightmapTex0;\n"
"varying vec2 fLightmapTex1;\n"
"varying vec2 fLightmapTex2;\n"
"varying vec2 fLightmapTex3;\n"

"uniform sampler2D sTex;\n"
"uniform sampler2D sLightmapTex0;\n"
"uniform sampler2D sLightmapTex1;\n"
"uniform sampler2D sLightmapTex2;\n"
"uniform sampler2D sLightmapTex3;\n"

"void main()\n"
"{\n"
"	vec3 lightmap = texture2D(sLightmapTex0, fLightmapTex0).rgb * lightmapScale0;\n"
"	lightmap += texture2D(sLightmapTex1, fLightmapTex1).rgb * lightmapScale1;\n"
"	lightmap += texture2D(sLightmapTex2, fLightmapTex2).rgb * lightmapScale2;\n"
"	lightmap += texture2D(sLightmapTex3, fLightmapTex3).rgb * lightmapScale3;\n"
"	gl_FragColor = texture2D(sTex, fTex) * vec4(lightmap, 1);\n"
"}\n";