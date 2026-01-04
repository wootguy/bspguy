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
	const char* name;
};

struct ShaderCompileFlag {
	const char* varName;
	int enableBit;
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

#define MAX_SHADER_COMPILE_FLAGS 4 // 16 unique compilations of a shader
#define MAX_SHADER_COMPILES (1 << MAX_SHADER_COMPILE_FLAGS)
#define SHADER_COMPILE_FLAGS_MASK (MAX_SHADER_COMPILES - 1)

class ShaderProgram
{
public:
	const char* name;
	
	uint programIds[MAX_SHADER_COMPILES];	// OpenGL program ID (unique per compilation)
	Shader* vShader[MAX_SHADER_COMPILES];	// vertex shaders
	Shader* fShader[MAX_SHADER_COMPILES];	// fragment shaders
	bool compiled;

	mat4x4* projMat;
	mat4x4* viewMat;
	mat4x4* modelMat;

	unordered_map<string, ShaderUniform> uniforms[MAX_SHADER_COMPILES]; // custom uniforms
	unordered_set<string> loggedErrors; // prevent error spam

	VertexAttr attributes[MAX_SHADER_COMPILES][MAX_VERTEX_ATTRIBUTES];
	int numAttributes = 0;
	int vertexSize = 0;

	// flags which generate a unique program per unique combination
	ShaderCompileFlag compileFlags[MAX_SHADER_COMPILE_FLAGS];
	int numCompileFlags = 0;
	int numPrograms = 1;

	// Creates a shader program to replace the fixed-function pipeline
	ShaderProgram(const char* name);
	~ShaderProgram(void);

	// glslVersion = "120" for OpenGL 2.1
	void compile(const char* vshaderSource, const char* fshaderSource, const char* glslVersion);

	void clearAttributes();

	// call before drawing any buffers
	void bind();

	// select sub-program to bind via compile flag bits
	void bind(int enableBits);

	inline int getActiveProgramIndex() { return activeCompileFlags & SHADER_COMPILE_FLAGS_MASK; }
	int getActiveProgramId() { return programIds[getActiveProgramIndex()]; }

	void setMatrixes(mat4x4* model, mat4x4* view, mat4x4* proj, mat4x4* modelView, mat4x4* modelViewProj);

	// Find the the modelView and modelViewProjection matrices
	// used in the shader code, so that we can update them.
	void setMatrixNames(const char* modelViewMat, const char* modelViewProjMat);

	// add a compiler definition flag for toggling shader code without using uniforms (faster)
	// enableBit is both a unique ID within this shader and must occupy a single bit so that
	// you can enable multiple flags at the same time.
	void addCompileFlag(int enableBit, const char* varName);

	void addAttribute(int numValues, int valueType, int normalized, const char* varName, bool inShader = true);
	void addAttribute(const VertexAttr& attr);
	void addAttributes(const std::vector<VertexAtrrArg>& attr);

	// get the location of a uniform in a linked program
	void addUniform(const char* uniformName, uniform_type type);
	void addUniforms(const std::vector<UniformArg>& args);

	void setUniform(string uniformName, float value, bool allPrograms = false);
	void setUniform(string uniformName, vec2 value, bool allPrograms = false);
	void setUniform(string uniformName, vec3 values, bool allPrograms = false);
	void setUniform(string uniformName, vec4 values, bool allPrograms = false);
	void setUniform(string uniformName, int value, bool allPrograms = false);
	void setUniform(string uniformName, int value0, int value1, bool allPrograms = false);
	void setUniform(string uniformName, int value0, int value1, int value2, bool allPrograms = false);
	void setUniform(string uniformName, int value0, int value1, int value2, int value3, bool allPrograms = false);

	// upload float/vec/mat uniform value(s)
	void setUniform(string uniformName, float* values, int count=1, bool allPrograms = false);

	// upload int/ivec uniform value(s)
	void setUniform(string uniformName, int* values, int count=1, bool allPrograms = false);

	// upload the model, view, and projection matrices to the shader (or fixed-funcion pipe)
	void updateMatrixes();

	// save/restore matrices
	void pushMatrix(int matType);
	void popMatrix(int matType);

	int calcMemoryUsage();

private:
	// uniforms
	uint modelViewID[MAX_SHADER_COMPILES];
	uint modelViewProjID[MAX_SHADER_COMPILES];

	// computed from model, view, and projection matrices
	mat4x4* modelViewProjMat; // for transforming vertices onto the screen
	mat4x4* modelViewMat;

	// stores previous states of matrices
	std::vector<mat4x4> matStack[3];

	int activeCompileFlags;

	void link(int programIdx);

	ShaderUniform getUniform(string name);

	void initUniform(ShaderUniform& uniform);

	const char* getShaderCodeHeader(int enableBits, const char* glslVersion);
};
