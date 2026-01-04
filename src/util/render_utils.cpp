#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "render_utils.h"
#include "Bsp.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "globals.h"
#include "util.h"
#include "ShaderProgram.h"

void drawLine(vec3 start, vec3 end, COLOR4 color) {
	cVert verts[2];

	verts[0].x = start.x;
	verts[0].y = start.z;
	verts[0].z = -start.y;
	verts[0].c = color;

	verts[1].x = end.x;
	verts[1].y = end.z;
	verts[1].z = -end.y;
	verts[1].c = color;

	VertexBuffer buffer(g_shaders.color, &verts[0], 2);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_LINES);
}

void drawArrow(vec3 start, vec3 end, COLOR4 color) {
	struct cArrow {
		cCube shaft; // minor todo: one face can be omitted. make a new struct
		cPyramid tip;
	};
	int arrowVerts = 6 * 6 + (6 + 3 * 4);

	vec3 angles = VecToAngles((end - start).normalize());
	mat4x4 rotMat;
	rotMat.loadIdentity();
	rotMat.rotateZ(-angles.x * (PI / 180.0f));
	rotMat.rotateY(-angles.y * (PI / 180.0f));

	float len = (end - start).length();
	cArrow arrow;
	arrow.shaft = cCube(vec3(-1, -1, -1), vec3(len - 16, 1, 1), color);
	arrow.tip = cPyramid(vec3(len - 16, 0, 0), 4, 16, color);

	cVert* rawVerts = (cVert*)&arrow;
	for (int k = 0; k < arrowVerts; k++) {
		vec3* pos = (vec3*)&rawVerts[k].x;
		*pos = (rotMat * vec4(*pos, 1)).xyz() + start.flip();
	}

	VertexBuffer buffer(g_shaders.color, &arrow, arrowVerts);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_TRIANGLES);
}

void drawLine2D(vec2 start, vec2 end, COLOR4 color) {
	cVert verts[2];

	verts[0].x = start.x;
	verts[0].y = start.y;
	verts[0].z = 0;
	verts[0].c = color;

	verts[1].x = end.x;
	verts[1].y = end.y;
	verts[1].z = 0;
	verts[1].c = color;

	VertexBuffer buffer(g_shaders.color, &verts[0], 2);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_LINES);
}

void drawBox(vec3 center, float width, COLOR4 color) {
	width *= 0.5f;
	vec3 sz = vec3(width, width, width);
	vec3 pos = vec3(center.x, center.z, -center.y);
	cCube cube(pos - sz, pos + sz, color);

	VertexBuffer buffer(g_shaders.color, &cube, 6 * 6);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_TRIANGLES);
}

void drawBoxOutline(vec3 center, float width, COLOR4 color) {
	width *= 0.5f;
	vec3 sz = vec3(width, width, width);
	vec3 pos = vec3(center.x, center.z, -center.y);
	vec3 mins = pos - sz;
	vec3 maxs = pos + sz;

	vec3 corners[8] = {
		vec3(mins.x, mins.y, mins.z), // 0
		vec3(maxs.x, mins.y, mins.z), // 1
		vec3(mins.x, maxs.y, mins.z), // 2
		vec3(maxs.x, maxs.y, mins.z), // 3
		vec3(mins.x, mins.y, maxs.z), // 4
		vec3(maxs.x, mins.y, maxs.z), // 5
		vec3(mins.x, maxs.y, maxs.z), // 6
		vec3(maxs.x, maxs.y, maxs.z),  // 7
	};

	cVert edges[24] = {
		cVert(corners[0], color), cVert(corners[1], color),
		cVert(corners[1], color), cVert(corners[3], color),
		cVert(corners[3], color), cVert(corners[2], color),
		cVert(corners[2], color), cVert(corners[0], color),
		cVert(corners[4], color), cVert(corners[5], color),
		cVert(corners[5], color), cVert(corners[7], color),
		cVert(corners[7], color), cVert(corners[6], color),
		cVert(corners[6], color), cVert(corners[4], color),
		cVert(corners[0], color), cVert(corners[4], color),
		cVert(corners[1], color), cVert(corners[5], color),
		cVert(corners[2], color), cVert(corners[6], color),
		cVert(corners[3], color), cVert(corners[7], color),
	};

	VertexBuffer buffer(g_shaders.color, &edges, 24);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_LINES);
}

void drawBox(vec3 mins, vec3 maxs, COLOR4 color) {
	mins = vec3(mins.x, mins.z, -mins.y);
	maxs = vec3(maxs.x, maxs.z, -maxs.y);

	cCube cube(mins, maxs, color);

	VertexBuffer buffer(g_shaders.color, &cube, 6 * 6);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_TRIANGLES);
}

void drawPolygon3D(Polygon3D& poly, COLOR4 color) {
	g_shaders.color->bind();
	g_shaders.color->modelMat->loadIdentity();
	g_shaders.color->updateMatrixes();
	glDisable(GL_CULL_FACE);

	static cVert verts[64];

	for (int i = 0; i < poly.verts.size() && i < 64; i++) {
		vec3 pos = poly.verts[i];
		verts[i].x = pos.x;
		verts[i].y = pos.z;
		verts[i].z = -pos.y;
		verts[i].c = color;
	}

	VertexBuffer buffer(g_shaders.color, verts, poly.verts.size());
	buffer.upload();
	buffer.draw(g_shaders.color, GL_TRIANGLE_FAN);
}

void drawPolygon2D(vector<vec2>& poly, vec2 pos, float scale, COLOR4 color) {
	for (int i = 0; i < poly.size(); i++) {
		vec2 v1 = poly[i];
		vec2 v2 = poly[(i + 1) % poly.size()];
		drawLine2D(pos + v1 * scale, pos + v2 * scale, color);
		if (i == 0) {
			drawLine2D(pos + v1 * scale, pos + (v1 + (v2 - v1) * 0.5f) * scale, COLOR4(0, 255, 0, 255));
		}
	}
}

void drawBox2D(vec2 center, float width, COLOR4 color) {
	vec2 pos = vec2(center.x, center.y) - vec2(width * 0.5f, width * 0.5f);
	cQuad cube(pos.x, pos.y, width, width, color);

	VertexBuffer buffer(g_shaders.color, &cube, 6);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_TRIANGLES);
}

void drawRect2D(vec2 pos, vec2 size, COLOR4 color) {
	cQuad cube(pos.x, pos.y, size.x, size.y, color);
	VertexBuffer buffer(g_shaders.color, &cube, 6);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_TRIANGLES);
}

void drawPlane(BSPPLANE& plane, COLOR4 color, float sz) {

	vec3 ori = plane.vNormal * plane.fDist;
	vec3 crossDir = fabs(plane.vNormal.z) > 0.9f ? vec3(1, 0, 0) : vec3(0, 0, 1);
	vec3 right = crossProduct(plane.vNormal, crossDir);
	vec3 up = crossProduct(right, plane.vNormal);

	vec3 topLeft = vec3(ori + right * -sz + up * sz).flip();
	vec3 topRight = vec3(ori + right * sz + up * sz).flip();
	vec3 bottomLeft = vec3(ori + right * -sz + up * -sz).flip();
	vec3 bottomRight = vec3(ori + right * sz + up * -sz).flip();

	cVert topLeftVert(topLeft, color);
	cVert topRightVert(topRight, color);
	cVert bottomLeftVert(bottomLeft, color);
	cVert bottomRightVert(bottomRight, color);
	cQuad quad(bottomRightVert, bottomLeftVert, topLeftVert, topRightVert);

	VertexBuffer buffer(g_shaders.color, &quad, 6);
	buffer.upload();
	buffer.draw(g_shaders.color, GL_TRIANGLES);
}

void drawClipnodes(Bsp* map, int iNode, int& currentPlane, int activePlane) {
	if (iNode == -1)
		return;
	BSPCLIPNODE& node = map->clipnodes[iNode];

	if (currentPlane == activePlane)
		drawPlane(map->planes[node.iPlane], { 255, 255, 255, 255 });
	currentPlane++;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			drawClipnodes(map, node.iChildren[i], currentPlane, activePlane);
		}
	}
}

void drawNodes(Bsp* map, int iNode, int& currentPlane, int activePlane) {
	if (iNode == -1)
		return;
	BSPNODE& node = map->nodes[iNode];

	if (currentPlane == activePlane)
		drawPlane(map->planes[node.iPlane], { 255, 128, 128, 255 });
	currentPlane++;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			drawNodes(map, node.iChildren[i], currentPlane, activePlane);
		}
	}
}

