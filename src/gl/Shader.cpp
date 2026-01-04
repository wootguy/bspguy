#include <GL/glew.h>
#include "Shader.h"
#include "util.h"

Shader::Shader( const char * sourceCode, const char* header, int shaderType )
{
	// Create Shader And Program Objects
	ID = glCreateShader(shaderType);

	static char sourceBuffer[1024 * 512];
	strcpy_safe(sourceBuffer, header, sizeof(sourceBuffer));
	strcat_safe(sourceBuffer, sourceCode, sizeof(sourceBuffer));
	const char* code = sourceBuffer;
	glShaderSource(ID, 1, &code, NULL);
	
	glCompileShader(ID);

	const char* shaderTypeName = "<unknown type>";

	switch (shaderType) {
	case GL_VERTEX_SHADER:
		shaderTypeName = "Vertex";
		break;
	case GL_FRAGMENT_SHADER:
		shaderTypeName = "Fragment";
		break;
	}

	int success;
	glGetShaderiv(ID, GL_COMPILE_STATUS, &success);
	if (success != GL_TRUE)
	{
		static char log[1024];
		int len;
		glGetShaderInfoLog(ID, 1024, &len, log);
		log[1023] = 0;
		logf("Failed to compile %s shader\n", shaderTypeName);
		logf(log);
		logf("\n");
		if (len > 1024)
			logf("Log too big to fit!");
	}

	compiled = success;
}


Shader::~Shader(void)
{
	glDeleteShader(ID);
}

