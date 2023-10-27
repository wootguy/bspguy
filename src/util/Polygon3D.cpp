#include "Polygon3D.h"
#include "util.h"
#include "BspRenderer.h"
#include "Renderer.h"

#define COLINEAR_EPSILON 0.125f
#define SAME_VERT_EPSILON 0.125f
#define COLINEAR_CUT_EPSILON 0.25f // increase if cutter gets stuck in a loop cutting the same polys

bool vec3Equal(vec3 v1, vec3 v2, float epsilon)
{
	vec3 v = v1 - v2;
	if (fabs(v.x) >= epsilon)
		return false;
	if (fabs(v.y) >= epsilon)
		return false;
	if (fabs(v.z) >= epsilon)
		return false;
	return true;
}

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
	init();
}

Polygon3D::Polygon3D(const vector<vec3>& verts, int idx) {
	this->verts = verts;
	this->idx = idx;
	init();
}

void Polygon3D::init() {
	vector<vec3> triangularVerts = getTriangularVerts(this->verts);
	localVerts.clear();
	isValid = false;
	center = vec3();
	area = 0;

	localMins = vec2(FLT_MAX, FLT_MAX);
	localMaxs = vec2(-FLT_MAX, -FLT_MAX);

	worldMins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	worldMaxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

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

	if (localToWorld.m[15] == 0) {
		// failed matrix inversion
		return;
	}

	for (int e = 0; e < verts.size(); e++) {
		vec2 localPoint = project(verts[e]);
		localVerts.push_back(localPoint);
		expandBoundingBox(localPoint, localMins, localMaxs);
		expandBoundingBox(verts[e], worldMins, worldMaxs);
		center += verts[e];
	}

	for (int i = 0; i < localVerts.size(); i++) {
		area += crossProduct(localVerts[i], localVerts[(i+1) % localVerts.size()]);
	}
	area = fabs(area) * 0.5f;

	center /= (float)verts.size();

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

	bool intersectsAnyEdge = false;
	if (isInside(cutLine.start) || isInside(cutLine.end)) {
		intersectsAnyEdge = true;
	}

	if (!intersectsAnyEdge) {
		for (int i = 0; i < localVerts.size(); i++) {
			vec2 e1 = localVerts[i];
			vec2 e2 = localVerts[(i + 1) % localVerts.size()];
			Line2D edge(e1, e2);

			if (edge.doesIntersect(cutLine)) {
				intersectsAnyEdge = true;
				break;
			}
		}
	}
	if (!intersectsAnyEdge) {
		//logf("No edge intersections\n");
		return splitPolys;
	}

	// extend to "infinity" if we know the cutting edge is touching the poly somewhere
	// a split should happen along that edge across the entire polygon
	cutLine.start = cutLine.start - cutLine.dir * MAX_COORD;
	cutLine.end = cutLine.end + cutLine.dir * MAX_COORD;

	for (int i = 0; i < localVerts.size(); i++) {
		vec2 e1 = localVerts[i];
		vec2 e2 = localVerts[(i + 1) % localVerts.size()];

		float dist1 = fabs(cutLine.distance(e1));
		float dist2 = fabs(cutLine.distance(e2));

		if (dist1 < COLINEAR_CUT_EPSILON && dist2 < COLINEAR_CUT_EPSILON) {
			//logf("cut is colinear with an edge\n");
			return splitPolys; // line is colinear with an edge, no intersections possible
		}
	}


	splitPolys.push_back(vector<vec3>());
	splitPolys.push_back(vector<vec3>());


	// get new verts with intersection points included
	vector<vec3> newVerts;
	vector<vec2> newLocalVerts;

	for (int i = 0; i < localVerts.size(); i++) {
		int next = (i + 1) % localVerts.size();
		vec2 e1 = localVerts[i];
		vec2 e2 = localVerts[next];
		Line2D edge(e1, e2);

		newVerts.push_back(verts[i]);
		newLocalVerts.push_back(e1);

		if (edge.doesIntersect(cutLine)) {
			vec2 intersect = edge.intersect(cutLine);
			vec3 worldPos = (localToWorld * vec4(intersect.x, intersect.y, fdist, 1)).xyz();

			if (!vec3Equal(worldPos, verts[i], SAME_VERT_EPSILON) && !vec3Equal(worldPos, verts[next], SAME_VERT_EPSILON)) {
				newVerts.push_back(worldPos);
				newLocalVerts.push_back(intersect);
			}
		}
	}

	// define new polys (separate by left/right of line
	for (int i = 0; i < newLocalVerts.size(); i++) {
		float dist = cutLine.distance(newLocalVerts[i]);

		if (dist < -SAME_VERT_EPSILON) {
			splitPolys[0].push_back(newVerts[i]);
		}
		else if (dist > SAME_VERT_EPSILON) {
			splitPolys[1].push_back(newVerts[i]);
		}
		else {
			splitPolys[0].push_back(newVerts[i]);
			splitPolys[1].push_back(newVerts[i]);
		}
	}

	g_app->debugCut = cutLine;

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
			//logf("Edge %d is inside %.1f %.1f\n", i, distance(e1), distance(e2));
			g_app->debugLine0 = e1;
			g_app->debugLine1 = e2;
			return cut(Line2D(project(e1), project(e2)));
		}
	}

	return vector<vector<vec3>>();
}

bool Polygon3D::isConvex() {
	int n = localVerts.size();
	if (n < 3) {
		return false;
	}

	int sign = 0;  // Initialize the sign of the cross product

	for (int i = 0; i < n; i++) {
		const vec2& A = localVerts[i];
		const vec2& B = localVerts[(i + 1) % n];  // Next vertex
		const vec2& C = localVerts[(i + 2) % n];  // Vertex after the next

		// normalizing prevents small epsilons not working for large differences in edge lengths
		vec2 AB = vec2(B.x - A.x, B.y - A.y).normalize();
		vec2 BC = vec2(C.x - B.x, C.y - B.y).normalize();

		float current_cross_product = crossProduct(AB, BC);

		if (fabs(current_cross_product) < COLINEAR_EPSILON) {
			continue;  // Skip collinear points
		}

		if (sign == 0) {
			sign = (current_cross_product > 0) ? 1 : -1;
		}
		else {
			if ((current_cross_product > 0 && sign == -1) || (current_cross_product < 0 && sign == 1)) {
				return false;
			}
		}
	}

	return true;
}

void Polygon3D::removeDuplicateVerts() {
	vector<vec3> newVerts;

	int sz = localVerts.size();
	for (int i = 0; i < sz; i++) {
		int last = (i + (sz - 1)) % sz;

		if (!vec3Equal(verts[i], verts[last], SAME_VERT_EPSILON))
			newVerts.push_back(verts[i]);
	}

	if (verts.size() != newVerts.size()) {
		logf("Removed %d duplicate verts\n", verts.size() - newVerts.size());
		verts = newVerts;
		init();
	}
}

void Polygon3D::extendAlongAxis(float amt) {
	for (int i = 0; i < verts.size(); i++) {
		verts[i] += plane_z * amt;
	}

	init();
}

void Polygon3D::removeColinearVerts() {
	vector<vec3> newVerts;

	if (verts.size() < 3) {
		logf("Not enough verts to remove colinear ones\n");
		return;
	}

	int sz = localVerts.size();
	for (int i = 0; i < sz; i++) {
		const vec2& A = localVerts[(i + (sz-1)) % sz];
		const vec2& B = localVerts[i];
		const vec2& C = localVerts[(i + 1) % sz];

		vec2 AB = vec2(B.x - A.x, B.y - A.y).normalize();
		vec2 BC = vec2(C.x - B.x, C.y - B.y).normalize();
		float cross = crossProduct(AB, BC);

		if (fabs(cross) >= COLINEAR_EPSILON) {
			newVerts.push_back(verts[i]);
		}
	}

	if (verts.size() != newVerts.size()) {
		//logf("Removed %d colinear verts\n", verts.size() - newVerts.size());
		verts = newVerts;
		init();
	}
}

Polygon3D Polygon3D::merge(const Polygon3D& mergePoly) {
	vector<vec3> mergedVerts;
	
	float epsilon = 1.0f;

	if (fabs(fdist - mergePoly.fdist) > epsilon || dotProduct(plane_z, mergePoly.plane_z) < 0.99f)
		return mergedVerts; // faces not coplaner

	int sharedEdges = 0;
	int commonEdgeStart1 = -1, commonEdgeEnd1 = -1;
	int commonEdgeStart2 = -1, commonEdgeEnd2 = -1;
	for (int i = 0; i < verts.size(); i++) {
		const vec3& e1 = verts[i];
		const vec3& e2 = verts[(i + 1) % verts.size()];

		for (int k = 0; k < mergePoly.verts.size(); k++) {
			const vec3& other1 = mergePoly.verts[k];
			const vec3& other2 = mergePoly.verts[(k + 1) % mergePoly.verts.size()];

			if ((vec3Equal(e1, other1, epsilon) && vec3Equal(e2, other2, epsilon))
				|| (vec3Equal(e1, other2, epsilon) && vec3Equal(e2, other1, epsilon))) {
				commonEdgeStart1 = i;
				commonEdgeEnd1 = (i + 1) % verts.size();
				commonEdgeStart2 = k;
				commonEdgeEnd2 = (k + 1) % mergePoly.verts.size();
				sharedEdges++;
			}
		}
	}

	if (sharedEdges == 0)
		return Polygon3D();
	if (sharedEdges > 1) {
		//logf("More than 1 shared edge for merge!\n");
		return Polygon3D();
	}

	mergedVerts.reserve(verts.size() + mergePoly.verts.size() - 2);
	for (int i = commonEdgeEnd1; i != commonEdgeStart1; i = (i + 1) % verts.size()) {
		mergedVerts.push_back(verts[i]);
	}
	for (int i = commonEdgeEnd2; i != commonEdgeStart2; i = (i + 1) % mergePoly.verts.size()) {
		mergedVerts.push_back(mergePoly.verts[i]);
	}

	Polygon3D newPoly(mergedVerts);

	if (!newPoly.isConvex()) {
		return Polygon3D();
	}
	newPoly.removeColinearVerts();

	return newPoly;
}