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

vec3 vec3::invert() {
	return vec3(x != 0 ? -x : x, y != 0 ? -y : y, z != 0 ? -z : z);
}

float vec3::length()
{
	return sqrt( (x*x) + (y*y) + (z*z) );
}

string vec3::toKeyvalueString() {
	string parts[3] = { to_string(x) , to_string(y), to_string(z) };

	// remove trailing zeros to save some space
	for (int i = 0; i < 3; i++) {
		parts[i].erase(parts[i].find_last_not_of('0') + 1, std::string::npos);

		// strip dot if there's no fractional part
		if (parts[i][parts[i].size() - 1] == '.') {
			parts[i] = parts[i].substr(0, parts[i].size() - 1);
		}
	}

	return parts[0] + " " + parts[1] + " " + parts[2];
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
