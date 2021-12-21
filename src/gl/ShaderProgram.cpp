#include <GL/glew.h>
#include "ShaderProgram.h"
#include "util.h"
#include <string>

static int g_active_shader_program;

ShaderProgram::ShaderProgram(const char* vshaderSource, const char* fshaderSource)
{
	modelViewID = modelViewProjID = -1;
	vShader = new Shader(vshaderSource, GL_VERTEX_SHADER);
	fShader = new Shader(fshaderSource, GL_FRAGMENT_SHADER);
	link();
}

void ShaderProgram::link()
{
	// Create Shader And Program Objects
	ID = glCreateProgram();
	// Attach The Shader Objects To The Program Object
	glAttachShader(ID, vShader->ID);
	glAttachShader(ID, fShader->ID);

	glLinkProgram(ID);

	int success;
	glGetProgramiv(ID, GL_LINK_STATUS, &success);
	if (success != GL_TRUE)
	{
		char* log = new char[1024];
		int len;
		glGetProgramInfoLog(ID, 1024, &len, log);
		logf("Shader Program Link Failure:\n");
		logf(log);
		if (len > 1024)
			logf("\nLog too big to fit!\n");
		delete[] log;
	}
}


ShaderProgram::~ShaderProgram(void)
{
	glDeleteProgram(ID);
	delete vShader;
	delete fShader;
}

void ShaderProgram::bind()
{
	if (g_active_shader_program != ID)
	{
		g_active_shader_program = ID;
		glUseProgram(ID);
		updateMatrixes();
	}
}

void ShaderProgram::removeShader(int shaderID)
{
	glDetachShader(ID, shaderID);
}

void ShaderProgram::setMatrixes(mat4x4* model, mat4x4* view, mat4x4* proj, mat4x4* modelView, mat4x4* modelViewProj)
{
	modelMat = model;
	viewMat = view;
	projMat = proj;
	modelViewMat = modelView;
	modelViewProjMat = modelViewProj;
}

void ShaderProgram::updateMatrixes()
{
	*modelViewMat = *viewMat * *modelMat;
	*modelViewProjMat = *projMat * *modelViewMat;
	*modelViewMat = modelViewMat->transpose();
	*modelViewProjMat = modelViewProjMat->transpose();

	if (modelViewID != -1)
		glUniformMatrix4fv(modelViewID, 1, false, (float*)modelViewMat);
	if (modelViewProjID != -1)
		glUniformMatrix4fv(modelViewProjID, 1, false, (float*)modelViewProjMat);
}

void ShaderProgram::setMatrixNames(const char* modelViewMat, const char* modelViewProjMat)
{
	if (modelViewMat)
	{
		modelViewID = glGetUniformLocation(ID, modelViewMat);
		if (modelViewID == -1)
			logf("Could not find modelView uniform: %s\n", modelViewMat);
	}
	if (modelViewProjMat)
	{
		modelViewProjID = glGetUniformLocation(ID, modelViewProjMat);
		if (modelViewProjID == -1)
			logf("Could not find modelViewProjection uniform: %s\n", modelViewProjMat);
	}
}

void ShaderProgram::setVertexAttributeNames(const char* posAtt, const char* colorAtt, const char* texAtt)
{
	if (posAtt)
	{
		vposID = glGetAttribLocation(ID, posAtt);
		if (vposID == -1) logf("Could not find vposition attribute: %s\n", posAtt);
	}
	if (colorAtt)
	{
		vcolorID = glGetAttribLocation(ID, colorAtt);
		if (vcolorID == -1) logf("Could not find vcolor attribute: %s\n", colorAtt);
	}
	if (texAtt)
	{
		vtexID = glGetAttribLocation(ID, texAtt);
		if (vtexID == -1) logf("Could not find vtexture attribute: %s\n", texAtt);
	}
}

void ShaderProgram::pushMatrix(int matType)
{
	if (matType & MAT_MODEL)	  matStack[0].push_back(*modelMat);
	if (matType & MAT_VIEW)		  matStack[1].push_back(*viewMat);
	if (matType & MAT_PROJECTION) matStack[2].push_back(*projMat);
}

void ShaderProgram::popMatrix(int matType)
{
	mat4x4* targets[3] = { modelMat, viewMat, projMat };
	for (int idx = 0, mask = 1; idx < 3; ++idx, mask <<= 1)
	{
		if (matType & mask)
		{
			std::vector<mat4x4>& stack = matStack[idx];
			if (!stack.empty())
			{
				*targets[idx] = stack[stack.size() - 1];
				stack.pop_back();
			}
			else
				logf("Can't pop matrix. Stack is empty.\n");
		}
	}

	updateMatrixes();
}
