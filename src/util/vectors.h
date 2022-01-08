#pragma once

#include <string>

struct vec3
{
	float x, y, z;
	vec3() : x(), y(), z() {}
	vec3(float x, float y, float z) : x(x), y(y), z(z) {}
	vec3 normalize(float length = 1.0f);
	float length();
	vec3 invert();
	std::string toKeyvalueString(bool truncate = false, const std::string& suffix_x = " ", const std::string& suffix_y = " ", const std::string& suffix_z = "");
	vec3 flip(); // flip from opengl to Half-life coordinate system and vice versa

	void operator-=(const vec3& v);
	void operator+=(const vec3& v);
	void operator*=(const vec3& v);
	void operator/=(const vec3& v);

	void operator-=(float f);
	void operator+=(float f);
	void operator*=(float f);
	void operator/=(float f);

	vec3 operator-() {
		x *= -1.f;
		y *= -1.f;
		z *= -1.f;
		return *this;
	}

	float operator [] (int i) const {
		switch (i)
		{
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		}
		return 0.0f;
	}

};

vec3 operator-(vec3 v1, const vec3& v2);
vec3 operator+(vec3 v1, const vec3& v2);
vec3 operator*(vec3 v1, const vec3& v2);
vec3 operator/(vec3 v1, const vec3& v2);

vec3 operator+(vec3 v, float f);
vec3 operator-(vec3 v, float f);
vec3 operator*(vec3 v, float f);
vec3 operator/(vec3 v, float f);

vec3 crossProduct(const vec3& v1, const vec3& v2);
float dotProduct(const vec3& v1, const vec3& v2);
void makeVectors(const vec3& angles, vec3& forward, vec3& right, vec3& up);

bool operator==(const vec3& v1, const vec3& v2);
bool operator!=(const vec3& v1, const vec3& v2);

struct vec2
{
	float x, y;
	vec2() : x(), y() {}
	vec2(float x, float y) : x(x), y(y) {}
	vec2 normalize(float length = 1.0f);
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

vec4 operator-(vec4 v1, const vec4& v2);
vec4 operator+(vec4 v1, const vec4& v2);
vec4 operator*(vec4 v1, const vec4& v2);
vec4 operator/(vec4 v1, const vec4& v2);

vec4 operator+(vec4 v, float f);
vec4 operator-(vec4 v, float f);
vec4 operator*(vec4 v, float f);
vec4 operator/(vec4 v, float f);

bool operator==(const vec4& v1, const vec4& v2);
bool operator!=(const vec4& v1, const vec4& v2);