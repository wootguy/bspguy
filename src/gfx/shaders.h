
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