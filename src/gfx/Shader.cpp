#include <GL/glew.h>
#include "Shader.h"
#include "util.h"

using namespace std;

Shader::Shader( const char * sourceCode, int shaderType )
{
	// Create Shader And Program Objects
	ID = glCreateShader(shaderType);

	glShaderSource(ID, 1, &sourceCode, NULL);
	glCompileShader(ID);

	int success;
	glGetShaderiv(ID, GL_COMPILE_STATUS, &success);
	if (success != GL_TRUE)
	{
		char* log = new char[512];
		int len;
		glGetShaderInfoLog(ID, 512, &len, log);
		printf("Shader Compilation Failed (type %d)\n", shaderType);
		printf(log);
		if (len > 512)
			printf("\nLog too big to fit!");
		delete[] log;
	}
}


Shader::~Shader(void)
{
	glDeleteShader(ID);
}

