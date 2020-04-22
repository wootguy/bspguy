#pragma once
#include "types.h"

struct vec3
{
	float x, y, z;
	vec3() : x(), y(), z() {}
	vec3( float x, float y, float z ) : x( x ), y( y ), z( z ) {}
	vec3 normalize(float length=1.0f);
	float length();

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