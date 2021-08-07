#pragma once
#include "vectors.h"

// A row-major 4x4 matrix for use in OpenGL shader programs
struct mat4x4
{
	float m[16];

	void loadIdentity();

	void perspective(float fov, float aspect, float near, float far);

	// Set up an orthographic projection matrix
	void ortho(float left, float right, float bottom, float top, float near, float far);

	void translate(float x, float y, float z);

	void scale(float x, float y, float z);

	void rotateX(float r);

	void rotateY(float r);

	void rotateZ(float r);

	// converts row-major matrix to column-major (for OpenGL)
	mat4x4 transpose();

	mat4x4 invert();

	float& operator ()(size_t idx)
	{
		return m[idx];
	}

	float operator ()(size_t idx) const
	{
		return m[idx];
	}
	mat4x4() = default;
	mat4x4(const float newm[16])
	{
		memcpy(m, newm, 16 * sizeof(float));
	}
	mat4x4 operator*(float newm[16])
	{
		mult(newm);
		return *this;
	}

private:
	void mult(float mat[16]);
};

mat4x4 operator*(mat4x4 m1, mat4x4 m2 );
vec4 operator*(mat4x4 mat, vec4 vec );
mat4x4 worldToLocalTransform(vec3 local_x, vec3 local_y, vec3 local_z);
