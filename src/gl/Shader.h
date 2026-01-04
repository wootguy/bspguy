#pragma once

class Shader
{
public:
	unsigned int ID;
	bool compiled;
	
    /*
    	Create and compile a shader from source
    	shaderType - GL_VERTEX_SHADER_ARB, GL_FRAGMENT_SHADER_ARB
    */
	Shader(const char * sourceCode, const char* header, int shaderType);
	~Shader(void);
};

