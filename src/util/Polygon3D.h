#pragma once
#include "vectors.h"
#include <vector>
#include "mat4x4.h"

struct Line2D {
	vec2 start;
	vec2 end;
	vec2 dir;

	Line2D(vec2 start, vec2 end);

	// distance between this point and the axis of this line
	float distance(vec2 p);

	bool doesIntersect(const Line2D& l2);

	// call doesIntersect for line segments first, this returns the intersection point for infinite lines
	vec2 intersect(const Line2D& l2);

private:

};

// points must be at least this far inside the polygon edges
// large enough to prevent PointContents returning empty on the edges of underground polys
// smaller than the starting offset for content checks
#define INPOLY_EPSILON (0.4f)

// convex 3D polygon
class Polygon3D {
public:
	bool isValid = false;
	vec3 plane_x;
	vec3 plane_y;
	vec3 plane_z; // plane normal
	float fdist;
	
	std::vector<vec3> verts;
	std::vector<vec2> localVerts; // points relative to the plane orientation

	mat4x4 worldToLocal;
	mat4x4 localToWorld;

	// extents of local coordinates
	vec2 localMins;
	vec2 localMaxs;

	// extents of world coordinates
	vec3 worldMins;
	vec3 worldMaxs;

	Polygon3D() {}

	Polygon3D(const std::vector<vec3>& verts);

	float distance(const vec3& p);

	// returns split polys for first edge on cutPoly that contacts this polygon
	// multiple intersections (overlapping polys) are not handled
	// returns empty on no intersection
	// only applies to edges that lie flat on the plane, not ones that pierce it
	vector<vector<vec3>> split(const Polygon3D& cutPoly);

	// cut the polygon by an edge defined in this polygon's coordinate space
	// returns empty if cutting not possible
	// returns 2 new convex polygons otherwise
	vector<vector<vec3>> cut(Line2D cutLine);

	// is point inside this polygon? Coordinates are in world space.
	// Points within EPSILON of an edge are not inside.
	bool isInside(vec3 p);

	// is point inside this polygon? coordinates are in polygon's local space.
	// Points within EPSILON of an edge are not inside.
	bool isInside(vec2 p);

	// project a 3d point onto this polygon's local coordinate system
	vec2 project(vec3 p);

	// get the world position of a point in the polygon's local coordinate system
	vec3 unproject(vec2 p);

	void draw2d(vec2 pos, vec2 maxSz);
};