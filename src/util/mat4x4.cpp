#include "mat4x4.h"
#include "util.h"
#include <string>

void mat4x4::loadIdentity()
{
	memset(m, 0, sizeof(m));
	m[4*0 + 0] = 1;
	m[4*1 + 1] = 1;
	m[4*2 + 2] = 1;
	m[4*3 + 3] = 1;
}

void glhFrustumf2(float *matrix, float left, float right, float bottom, float top,
				  float znear, float zfar)
{
	float temp, temp2, temp3, temp4;
	temp = 2.0 * znear;
	temp2 = right - left;
	temp3 = top - bottom;
	temp4 = zfar - znear;
	matrix[0] = temp / temp2;
	matrix[5] = temp / temp3;
	matrix[2] = (right + left) / temp2;
	matrix[6] = (top + bottom) / temp3;
	matrix[10] = (-zfar - znear) / temp4;
	matrix[14] = -1.0;
	matrix[11] = (-temp * zfar) / temp4;
}

void mat4x4::perspective( float fov, float aspect, float near, float far )
{
	memset(m, 0, sizeof(m));
	float ymax, xmax;
	ymax = near * tanf(fov * PI / 360.0f);
	xmax = ymax * aspect;
	glhFrustumf2(m, -xmax, xmax, -ymax, ymax, near, far);
}

// Set up an orthographic projection matrix
// http://en.wikipedia.org/wiki/Orthographic_projection_(geometry)
void mat4x4::ortho(float left, float right, float bottom, float top, float near, float far)
{
	memset(m, 0, sizeof(m));
	float w = right - left;
	float h = top - bottom;
	float d = far - near; 

	// scale by the screen size
	m[4*0 + 0] = 2.0f / w;
	m[4*1 + 1] = 2.0f / h;
	m[4*2 + 2] = -2.0f / d;
	m[4*3 + 3] = 1;

	// Translate the origin to the upper-left corner of the screen
	m[4*0 + 3] = -(right + left) / w;
	m[4*1 + 3] = -(top + bottom) / h;
	m[4*2 + 3] = (far + near) / d;
}

void mat4x4::translate(float x, float y, float z)
{
	float tmat[16] =
	{
		1, 0, 0, x,
		0, 1, 0, y,
		0, 0, 1, z,
		0, 0, 0, 1,
	};
	mult(tmat);
}

void mat4x4::scale(float x, float y, float z)
{
	float tmat[16] =
	{
		x, 0, 0, 0,
		0, y, 0, 0,
		0, 0, z, 0,
		0, 0, 0, 1,
	};
	mult(tmat);
}

void mat4x4::rotateX(float r)
{
	float c = cosf(r);
	float s = sinf(r);
	float rmat[16] =
	{
		1, 0, 0, 0,
		0, c,-s, 0,
		0, s, c, 0,
		0, 0, 0, 1,
	};
	mult(rmat);
}

void mat4x4::rotateY(float r)
{
	float c = cosf(r);
	float s = sinf(r);
	float rmat[16] =
	{
		c, 0, s, 0,
		0, 1, 0, 0,
		-s, 0, c, 0,
		0, 0, 0, 1,
	};
	mult(rmat);
}

void mat4x4::rotateZ(float r)
{
	float c = cosf(r);
	float s = sinf(r);
	float rmat[16] =
	{
		c,-s, 0, 0,
		s, c, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};
	mult(rmat);
}

mat4x4 worldToLocalTransform(vec3 local_x, vec3 local_y, vec3 local_z) {
	const vec3 world_x(1, 0, 0);
	const vec3 world_y(0, 1, 0);
	const vec3 world_z(0, 0, 1);

	mat4x4 worldToLocal;

	worldToLocal.loadIdentity();
	worldToLocal.m[0 * 4 + 0] = dotProduct(local_x, world_x);
	worldToLocal.m[1 * 4 + 0] = dotProduct(local_x, world_y);
	worldToLocal.m[2 * 4 + 0] = dotProduct(local_x, world_z);
	worldToLocal.m[0 * 4 + 1] = dotProduct(local_y, world_x);
	worldToLocal.m[1 * 4 + 1] = dotProduct(local_y, world_y);
	worldToLocal.m[2 * 4 + 1] = dotProduct(local_y, world_z);
	worldToLocal.m[0 * 4 + 2] = dotProduct(local_z, world_x);
	worldToLocal.m[1 * 4 + 2] = dotProduct(local_z, world_y);
	worldToLocal.m[2 * 4 + 2] = dotProduct(local_z, world_z);

	return worldToLocal;
}

mat4x4 mat4x4::transpose()
{
	mat4x4 result;
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			result.m[y + x*4] = m[y*4 + x];
	return result;
}

// http://stackoverflow.com/questions/1148309/inverting-a-4x4-matrix
mat4x4 mat4x4::invert()
{
	mat4x4 out = mat4x4();
	float inv[16];

	inv[0] = m[5]  * m[10] * m[15] - 
		m[5]  * m[11] * m[14] - 
		m[9]  * m[6]  * m[15] + 
		m[9]  * m[7]  * m[14] +
		m[13] * m[6]  * m[11] - 
		m[13] * m[7]  * m[10];

	inv[4] = -m[4]  * m[10] * m[15] + 
		m[4]  * m[11] * m[14] + 
		m[8]  * m[6]  * m[15] - 
		m[8]  * m[7]  * m[14] - 
		m[12] * m[6]  * m[11] + 
		m[12] * m[7]  * m[10];

	inv[8] = m[4]  * m[9] * m[15] - 
		m[4]  * m[11] * m[13] - 
		m[8]  * m[5] * m[15] + 
		m[8]  * m[7] * m[13] + 
		m[12] * m[5] * m[11] - 
		m[12] * m[7] * m[9];

	inv[12] = -m[4]  * m[9] * m[14] + 
		m[4]  * m[10] * m[13] +
		m[8]  * m[5] * m[14] - 
		m[8]  * m[6] * m[13] - 
		m[12] * m[5] * m[10] + 
		m[12] * m[6] * m[9];

	inv[1] = -m[1]  * m[10] * m[15] + 
		m[1]  * m[11] * m[14] + 
		m[9]  * m[2] * m[15] - 
		m[9]  * m[3] * m[14] - 
		m[13] * m[2] * m[11] + 
		m[13] * m[3] * m[10];

	inv[5] = m[0]  * m[10] * m[15] - 
		m[0]  * m[11] * m[14] - 
		m[8]  * m[2] * m[15] + 
		m[8]  * m[3] * m[14] + 
		m[12] * m[2] * m[11] - 
		m[12] * m[3] * m[10];

	inv[9] = -m[0]  * m[9] * m[15] + 
		m[0]  * m[11] * m[13] + 
		m[8]  * m[1] * m[15] - 
		m[8]  * m[3] * m[13] - 
		m[12] * m[1] * m[11] + 
		m[12] * m[3] * m[9];

	inv[13] = m[0]  * m[9] * m[14] - 
		m[0]  * m[10] * m[13] - 
		m[8]  * m[1] * m[14] + 
		m[8]  * m[2] * m[13] + 
		m[12] * m[1] * m[10] - 
		m[12] * m[2] * m[9];

	inv[2] = m[1]  * m[6] * m[15] - 
		m[1]  * m[7] * m[14] - 
		m[5]  * m[2] * m[15] + 
		m[5]  * m[3] * m[14] + 
		m[13] * m[2] * m[7] - 
		m[13] * m[3] * m[6];

	inv[6] = -m[0]  * m[6] * m[15] + 
		m[0]  * m[7] * m[14] + 
		m[4]  * m[2] * m[15] - 
		m[4]  * m[3] * m[14] - 
		m[12] * m[2] * m[7] + 
		m[12] * m[3] * m[6];

	inv[10] = m[0]  * m[5] * m[15] - 
		m[0]  * m[7] * m[13] - 
		m[4]  * m[1] * m[15] + 
		m[4]  * m[3] * m[13] + 
		m[12] * m[1] * m[7] - 
		m[12] * m[3] * m[5];

	inv[14] = -m[0]  * m[5] * m[14] + 
		m[0]  * m[6] * m[13] + 
		m[4]  * m[1] * m[14] - 
		m[4]  * m[2] * m[13] - 
		m[12] * m[1] * m[6] + 
		m[12] * m[2] * m[5];

	inv[3] = -m[1] * m[6] * m[11] + 
		m[1] * m[7] * m[10] + 
		m[5] * m[2] * m[11] - 
		m[5] * m[3] * m[10] - 
		m[9] * m[2] * m[7] + 
		m[9] * m[3] * m[6];

	inv[7] = m[0] * m[6] * m[11] - 
		m[0] * m[7] * m[10] - 
		m[4] * m[2] * m[11] + 
		m[4] * m[3] * m[10] + 
		m[8] * m[2] * m[7] - 
		m[8] * m[3] * m[6];

	inv[11] = -m[0] * m[5] * m[11] + 
		m[0] * m[7] * m[9] + 
		m[4] * m[1] * m[11] - 
		m[4] * m[3] * m[9] - 
		m[8] * m[1] * m[7] + 
		m[8] * m[3] * m[5];

	inv[15] = m[0] * m[5] * m[10] - 
		m[0] * m[6] * m[9] - 
		m[4] * m[1] * m[10] + 
		m[4] * m[2] * m[9] + 
		m[8] * m[1] * m[6] - 
		m[8] * m[2] * m[5];

	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

	if (det == 0)
	{
		logf("Matrix inversion failed (determinant is zero)\n");
		return out;
	}

	det = 1.0 / det;
	
	for (int i = 0; i < 16; i++)
		out.m[i] = inv[i] * det;

	return out;
}

void mat4x4::mult( float mat[16] )
{
	mat4x4& other = *((mat4x4*)mat);
	*this = *this * other;
}

mat4x4 operator*( mat4x4 m1, mat4x4 m2 )
{
	mat4x4 result;
	memset(result.m, 0, sizeof(result.m));

	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++)
				result.m[i*4 + j] += m1.m[i*4 + k] * m2.m[k*4 + j];

	return result;
}

vec4 operator*( mat4x4 mat, vec4 vec )
{
	vec4 res;
	res.x = mat.m[0]*vec.x + mat.m[4]*vec.y + mat.m[8]*vec.z  + mat.m[12]*vec.w;
	res.y = mat.m[1]*vec.x + mat.m[5]*vec.y + mat.m[9]*vec.z  + mat.m[13]*vec.w;
	res.z = mat.m[2]*vec.x + mat.m[6]*vec.y + mat.m[10]*vec.z + mat.m[14]*vec.w;
	res.w = vec.w;
	return res;
}
