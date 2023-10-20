#include "Polygon3D.h"
#include "util.h"
#include "BspRenderer.h"
#include "Renderer.h"

Line2D::Line2D(vec2 start, vec2 end) {
	this->start = start;
	this->end = end;
	dir = (end - start).normalize();
}

float Line2D::distance(vec2 p) {
	return crossProduct(dir, start - p);
}

bool onSegment(const vec2& p, const vec2& q, const vec2& r) {
	return (q.x <= max(p.x, r.x) && q.x >= min(p.x, r.x) &&
			q.y <= max(p.y, r.y) && q.y >= min(p.y, r.y));
}

int orientation(const vec2& p, const vec2& q, const vec2& r) {
	float val = (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);

	if (val == 0.0) return 0;  // Collinear
	return (val > 0.0) ? 1 : 2;  // Clockwise or counterclockwise
}

bool Line2D::doesIntersect(const Line2D& l2) {
	const vec2& A = start;
	const vec2& B = end;
	const vec2& C = l2.start - l2.dir * EPSILON; // extend a bit in case point is on edge
	const vec2& D = l2.end + l2.dir * EPSILON;

	int o1 = orientation(A, B, C);
	int o2 = orientation(A, B, D);
	int o3 = orientation(C, D, A);
	int o4 = orientation(C, D, B);

	if (o1 != o2 && o3 != o4)
		return true;  // They intersect

	if (o1 == 0 && onSegment(A, C, B)) return true;
	if (o2 == 0 && onSegment(A, D, B)) return true;
	if (o3 == 0 && onSegment(C, A, D)) return true;
	if (o4 == 0 && onSegment(C, B, D)) return true;

	return false;  // Doesn't intersect
}

vec2 Line2D::intersect(const Line2D& l2) {
	const vec2& A = start;
	const vec2& B = end;
	const vec2& C = l2.start;
	const vec2& D = l2.end;

	float a1 = B.y - A.y;
	float b1 = A.x - B.x;
	float c1 = a1 * A.x + b1 * A.y;

	float a2 = D.y - C.y;
	float b2 = C.x - D.x;
	float c2 = a2 * C.x + b2 * C.y;

	float determinant = a1 * b2 - a2 * b1;

	if (determinant == 0.0) {
		logf("Line2D intersect:determinant is 0\n");
		return vec2();
	}
	else {
		float x = (b2 * c1 - b1 * c2) / determinant;
		float y = (a1 * c2 - a2 * c1) / determinant;
		return vec2(x, y);
	}
}

Polygon3D::Polygon3D(const vector<vec3>& verts) {
	this->verts = verts;

	vector<vec3> triangularVerts = getTriangularVerts(this->verts);

	if (triangularVerts.empty())
		return;

	vec3 e1 = (triangularVerts[1] - triangularVerts[0]).normalize();
	vec3 e2 = (triangularVerts[2] - triangularVerts[0]).normalize();

	plane_z = crossProduct(e1, e2).normalize();
	plane_x = e1;
	plane_y = crossProduct(plane_z, plane_x).normalize();
	fdist = dotProduct(triangularVerts[0], plane_z);

	worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);
	localToWorld = worldToLocal.invert();

	localMins = vec2(FLT_MAX, FLT_MAX);
	localMaxs = vec2(-FLT_MAX, -FLT_MAX);

	worldMins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	worldMaxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int e = 0; e < verts.size(); e++) {
		vec2 localPoint = project(verts[e]);
		localVerts.push_back(localPoint);
		expandBoundingBox(localPoint, localMins, localMaxs);
		expandBoundingBox(verts[e], worldMins, worldMaxs);
	}

	vec3 vep(EPSILON, EPSILON, EPSILON);
	worldMins -= vep;
	worldMaxs += vep;

	isValid = true;
}

vec2 Polygon3D::project(vec3 p) {
	return (worldToLocal * vec4(p, 1)).xy();
}

vec3 Polygon3D::unproject(vec2 p) {
	return (localToWorld * vec4(p.x, p.y, fdist, 1)).xyz();
}

float Polygon3D::distance(const vec3& p) {
	return dotProduct(p - verts[0], plane_z);
}

bool Polygon3D::isInside(vec3 p) {
	if (fabs(distance(p)) > EPSILON) {
		return false;
	}

	return isInside(project(p));
}

float isLeft(const vec2& p1, const vec2& p2, const vec2& point) {
	return crossProduct(p2 - p1, point - p1);
}

// winding method
bool Polygon3D::isInside(vec2 p) {
	int windingNumber = 0;

	for (int i = 0; i < localVerts.size(); i++) {
		const vec2& p1 = localVerts[i];
		const vec2& p2 = localVerts[(i + 1) % localVerts.size()];
		vec2 dir = (p2 - p1).normalize();

		if (p1.y <= p.y) {
			if (p2.y > p.y && isLeft(p1, p2, p) > 0) {
				windingNumber += 1;
			}
		}
		else if (p2.y <= p.y && isLeft(p1, p2, p) < 0) {
			windingNumber -= 1;
		}

		float dist = crossProduct(dir, p1 - p);
		if (fabs(dist) < INPOLY_EPSILON) {
			return false; // point is too close to an edge
		}
	}

	return windingNumber != 0;
}

vector<vector<vec3>> Polygon3D::cut(Line2D cutLine) {
	vector<vector<vec3>> splitPolys;

	int vertIntersections = 0;
	for (int i = 0; i < localVerts.size(); i++) {
		float dist = fabs(cutLine.distance(localVerts[i]));
		if (dist < EPSILON) {
			vertIntersections++;
			if (vertIntersections > 1) {
				//logf("cut is colinear with an edge\n");
				return splitPolys; // line is colinear with an edge, no intersections possible
			}
		}
	}

	bool intersectsAnyEdge = false;
	for (int i = 0; i < localVerts.size(); i++) {
		vec2 e1 = localVerts[i];
		vec2 e2 = localVerts[(i + 1) % localVerts.size()];
		Line2D edge(e1, e2);

		if (edge.doesIntersect(cutLine)) {
			intersectsAnyEdge = true;
			break;
		}
	}
	if (!intersectsAnyEdge) {
		//logf("No edge intersections\n");
		return splitPolys;
	}

	// extend to "infinity" now that we know the cutting edge is touching the poly somewhere
	// a split should happen along that edge across the entire polygon
	cutLine.start = cutLine.start - cutLine.dir * MAX_COORD;
	cutLine.end = cutLine.end + cutLine.dir * MAX_COORD;

	splitPolys.push_back(vector<vec3>());
	splitPolys.push_back(vector<vec3>());

	int edgeIntersections = 0;

	//logf("VErt intersects: %d\n", vertIntersections);

	for (int i = 0; i < localVerts.size(); i++) {
		vec2 e1 = localVerts[i];
		vec2 e2 = localVerts[(i + 1) % localVerts.size()];
		Line2D edge(e1, e2);

		int polyIdx = edgeIntersections == 1 ? 1 : 0;

		if (edge.doesIntersect(cutLine)) {
			vec2 intersect = edge.intersect(cutLine);
			vec3 worldPos = (localToWorld * vec4(intersect.x, intersect.y, fdist, 1)).xyz();

			splitPolys[polyIdx].push_back(verts[i]);
			splitPolys[0].push_back(worldPos);
			splitPolys[1].push_back(worldPos);

			edgeIntersections++;
			if (edgeIntersections > 2) {
				logf(">2 edge intersections! Has %d vert intersect\n", vertIntersections);
				return vector<vector<vec3>>();
			}
		}
		else {
			splitPolys[polyIdx].push_back(verts[i]);
		}
	}

	//logf("Edge intersects: %d\n", edgeIntersections);

	if (edgeIntersections <= 1) {
		return vector<vector<vec3>>();
	}

	if (splitPolys[0].size() < 3 || splitPolys[1].size() < 3) {
		//logf("Degenerate split!\n");
		return vector<vector<vec3>>();
	}

	return splitPolys;
}

vector<vector<vec3>> Polygon3D::split(const Polygon3D& cutPoly) {
	if (!boxesIntersect(worldMins, worldMaxs, cutPoly.worldMins, cutPoly.worldMaxs)) {
		return vector<vector<vec3>>();
	}

	for (int i = 0; i < cutPoly.verts.size(); i++) {
		const vec3& e1 = cutPoly.verts[i];
		const vec3& e2 = cutPoly.verts[(i + 1) % cutPoly.verts.size()];

		if (fabs(distance(e1)) < EPSILON && fabs(distance(e2)) < EPSILON) {
			//logf("Edge %d is inside\n", i);
			g_app->debugLine0 = e1;
			g_app->debugLine1 = e2;
			return cut(Line2D(project(e1), project(e2)));
		}
	}

	return vector<vector<vec3>>();
}