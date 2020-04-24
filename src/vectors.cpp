#include "vectors.h"
#include <cmath>

#define EPSILON 0.001f

bool operator==( vec3 v1, vec3 v2 )
{
	vec3 v = v1 - v2;
	if (fabs(v.x) >= EPSILON)
		return false;
	if (fabs(v.y) >= EPSILON)
		return false;
	if (fabs(v.z) >= EPSILON)
		return false;
	return true;
}

bool operator!=( vec3 v1, vec3 v2 )
{
	return v1.x != v2.x || v1.y != v2.y || v1.z != v2.z;
}

vec3 operator-( vec3 v1, vec3 v2 )
{
	v1.x -= v2.x;
	v1.y -= v2.y;
	v1.z -= v2.z;
	return v1;
}

vec3 operator+( vec3 v1, vec3 v2 )
{
	v1.x += v2.x;
	v1.y += v2.y;
	v1.z += v2.z;
	return v1;
}

vec3 operator*( vec3 v1, vec3 v2 )
{
	v1.x *= v2.x;
	v1.y *= v2.y;
	v1.z *= v2.z;
	return v1;
}

vec3 operator/( vec3 v1, vec3 v2 )
{
	v1.x /= v2.x;
	v1.y /= v2.y;
	v1.z /= v2.z;
	return v1;
}

vec3 operator-( vec3 v, float f )
{
	v.x -= f;
	v.y -= f;
	v.z -= f;
	return v;
}

vec3 operator+( vec3 v, float f )
{
	v.x += f;
	v.y += f;
	v.z += f;
	return v;
}

vec3 operator*( vec3 v, float f )
{
	v.x *= f;
	v.y *= f;
	v.z *= f;
	return v;
}

vec3 operator/( vec3 v, float f )
{
	v.x /= f;
	v.y /= f;
	v.z /= f;
	return v;
}

void vec3::operator-=( vec3 v )
{
	x -= v.x;
	y -= v.y;
	z -= v.z;
}

void vec3::operator+=( vec3 v )
{
	x += v.x;
	y += v.y;
	z += v.z;
}

void vec3::operator*=( vec3 v )
{
	x *= v.x;
	y *= v.y;
	z *= v.z;
}

void vec3::operator/=( vec3 v )
{
	x /= v.x;
	y /= v.y;
	z /= v.z;
}

void vec3::operator-=( float f )
{
	x -= f;
	y -= f;
	z -= f;
}

void vec3::operator+=( float f )
{
	x += f;
	y += f;
	z += f;
}

void vec3::operator*=( float f )
{
	x *= f;
	y *= f;
	z *= f;
}

void vec3::operator/=( float f )
{
	x /= f;
	y /= f;
	z /= f;
}

vec3 crossProduct( vec3 v1, vec3 v2 )
{
	float x = v1.y*v2.z - v2.y*v1.z; 
	float y = v2.x*v1.z - v1.x*v2.z; 
	float z = v1.x*v2.y - v1.y*v2.x;
	return vec3(x, y, z);
}

float dotProduct( vec3 v1, vec3 v2 )
{
	return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;
}

vec3 vec3::normalize( float length )
{
	if (x == 0 && y == 0 && z == 0)
		return vec3(0, 0, 0);
	float d = length / sqrt( (x*x) + (y*y) + (z*z) );
	return vec3(x*d, y*d, z*d);
}

float vec3::length()
{
	return sqrt( (x*x) + (y*y) + (z*z) );
}




bool operator==(vec2 v1, vec2 v2)
{
	vec2 v = v1 - v2;
	if (fabs(v.x) >= EPSILON)
		return false;
	if (fabs(v.y) >= EPSILON)
		return false;
	return true;
}

bool operator!=(vec2 v1, vec2 v2)
{
	return v1.x != v2.x || v1.y != v2.y;
}

vec2 operator-(vec2 v1, vec2 v2)
{
	v1.x -= v2.x;
	v1.y -= v2.y;
	return v1;
}

vec2 operator+(vec2 v1, vec2 v2)
{
	v1.x += v2.x;
	v1.y += v2.y;
	return v1;
}

vec2 operator*(vec2 v1, vec2 v2)
{
	v1.x *= v2.x;
	v1.y *= v2.y;
	return v1;
}

vec2 operator/(vec2 v1, vec2 v2)
{
	v1.x /= v2.x;
	v1.y /= v2.y;
	return v1;
}

vec2 operator-(vec2 v, float f)
{
	v.x -= f;
	v.y -= f;
	return v;
}

vec2 operator+(vec2 v, float f)
{
	v.x += f;
	v.y += f;
	return v;
}

vec2 operator*(vec2 v, float f)
{
	v.x *= f;
	v.y *= f;
	return v;
}

vec2 operator/(vec2 v, float f)
{
	v.x /= f;
	v.y /= f;
	return v;
}

void vec2::operator-=(vec2 v)
{
	x -= v.x;
	y -= v.y;
}

void vec2::operator+=(vec2 v)
{
	x += v.x;
	y += v.y;
}

void vec2::operator*=(vec2 v)
{
	x *= v.x;
	y *= v.y;
}

void vec2::operator/=(vec2 v)
{
	x /= v.x;
	y /= v.y;
}

void vec2::operator-=(float f)
{
	x -= f;
	y -= f;
}

void vec2::operator+=(float f)
{
	x += f;
	y += f;
}

void vec2::operator*=(float f)
{
	x *= f;
	y *= f;
}

void vec2::operator/=(float f)
{
	x /= f;
	y /= f;
}
