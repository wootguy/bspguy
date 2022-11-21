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

	static char log[512];
	int len;
	glGetShaderInfoLog(ID, 512, &len, log);
	if (success != GL_TRUE)
		logf("Shader Compilation Failed (type %d)\n", shaderType);
		
	if (len > 0) {
		logf("Shader Compilation output:\n");
		logf(log);
		if (len > 512)
			logf("\nLog too big to fit!");
	}
}


Shader::~Shader(void)
{
	glDeleteShader(ID);
}

