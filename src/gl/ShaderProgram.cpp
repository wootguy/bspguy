#include <GL/glew.h>
#include "ShaderProgram.h"
#include "util.h"
#include <string.h>

int g_active_shader_program;

ShaderProgram::ShaderProgram(const char* name)
{
	this->name = name;
	compiled = false;
	memset(programIds, -1, sizeof(programIds));
	memset(compileSkipFlags, false, sizeof(compileSkipFlags));
	memset(vShader, 0, sizeof(vShader));
	memset(fShader, 0, sizeof(fShader));
	memset(modelViewID, -1, sizeof(modelViewID));
	memset(modelViewProjID, -1, sizeof(modelViewProjID));
}

void ShaderProgram::compile(const char* vshaderSource, const char* fshaderSource, const char* glslVersion) {
	for (int i = 0; i < MAX_SHADER_COMPILES; i++) {
		int programId = programIds[i];

		if (vShader[i]) {
			glDetachShader(programId, vShader[i]->ID);
			delete vShader[i];
			vShader[i] = NULL;
		}
		if (fShader[i]) {
			glDetachShader(programId, fShader[i]->ID);
			delete fShader[i];
			fShader[i] = NULL;
		}
	}
	
	for (int i = 0; i < numPrograms; i++) {
		if (compileSkipFlags[i]) {
			programIds[i] = -1;
			continue;
		}

		const char* header = getShaderCodeHeader(i, glslVersion);
		vShader[i] = new Shader(vshaderSource, header, GL_VERTEX_SHADER);
		fShader[i] = new Shader(fshaderSource, header, GL_FRAGMENT_SHADER);
		link(i);
		g_renderStats.numShaders++;
	}
}

void ShaderProgram::clearAttributes() {
	numAttributes = 0;
	vertexSize = 0;
	numCompileFlags = 0;
	numPrograms = 1;

	for (int i = 0; i < MAX_SHADER_COMPILES; i++)
		uniforms[i].clear();
}

void ShaderProgram::link(int programIdx)
{
	uint& ID = programIds[programIdx];

	// Create Shader And Program Objects
	ID = glCreateProgram();
	// Attach The Shader Objects To The Program Object
	glAttachShader(ID, vShader[programIdx]->ID);
	glAttachShader(ID, fShader[programIdx]->ID);

	glLinkProgram(ID);

	int success;
	glGetProgramiv(ID, GL_LINK_STATUS, &success);
	if (success != GL_TRUE)
	{
		static char log[1024];
		int len;
		glGetProgramInfoLog(ID, 1024, &len, log);
		errorf("Failed to link %s shader program:\n", name);
		errorf(log);
		errorf("\n");
		if (len > 1024)
			errorf("Log too big to fit!\n");
		g_renderStats.numShadersFailed++;
		glDeleteProgram(ID);
		ID = -1;
	}
	else {
		compiled = true;
	}
}


ShaderProgram::~ShaderProgram(void)
{
	for (int i = 0; i < numPrograms; i++) {
		glDeleteProgram(programIds[i]);
		delete vShader[i];
		delete fShader[i];
	}
}

void ShaderProgram::bind(int enableBits) {
	if (compileSkipFlags[enableBits]) {
		errorf("Shader %s skipped compilation for bits %d. Cannot bind.\n", name, enableBits);
		return;
	}
	activeCompileFlags = enableBits & SHADER_COMPILE_FLAGS_MASK;
	bind();
}

void ShaderProgram::bind()
{
	int id = getActiveProgramId();

	if (g_active_shader_program != id)
	{
		g_renderStats.numShaderBinds++;
		g_active_shader_program = id;
		glUseProgram(id);
	}
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
	int idx = getActiveProgramIndex();

	*modelViewMat = *viewMat * *modelMat;
	*modelViewProjMat = *projMat * *modelViewMat;
	*modelViewMat = modelViewMat->transpose();
	*modelViewProjMat = modelViewProjMat->transpose();
	g_renderStats.numMatrixUploads += 2;

	if (modelViewID[idx] != -1)
		glUniformMatrix4fv(modelViewID[idx], 1, false, (float*)modelViewMat);
	if (modelViewProjID[idx] != -1)
		glUniformMatrix4fv(modelViewProjID[idx], 1, false, (float*)modelViewProjMat);
}

void ShaderProgram::setMatrixNames(const char* modelViewMat, const char* modelViewProjMat) {
	for (int i = 0; i < numPrograms; i++) {
		if (compileSkipFlags[i])
			continue;
		bind(i);
		int id = getActiveProgramId();

		if (id == -1)
			continue;

		if (modelViewMat != NULL)
		{
			modelViewID[i] = glGetUniformLocation(id, modelViewMat);
			if (modelViewID[i] == -1)
				logf("Could not find %d matrix in shader %s (idx %d)\n", modelViewMat, name, i);
		}
		if (modelViewProjMat != NULL)
		{
			modelViewProjID[i] = glGetUniformLocation(id, modelViewProjMat);
			if (modelViewProjID[i] == -1)
				logf("Could not find %d matrix in shader %s (idx %d)\n", modelViewProjMat, name, i);
		}
	}
}

void ShaderProgram::addCompileFlag(int enableBit, const char* varName) {
	if (numCompileFlags >= MAX_SHADER_COMPILE_FLAGS) {
		errorf("Max compiler flags exceeded for shader %s\n", name);
		return;
	}

	if (enableBit == 0 || enableBit > (1 << (MAX_SHADER_COMPILE_FLAGS-1))) {
		errorf("Invalid enable bit for shader flag %s in shader %s\n", varName, name);
		return;
	}

	ShaderCompileFlag& flag = compileFlags[numCompileFlags++];
	flag.enableBit = enableBit;
	flag.varName = varName;

	numPrograms *= 2;
}

void ShaderProgram::skipCompileBits(int skipBits, bool mutuallyExclusive) {
	if (mutuallyExclusive) {
		for (int i = 0; i < MAX_SHADER_COMPILES; i++) {
			if ((i & skipBits) == skipBits) {
				compileSkipFlags[i] = true;
			}
		}
	}
	else {
		compileSkipFlags[skipBits & SHADER_COMPILE_FLAGS_MASK] = true;
	}
	
}

void ShaderProgram::addAttribute(int numValues, int valueType, int normalized, const char* varName, bool inShader) {
	VertexAttr attribute(numValues, valueType, -1, normalized, varName);

	addAttribute(attribute);
}

void ShaderProgram::addAttributes(const std::vector<VertexAtrrArg>& attr) {
	for (const VertexAtrrArg& a : attr) {
		addAttribute(VertexAttr(a.numValues, a.valueType, -1, a.normalized, a.varName));
	}
}

void ShaderProgram::addAttribute(const VertexAttr& attrib) {
	vertexSize += attrib.size;
	int i = numAttributes;

	if (i >= MAX_VERTEX_ATTRIBUTES) {
		logf("Too many vertex attributes in shader %s!\n", name);
		return;
	}

	for (int k = 0; k < numPrograms; k++) {
		if (compileSkipFlags[k])
			continue;
		bind(k);
		VertexAttr& a = attributes[k][i];
		a = attrib;
		a.handle = glGetAttribLocation(g_active_shader_program, a.varName);

		if (a.handle == -1) {
			// don't care about missing attritubes that are optimized out depending on compile flags
			//logf("Could not find vertex attribute '%s' in shader %s\n", a.varName, name);
		}
	}
	
	numAttributes++;
}

void ShaderProgram::initUniform(ShaderUniform& uniform) {

	for (int i = 0; i < numPrograms; i++) {
		if (compileSkipFlags[i] || programIds[i] == -1)
			continue;
		bind(i);

		uniform.location = glGetUniformLocation(programIds[i], uniform.name);

		glCheckError("Finding uniform in shadder");

		glGetError(); // make sure the next error is for the uniform
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
			errorf("ERROR: Unhandled uniform type for %s in shader %s.\n", uniform.name, name);
			break;
		}

		int uniError = glGetError();
		if (uniError == 1282) {
			errorf("ERROR: Wrong uniform type set for %s in shader %s\n", uniform.name, name);
			return;
		}
		else if (uniError != 0) {
			errorf("ERROR: Got OpenGL error %d initializing uniform %s in shader %s\n", uniError,
				uniform.name, name);
			return;
		}

		uniforms[i][uniform.name] = uniform;
	}
}

void ShaderProgram::addUniform(const char* uniformName, uniform_type type) {
	if (type >= UNIFORM_TYPES) {
		errorf("ERROR: Invalid uniform type %d set in %s shader\n", type, name);
		return;
	}

	ShaderUniform uniform;
	uniform.type = type;
	uniform.location = -1;
	uniform.name = uniformName;
	initUniform(uniform);
}

void ShaderProgram::addUniforms(const std::vector<UniformArg>& args) {
	for (const UniformArg& a : args) {
		addUniform(a.name, a.type);
	}
}

void ShaderProgram::setUniform(string uniformName, float value, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, value, false);
		}
		bind(oldEnableBits);
		return;
	}

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
		errorf("ERROR: Can't set uniform %s as a float in shader %s.\n", uniformName.c_str(), name);
	}
}

void ShaderProgram::setUniform(string uniformName, vec2 value, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, value, false);
		}
		bind(oldEnableBits);
	}

	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_VEC2) {
		glUniform2f(uniform.location, value.x, value.y);
	}
	else {
		errorf("ERROR: Can't set uniform %s as a vec2 in shader %s.\n", uniformName.c_str(), name);
	}
}

void ShaderProgram::setUniform(string uniformName, vec3 value, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, value, false);
		}
		bind(oldEnableBits);
	}

	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_VEC3) {
		glUniform3f(uniform.location, value.x, value.y, value.z);
	}
	else {
		errorf("ERROR: Can't set uniform %s as a vec3 in shader %s.\n", uniformName.c_str(), name);
	}
}

void ShaderProgram::setUniform(string uniformName, vec4 value, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, value, false);
		}
		bind(oldEnableBits);
	}

	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_VEC4) {
		glUniform4f(uniform.location, value.x, value.y, value.z, value.w);
	}
	else {
		errorf("ERROR: Can't set uniform %s as a vec4 in shader %s.\n", uniformName.c_str(), name);
	}
}

void ShaderProgram::setUniform(string uniformName, int value, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, value, false);
		}
		bind(oldEnableBits);
	}

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
		errorf("ERROR: Can't set uniform %s as an int in shader %s.\n", uniformName.c_str(), name);
	}
}

void ShaderProgram::setUniform(string uniformName, int value, int value2, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, value, value2, false);
		}
		bind(oldEnableBits);
	}

	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_IVEC2) {
		glUniform2i(uniform.location, value, value2);
	}
	else {
		errorf("ERROR: Can't set uniform %s as an ivec2 in shader %s.\n", uniformName.c_str(), name);
	}
}

void ShaderProgram::setUniform(string uniformName, int value, int value2, int value3, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, value, value2, value3, false);
		}
		bind(oldEnableBits);
	}

	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_IVEC3) {
		glUniform3i(uniform.location, value, value2, value3);
	}
	else {
		errorf("ERROR: Can't set uniform %s as an ivec3 in shader %s.\n", uniformName.c_str(), name);
	}
}

void ShaderProgram::setUniform(string uniformName, int value, int value2, int value3, int value4, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, value, value2, value3, value4, false);
		}
		bind(oldEnableBits);
	}

	ShaderUniform uniform = getUniform(uniformName);

	if (uniform.location == -1)
		return;

	g_renderStats.numUniformsUploaded++;

	if (uniform.type == UNIFORM_IVEC4) {
		glUniform4i(uniform.location, value, value2, value3, value4);
	}
	else {
		errorf("ERROR: Can't set uniform %s as an ivec4 in shader %s.\n", uniformName.c_str(), name);
	}
}

void ShaderProgram::setUniform(string uniformName, float* values, int count, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, values, count, false);
		}
		bind(oldEnableBits);
	}
	
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
		errorf("ERROR: Can't set uniform %s as floats in shader %s.\n", uniformName.c_str(), name);
		break;
	}
}


void ShaderProgram::setUniform(string uniformName, int* values, int count, bool allPrograms) {
	if (allPrograms) {
		int oldEnableBits = activeCompileFlags;
		for (int i = 0; i < numPrograms; i++) {
			if (compileSkipFlags[i] || programIds[i] == -1)
				continue;
			bind(i);
			setUniform(uniformName, values, count, false);
		}
		bind(oldEnableBits);
	}
	
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
		errorf("ERROR: Can't set uniform %s as ints in %s shader.\n", uniformName.c_str(), name);
		break;
	}
}

ShaderUniform ShaderProgram::getUniform(string uniformName) {
	int idx = getActiveProgramIndex();

	ShaderUniform bad;
	bad.location = -1;
	bad.type = UNIFORM_FLOAT;

	if (programIds[idx] == -1) // shader not compiled
		return bad;

	auto uni = uniforms[idx].find(uniformName);

	if (uni == uniforms[idx].end()) {
		string error = cstrf("ERROR: Uniform %s was not added to shader %s (idx %d)\n",
			uniformName.c_str(), name, idx);

		if (loggedErrors.count(error) == 0) {
			errorf(error.c_str());
			loggedErrors.insert(error);
		}
		
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

int ShaderProgram::calcMemoryUsage() {
	int bytes = sizeof(ShaderProgram);

	for (int i = 0; i < MAX_SHADER_COMPILES; i++) {
		for (auto item : uniforms[i]) {
			bytes += item.first.size() + sizeof(string) + sizeof(ShaderUniform);
		}

		bytes += vShader[i] ? sizeof(Shader) : 0;
		bytes += fShader[i] ? sizeof(Shader) : 0;
	}

	for (const string& item : loggedErrors) {
		bytes += item.size() + sizeof(string);
	}

	for (int i = 0; i < 3; i++) {
		bytes += matStack[i].size() + sizeof(mat4x4);
	}

	return bytes;
}

const char* ShaderProgram::getShaderCodeHeader(int enableBits, const char* glslVersion) {
	static char buf[512];

	strcpy_safe(buf, cstrf("#version %s\n", glslVersion), sizeof(buf));

	for (int i = 0; i < numCompileFlags; i++) {
		if (enableBits & (1 << i)) {
			strcat_safe(buf, cstrf("#define %s\n", compileFlags[i].varName), sizeof(buf));
		}
	}

	strcat_safe(buf, "#line 1\n", sizeof(buf));

	return buf;
}