#include <GL/glew.h>
#include "ShaderProgram.h"
#include "util.h"
#include <string.h>

int g_active_shader_program;

ShaderProgram::ShaderProgram(string name)
{
	this->name = name;
	compiled = false;
	modelViewID = modelViewProjID = -1;
	vShader = fShader = NULL;
}

void ShaderProgram::compile(const char* vshaderSource, const char* fshaderSource) {
	if (vShader) {
		removeShader(vShader->ID);
		delete vShader;
		vShader = NULL;
	}
	if (fShader) {
		removeShader(fShader->ID);
		delete fShader;
		fShader = NULL;
	}
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
		logf("Failed to link %s shader program:\n", name.c_str());
		logf(log);
		logf("\n");
		if (len > 1024)
			logf("Log too big to fit!\n");
		delete[] log;
	}
	else {
		compiled = true;
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
		g_renderStats.numShaderBinds++;
		g_active_shader_program = ID;
		glUseProgram(ID);
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
	g_renderStats.numMatrixUploads += 2;

	if (modelViewID != -1)
		glUniformMatrix4fv(modelViewID, 1, false, (float*)modelViewMat);
	if (modelViewProjID != -1)
		glUniformMatrix4fv(modelViewProjID, 1, false, (float*)modelViewProjMat);
}

void ShaderProgram::setMatrixNames(const char* modelViewMat, const char* modelViewProjMat)
{
	bind();

	if (modelViewMat != NULL)
	{
		modelViewID = glGetUniformLocation(ID, modelViewMat);
		if (modelViewID == -1)
			logf("Could not find modelView uniform: %s\n", modelViewMat);
	}
	if (modelViewProjMat != NULL)
	{
		modelViewProjID = glGetUniformLocation(ID, modelViewProjMat);
		if (modelViewProjID == -1)
			logf("Could not find modelViewProjection uniform: %s\n", modelViewProjMat);
	}
}

void ShaderProgram::setVertexAttributeNames(const char* posAtt, const char* colorAtt, const char* texAtt, const char* normAtt)
{
	bind();

	if (posAtt != NULL)
	{
		vposID = glGetAttribLocation(ID, posAtt);
		if (vposID == -1) logf("Could not find vposition attribute: %s in %s shader\n", posAtt, name.c_str());
	}
	if (colorAtt != NULL)
	{
		vcolorID = glGetAttribLocation(ID, colorAtt);
		if (vcolorID == -1) logf("Could not find vcolor attribute: %s in %s shader\n", colorAtt, name.c_str());
	}
	if (texAtt != NULL)
	{
		vtexID = glGetAttribLocation(ID, texAtt);
		if (vtexID == -1) logf("Could not find vtexture attribute: %s in %s shader\n", texAtt, name.c_str());
	}
	if (normAtt != NULL)
	{
		vnormID = glGetAttribLocation(ID, normAtt);
		if (vnormID == -1) logf("Could not find vnormal attribute: %s in %s shader\n", normAtt, name.c_str());
	}
}

void ShaderProgram::addUniform(string uniformName, uniform_type type) {
	if (type >= UNIFORM_TYPES) {
		logf("ERROR: Invalid uniform type %d set in %s shader\n", type, name.c_str());
		return;
	}

	bind();

	ShaderUniform uniform;
	uniform.location = glGetUniformLocation(ID, uniformName.c_str());
	uniform.type = type;
	
	if (uniform.location == -1) {
		logf("ERROR: Uniform %s not found in %s shader\n", uniformName.c_str(), name.c_str());
		return;
	}

	int clearError = glGetError();
	if (clearError)
		logf("Cleared OpenGL error %d\n", clearError);

	static float emptyMatrix[16] = { 0.0f };

	// test uniform type and initialize to 0
	switch (uniform.type) {
	case UNIFORM_FLOAT:
		glUniform1f(uniform.location, 0.0f);
		break;
	case UNIFORM_VEC2:
		glUniform2f(uniform.location, 0.0f, 0.0f);
		break;
	case UNIFORM_VEC3:
		glUniform3f(uniform.location, 0.0f, 0.0f, 0.0f);
		break;
	case UNIFORM_VEC4:
		glUniform4f(uniform.location, 0.0f, 0.0f, 0.0f, 0.0f);
		break;
	case UNIFORM_MAT2:
		glUniformMatrix2fv(uniform.location, 1, false, emptyMatrix);
		break;
	case UNIFORM_MAT3:
		glUniformMatrix3fv(uniform.location, 1, false, emptyMatrix);
		break;
	case UNIFORM_MAT4:
		glUniformMatrix4fv(uniform.location, 1, false, emptyMatrix);
		break;
	case UNIFORM_INT:
		glUniform1i(uniform.location, 0);
		break;
	case UNIFORM_IVEC2:
		glUniform2i(uniform.location, 0, 0);
		break;
	case UNIFORM_IVEC3:
		glUniform3i(uniform.location, 0, 0, 0);
		break;
	case UNIFORM_IVEC4:
		glUniform4i(uniform.location, 0, 0, 0, 0);
		break;
	default:
		logf("ERROR: Unhandled uniform type for %s in shader %s.\n", uniformName.c_str(), name.c_str());
		break;
	}

	int uniError = glGetError();
	if (uniError == 1282) {
		logf("ERROR: Wrong uniform type set for %s in shader %s\n", uniformName.c_str(), name.c_str());
		return;
	}
	else if (uniError != 0) {
		logf("ERROR: Got OpenGL error %d initializing uniform %s in shader %s\n", uniError,
			uniformName.c_str(), name.c_str());
		return;
	}

	uniforms[uniformName] = uniform;
}

void ShaderProgram::setUniform(string uniformName, float value) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_FLOAT) {
		glUniform1f(uniform.location, value);
	}
	else if (uniform.type == UNIFORM_INT) {
		glUniform1i(uniform.location, value); // for ease of use with operator overloaded funcs
	}
	else {
		logf("ERROR: Can't set uniform %s as a float in shader %s.\n", uniformName.c_str(), name.c_str());
	}
}

void ShaderProgram::setUniform(string uniformName, vec2 value) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_VEC2) {
		glUniform2f(uniform.location, value.x, value.y);
	}
	else {
		logf("ERROR: Can't set uniform %s as a vec2 in shader %s.\n", uniformName.c_str(), name.c_str());
	}
}

void ShaderProgram::setUniform(string uniformName, vec3 value) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_VEC3) {
		glUniform3f(uniform.location, value.x, value.y, value.z);
	}
	else {
		logf("ERROR: Can't set uniform %s as a vec3 in shader %s.\n", uniformName.c_str(), name.c_str());
	}
}

void ShaderProgram::setUniform(string uniformName, vec4 value) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_VEC4) {
		glUniform4f(uniform.location, value.x, value.y, value.z, value.w);
	}
	else {
		logf("ERROR: Can't set uniform %s as a vec4 in shader %s.\n", uniformName.c_str(), name.c_str());
	}
}

void ShaderProgram::setUniform(string uniformName, int value) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_INT) {
		glUniform1i(uniform.location, value);
	}
	else if (uniform.type == UNIFORM_FLOAT) {
		glUniform1f(uniform.location, value); // for ease of use with operator overloaded funcs
	}
	else {
		logf("ERROR: Can't set uniform %s as an int in shader %s.\n", uniformName.c_str(), name.c_str());
	}
}

void ShaderProgram::setUniform(string uniformName, int value, int value2) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_IVEC2) {
		glUniform2i(uniform.location, value, value2);
	}
	else {
		logf("ERROR: Can't set uniform %s as an ivec2 in shader %s.\n", uniformName.c_str(), name.c_str());
	}
}

void ShaderProgram::setUniform(string uniformName, int value, int value2, int value3) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_IVEC3) {
		glUniform3i(uniform.location, value, value2, value3);
	}
	else {
		logf("ERROR: Can't set uniform %s as an ivec3 in shader %s.\n", uniformName.c_str(), name.c_str());
	}
}

void ShaderProgram::setUniform(string uniformName, int value, int value2, int value3, int value4) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_IVEC4) {
		glUniform4i(uniform.location, value, value2, value3, value4);
	}
	else {
		logf("ERROR: Can't set uniform %s as an ivec4 in shader %s.\n", uniformName.c_str(), name.c_str());
	}
}

void ShaderProgram::setUniform(string uniformName, float* values, int count) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	switch (uniform.type) {
	case UNIFORM_FLOAT:
		glUniform1fv(uniform.location, count, values);
		break;
	case UNIFORM_VEC2:
		glUniform2fv(uniform.location, count / 2, values);
		break;
	case UNIFORM_VEC3:
		glUniform3fv(uniform.location, count / 3, values);
		break;
	case UNIFORM_VEC4:
		glUniform4fv(uniform.location, count / 4, values);
		break;
	case UNIFORM_MAT2:
		glUniformMatrix2fv(uniform.location, count / 4, false, values);
		break;
	case UNIFORM_MAT3:
		glUniformMatrix3fv(uniform.location, count / 9, false, values);
		break;
	case UNIFORM_MAT4:
		glUniformMatrix4fv(uniform.location, count / 16, false, values);
		break;
	default:
		logf("ERROR: Can't set uniform %s as floats in shader %s.\n", uniformName.c_str(), name.c_str());
		break;
	}
}


void ShaderProgram::setUniform(string uniformName, int* values, int count) {
	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	switch (uniform.type) {
	case UNIFORM_INT:
		glUniform1iv(uniform.location, count, values);
		break;
	case UNIFORM_IVEC2:
		glUniform2iv(uniform.location, count / 2, values);
		break;
	case UNIFORM_IVEC3:
		glUniform3iv(uniform.location, count / 3, values);
		break;
	case UNIFORM_IVEC4:
		glUniform4iv(uniform.location, count / 4, values);
		break;
	default:
		logf("ERROR: Can't set uniform %s as ints in %s shader.\n", uniformName.c_str(), name.c_str());
		break;
	}
}

ShaderUniform ShaderProgram::getUniform(string uniformName) {
	auto uni = uniforms.find(uniformName);

	if (uni == uniforms.end()) {
		string error = "ERROR: Uniform " + uniformName + " was not added to " + name + " shader\n";

		if (loggedErrors.count(error) == 0) {
			logf(error.c_str());
			loggedErrors.insert(error);
		}

		ShaderUniform bad;
		bad.location = -1;
		bad.type = UNIFORM_FLOAT;
		return bad;
	}

	return uni->second;
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

	updateMatrixes(); // TODO: this is expensive but i don't want to deal with the bugs right now. It breaks point ents.
}