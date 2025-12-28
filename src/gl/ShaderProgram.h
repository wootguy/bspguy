#pragma once
#include "Shader.h"
#include <vector>
#include "mat4x4.h"
#include <unordered_map>
#include <unordered_set>

enum mat_types
{
	MAT_MODEL = 1,
	MAT_VIEW = 2,
	MAT_PROJECTION = 4,
};

// GLSL 1.20 uniform types
enum uniform_type {
	UNIFORM_FLOAT,
	UNIFORM_INT,

	UNIFORM_VEC2,
	UNIFORM_VEC3,
	UNIFORM_VEC4,

	UNIFORM_IVEC2,
	UNIFORM_IVEC3,
	UNIFORM_IVEC4,

	UNIFORM_MAT2,
	UNIFORM_MAT3,
	UNIFORM_MAT4,
	UNIFORM_TYPES
};

struct ShaderUniform {
	uint32_t location;
	uniform_type type;
};

extern int g_active_shader_program;

class ShaderProgram
{
public:
	string name;
	uint ID; // OpenGL program ID
	bool compiled;

	Shader* vShader; // vertex shader
	Shader* fShader; // fragment shader

	// commonly used vertex attributes
	uint vposID = -1;
	uint vcolorID = -1;
	uint vtexID = -1;
	uint vnormID = -1;

	mat4x4* projMat;
	mat4x4* viewMat;
	mat4x4* modelMat;

	unordered_map<string, ShaderUniform> uniforms; // custom uniforms
	unordered_set<string> loggedErrors; // prevent error spam

	// Creates a shader program to replace the fixed-function pipeline
	ShaderProgram(string name);
	~ShaderProgram(void);

	void compile(const char* vshaderSource, const char* fshaderSource);

	// use this shader program instead of the fixed function pipeline.
	// to go back to normal opengl rendering, use this:
	// glUseProgramObject(0);
	void bind();

	void removeShader(int ID);

	void setMatrixes(mat4x4* model, mat4x4* view, mat4x4* proj, mat4x4* modelView, mat4x4* modelViewProj);

	// Find the the modelView and modelViewProjection matrices
	// used in the shader code, so that we can update them.
	void setMatrixNames(const char* modelViewMat, const char* modelViewProjMat);

	// Find the IDs for the common vertex attributes (position, color, texture coords, normals)
	void setVertexAttributeNames(const char* posAtt, const char* colorAtt, const char* texAtt, const char* normAtt);

	// get the location of a uniform in a linked program
	void addUniform(string uniformName, uniform_type type);

	void setUniform(string uniformName, float value);
	void setUniform(string uniformName, vec2 value);
	void setUniform(string uniformName, vec3 values);
	void setUniform(string uniformName, vec4 values);
	void setUniform(string uniformName, int value);
	void setUniform(string uniformName, int value0, int value1);
	void setUniform(string uniformName, int value0, int value1, int value2);
	void setUniform(string uniformName, int value0, int value1, int value2, int value3);

	// upload float/vec/mat uniform value(s)
	void setUniform(string uniformName, float* values, int count=1);

	// upload int/ivec uniform value(s)
	void setUniform(string uniformName, int* values, int count=1);

	// upload the model, view, and projection matrices to the shader (or fixed-funcion pipe)
	void updateMatrixes();

	// save/restore matrices
	void pushMatrix(int matType);
	void popMatrix(int matType);

private:
	// uniforms
	uint modelViewID = -1;
	uint modelViewProjID = -1;

	// computed from model, view, and projection matrices
	mat4x4* modelViewProjMat; // for transforming vertices onto the screen
	mat4x4* modelViewMat;

	// stores previous states of matrices
	std::vector<mat4x4> matStack[3];

	void link();

	ShaderUniform getUniform(string name);
};
