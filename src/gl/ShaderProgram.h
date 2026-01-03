#pragma once
#include "Shader.h"
#include "VertexBuffer.h"
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
	int32_t location; // -1 = not in shader or optimized out
	uniform_type type;
};

struct VertexAtrrArg {
	int16_t numValues;
	int16_t valueType;
	int16_t normalized;
	const char* varName;
};

struct UniformArg {
	const char* name;
	uniform_type type;
};

extern int g_active_shader_program;

class ShaderProgram
{
public:
	const char* name;
	uint ID; // OpenGL program ID
	bool compiled;

	Shader* vShader; // vertex shader
	Shader* fShader; // fragment shader

	mat4x4* projMat;
	mat4x4* viewMat;
	mat4x4* modelMat;

	unordered_map<string, ShaderUniform> uniforms; // custom uniforms
	unordered_set<string> loggedErrors; // prevent error spam

	VertexAttr attributes[MAX_VERTEX_ATTRIBUTES];
	int numAttributes = 0;
	int vertexSize = 0;

	// Creates a shader program to replace the fixed-function pipeline
	ShaderProgram(const char* name);
	~ShaderProgram(void);

	void compile(const char* vshaderSource, const char* fshaderSource);

	void clearAttributes();

	// use this shader program instead of the fixed function pipeline.
	// to go back to normal opengl rendering, use this:
	// glUseProgramObject(0);
	void bind();

	void removeShader(int ID);

	void setMatrixes(mat4x4* model, mat4x4* view, mat4x4* proj, mat4x4* modelView, mat4x4* modelViewProj);

	// Find the the modelView and modelViewProjection matrices
	// used in the shader code, so that we can update them.
	void setMatrixNames(const char* modelViewMat, const char* modelViewProjMat);

	// get the location of a uniform in a linked program
	// set inShader to false if the uniform is not part of the current uploaded version
	void addUniform(const char* uniformName, uniform_type type);

	void addUniforms(const std::vector<UniformArg>& args);

	void addAttribute(int numValues, int valueType, int normalized, const char* varName, bool inShader = true);
	void addAttribute(const VertexAttr& attr);
	void addAttributes(const std::vector<VertexAtrrArg>& attr);

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

	int calcMemoryUsage();

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
