#pragma once
#include "Shader.h"
#include <vector>
#include "mat4x4.h"

enum mat_types
{
	MAT_MODEL = 1,
	MAT_VIEW = 2,
	MAT_PROJECTION = 4,
};

class ShaderProgram
{
public:
	unsigned int ID; // OpenGL program ID

	Shader* vShader; // vertex shader
	Shader* fShader; // fragment shader

	// commonly used vertex attributes
	unsigned int vposID;
	unsigned int vcolorID;
	unsigned int vtexID;

	mat4x4* projMat;
	mat4x4* viewMat;
	mat4x4* modelMat;

	// Creates a shader program to replace the fixed-function pipeline
	ShaderProgram(const char* vshaderSource, const char* fshaderSource);
	~ShaderProgram(void);

	// use this shader program instead of the fixed function pipeline.
	// to go back to normal opengl rendering, use this:
	// glUseProgramObject(0);
	void bind();

	void removeShader(int shaderID);

	void setMatrixes(mat4x4* model, mat4x4* view, mat4x4* proj, mat4x4* modelView, mat4x4* modelViewProj);

	// Find the the modelView and modelViewProjection matrices
	// used in the shader code, so that we can update them.
	void setMatrixNames(const char* modelViewMat, const char* modelViewProjMat);

	// Find the IDs for the common vertex attributes (position, color, texture coords, normals)
	void setVertexAttributeNames(const char* posAtt, const char* colorAtt, const char* texAtt);

	// upload the model, view, and projection matrices to the shader (or fixed-funcion pipe)
	void updateMatrixes();

	// save/restore matrices
	void pushMatrix(int matType);
	void popMatrix(int matType);

private:
	// uniforms
	unsigned int modelViewID;
	unsigned int modelViewProjID;

	// computed from model, view, and projection matrices
	mat4x4* modelViewProjMat; // for transforming vertices onto the screen
	mat4x4* modelViewMat;

	// stores previous states of matrices
	std::vector<mat4x4> matStack[3];

	void link();
};

