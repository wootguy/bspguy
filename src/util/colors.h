#pragma once
#include <stdint.h>
#include "vectors.h"

#pragma pack(push, 1)
struct COLOR3
{
	uint8_t r, g, b;

	COLOR3() : r(0), g(0), b(0) {}
	COLOR3(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
	vec3 toVec() { return vec3(r / 255.0f, g / 255.0f, b / 255.0f); }
};
#pragma pack(pop)
struct COLOR4
{
	uint8_t r, g, b, a;

	COLOR4() : r(0), g(0), b(0), a(0) {}
	COLOR4(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r(r), g(g), b(b), a(a) {}
	COLOR4(COLOR3 c, uint8_t a) : r(c.r), g(c.g), b(c.b), a(a) {}
	vec4 toVec() { return vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }
	COLOR3 rgb() { return COLOR3(r, g, b); }
};

COLOR3 operator*(COLOR3 v, float f);
bool operator==(COLOR3 c1, COLOR3 c2);
bool operator!=(COLOR3 c1, COLOR3 c2);

COLOR4 operator*(COLOR4 v, float f);
bool operator==(COLOR4 c1, COLOR4 c2);
bool operator!=(COLOR4 c1, COLOR4 c2);

// for using COLOR3 with sets/maps
namespace std {
	template<>
	struct hash<COLOR3> {
		std::size_t operator()(const COLOR3& c) const {
			return (static_cast<size_t>(c.r) << 16) |
				(static_cast<size_t>(c.g) << 8) |
				static_cast<size_t>(c.b);
		}
	};
}