#pragma once
#include "types.h"
#include <string>

struct vec3
{
	float x, y, z;
	vec3() : x(), y(), z() {}
	vec3( float x, float y, float z ) : x( x ), y( y ), z( z ) {}
	vec3 normalize(float length=1.0f);
	float length();
	vec3 invert();
	std::string toKeyvalueString(bool truncate = false);
	vec3 flip(); // flip from opengl to Half-life coordinate system and vice versa

	void operator-=(vec3 v);
	void operator+=(vec3 v);
	void operator*=(vec3 v);
	void operator/=(vec3 v);

	void operator-=(float f);
	void operator+=(float f);
	void operator*=(float f);
	void operator/=(float f);
};

vec3 operator-(vec3 v1, vec3 v2);
vec3 operator+(vec3 v1, vec3 v2);
vec3 operator*(vec3 v1, vec3 v2);
vec3 operator/(vec3 v1, vec3 v2);

vec3 operator+(vec3 v, float f);
vec3 operator-(vec3 v, float f);
vec3 operator*(vec3 v, float f);
vec3 operator/(vec3 v, float f);

vec3 crossProduct(vec3 v1, vec3 v2);
float dotProduct(vec3 v1, vec3 v2);
void makeVectors(vec3 angles, vec3& forward, vec3& right, vec3& up);

bool operator==(vec3 v1, vec3 v2);
bool operator!=(vec3 v1, vec3 v2);

struct vec2
{
	float x, y;
	vec2() : x(), y() {}
	vec2(float x, float y) : x(x), y(y) {}
	float length();

	void operator-=(vec2 v);
	void operator+=(vec2 v);
	void operator*=(vec2 v);
	void operator/=(vec2 v);

	void operator-=(float f);
	void operator+=(float f);
	void operator*=(float f);
	void operator/=(float f);
};

vec2 operator-(vec2 v1, vec2 v2);
vec2 operator+(vec2 v1, vec2 v2);
vec2 operator*(vec2 v1, vec2 v2);
vec2 operator/(vec2 v1, vec2 v2);

vec2 operator+(vec2 v, float f);
vec2 operator-(vec2 v, float f);
vec2 operator*(vec2 v, float f);
vec2 operator/(vec2 v, float f);

bool operator==(vec2 v1, vec2 v2);
bool operator!=(vec2 v1, vec2 v2);

struct vec4
{
	float x, y, z, w;

	vec4() : x(0), y(0), z(0), w(0) {}
	vec4(float x, float y, float z) : x(x), y(y), z(z), w(1) {}
	vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
	vec4(vec3 v, float a) : x(v.x), y(v.y), z(v.z), w(a) {}
	vec3 xyz();
	vec2 xy();
};

vec4 operator-(vec4 v1, vec4 v2);
vec4 operator+(vec4 v1, vec4 v2);
vec4 operator*(vec4 v1, vec4 v2);
vec4 operator/(vec4 v1, vec4 v2);

vec4 operator+(vec4 v, float f);
vec4 operator-(vec4 v, float f);
vec4 operator*(vec4 v, float f);
vec4 operator/(vec4 v, float f);

bool operator==(vec4 v1, vec4 v2);
bool operator!=(vec4 v1, vec4 v2);