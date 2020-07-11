#pragma once
#include "types.h"

class Shader
{
public:
	unsigned int ID;
	
    /*
    	Create and compile a shader from source
    	shaderType - GL_VERTEX_SHADER_ARB, GL_FRAGMENT_SHADER_ARB
    */
	Shader(const char * sourceCode, int shaderType);
	~Shader(void);
};

