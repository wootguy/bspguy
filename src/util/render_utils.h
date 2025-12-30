#pragma once
#include "vectors.h"
#include "colors.h"
#include "Polygon3D.h"
#include "bsptypes.h"

class Bsp;

void drawLine(vec3 start, vec3 end, COLOR4 color);

void drawArrow(vec3 start, vec3 end, COLOR4 color);

void drawLine2D(vec2 start, vec2 end, COLOR4 color);

void drawBox(vec3 center, float width, COLOR4 color);

void drawBoxOutline(vec3 center, float width, COLOR4 color);

void drawBox(vec3 mins, vec3 maxs, COLOR4 color);

void drawPolygon3D(Polygon3D& poly, COLOR4 color);

void drawPolygon2D(std::vector<vec2>& poly, vec2 pos, float scale, COLOR4 color);

void drawBox2D(vec2 center, float width, COLOR4 color);

void drawRect2D(vec2 pos, vec2 size, COLOR4 color);

void drawPlane(BSPPLANE& plane, COLOR4 color, float sz=32768);

void drawClipnodes(Bsp* map, int iNode, int& currentPlane, int activePlane);

void drawNodes(Bsp* map, int iNode, int& currentPlane, int activePlane);
