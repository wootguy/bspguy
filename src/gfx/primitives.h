#pragma once
#include <vector>
#include "vectors.h"
#include "Wad.h"
#include "bsplimits.h"

#pragma pack(push, 1)
struct tVert
{
	float u, v;
	float x, y, z;

	tVert() {}
	tVert(float x, float y, float z, float u, float v) : u(u), v(v), x(x), y(y), z(z) {}
	tVert(vec3 p, float u, float v) : u(u), v(v), x(p.x), y(p.y), z(p.z) {}
	tVert(vec3 p, vec2 uv) : u(uv.x), v(uv.y), x(p.x), y(p.y), z(p.z) {}

	vec3 pos();
};

struct lightmapVert
{
	// texture coordinates
	float u, v;

	// lightmap texture coordinates
	float luv[MAXLIGHTMAPS][2];

	float x, y, z;
};

struct cVert
{
	COLOR3 c;
	float x, y, z;

	cVert() {}
	cVert(float x, float y, float z, COLOR3 c) : c(c), x(x), y(y), z(z) {}
	cVert(vec3 p, COLOR3 c) : c(c), x(p.x), y(p.y), z(p.z) {}
};

struct tTri
{
	tVert v1, v2, v3;

	tTri() {}
	tTri(tVert v1, tVert v2, tVert v3) : v1(v1), v2(v2), v3(v3) {}
};

struct cTri
{
	cVert v1, v2, v3;

	cTri() {}
	cTri(cVert v1, cVert v2, cVert v3) : v1(v1), v2(v2), v3(v3) {}
};

// Textured 3D Quad
struct tQuad
{
	tVert v1, v2, v3;
	tVert v4, v5, v6;

	tQuad() {}
	tQuad(float x, float y, float w, float h);
	tQuad(float x, float y, float w, float h, float uu1, float vv1, float uu2, float vv2);
	tQuad(tVert v1, tVert v2, tVert v3, tVert v4);
};

// Colored 3D Quad
struct cQuad
{
	cVert v1, v2, v3;
	cVert v4, v5, v6;

	cQuad() {}
	cQuad(cVert v1, cVert v2, cVert v3, cVert v4);

	void setColor(COLOR3 c); // color for the entire quad
	void setColor(COLOR3 c1, COLOR3 c2, COLOR3 c3, COLOR3 c4); // color each vertex in CCW order
};

// Textured 3D Cube
struct tCube
{
	tQuad left, right;
	tQuad top, bottom;
	tQuad front, back;

	tCube(vec3 mins, vec3 maxs);
};

// Colored 3D Cube
struct cCube
{
	cQuad left, right;
	cQuad top, bottom;
	cQuad front, back;

	cCube(vec3 mins, vec3 maxs, COLOR3 c);

	void setColor(COLOR3 c); // set color for the entire cube
	void setColor(COLOR3 left, COLOR3 right, COLOR3 top, COLOR3 bottom, COLOR3 front, COLOR3 back);
};

#pragma pack(pop)