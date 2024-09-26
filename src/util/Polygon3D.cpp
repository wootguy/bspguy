#include "Polygon3D.h"
#include "util.h"
#include "Renderer.h"
#include "globals.h"
#include <float.h>
#include <stack>

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

Polygon3D::Polygon3D(const vector<vec3>& verts) {
	this->verts = verts;
	init();
}

Polygon3D::Polygon3D(const vector<vec3>& verts, int idx) {
	this->verts = verts;
	this->idx = idx;
	init();
}

int Polygon3D::sizeBytes() {
	return sizeof(Polygon3D) 
		+ sizeof(vec3) * verts.size() 
		+ sizeof(vec2) * localVerts.size()
		+ sizeof(vec2) * topdownVerts.size();
}

void Polygon3D::init() {
	vector<vec3> triangularVerts = getTriangularVerts(this->verts);
	localVerts.clear();
	topdownVerts.clear();
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
		topdownVerts.push_back(vec2(verts[e].x, verts[e].y));
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
bool Polygon3D::isInside(vec2 p, bool includeEdge) {
	int windingNumber = 0;

	for (int i = 0; i < localVerts.size(); i++) {
		const vec2& p1 = localVerts[i];
		const vec2& p2 = localVerts[(i + 1) % localVerts.size()];

		if (p1.y <= p.y) {
			if (p2.y > p.y && isLeft(p1, p2, p) > 0) {
				windingNumber += 1;
			}
		}
		else if (p2.y <= p.y && isLeft(p1, p2, p) < 0) {
			windingNumber -= 1;
		}

		Line2D edge(p1, p2);
		float dist = edge.distance(p);

		if (fabs(dist) < INPOLY_EPSILON) {
			return includeEdge; // point is too close to an edge
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
	cutLine.start = cutLine.start - cutLine.dir * MAX_MAP_COORD;
	cutLine.end = cutLine.end + cutLine.dir * MAX_MAP_COORD;

	for (int i = 0; i < localVerts.size(); i++) {
		vec2 e1 = localVerts[i];
		vec2 e2 = localVerts[(i + 1) % localVerts.size()];

		float dist1 = fabs(cutLine.distanceAxis(e1));
		float dist2 = fabs(cutLine.distanceAxis(e2));

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
		float dist = cutLine.distanceAxis(newLocalVerts[i]);

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

void Polygon3D::removeDuplicateVerts(float epsilon) {
	vector<vec3> newVerts;

	int sz = verts.size();
	for (int i = 0; i < sz; i++) {
		int last = (i + (sz - 1)) % sz;

		if (!vec3Equal(verts[i], verts[last], epsilon))
			newVerts.push_back(verts[i]);
	}

	if (verts.size() != newVerts.size()) {
		//logf("Removed %d duplicate verts\n", verts.size() - newVerts.size());
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

void push_unique_vert(vector<vec2>& verts, vec2 vert) {
	for (int k = 0; k < verts.size(); k++) {
		if ((verts[k] - vert).length() < 0.125f) {
			return;
		}
	}

	verts.push_back(vert);
}


namespace GrahamScan {
	// https://www.tutorialspoint.com/cplusplus-program-to-implement-graham-scan-algorithm-to-find-the-convex-hull
	vec2 p0;

	vec2 secondTop(stack<vec2>& stk) {
		vec2 tempvec2 = stk.top();
		stk.pop();
		vec2 res = stk.top();    //get the second top element
		stk.push(tempvec2);      //push previous top again
		return res;
	}

	int squaredDist(vec2 p1, vec2 p2) {
		return ((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y));
	}

	int direction(vec2 a, vec2 b, vec2 c) {
		int val = (b.y - a.y) * (c.x - b.x) - (b.x - a.x) * (c.y - b.y);
		if (val == 0)
			return 0;    //colinear
		else if (val < 0)
			return 2;    //anti-clockwise direction
		return 1;    //clockwise direction
	}

	int comp(const void* point1, const void* point2) {
		vec2* p1 = (vec2*)point1;
		vec2* p2 = (vec2*)point2;
		int dir = direction(p0, *p1, *p2);
		if (dir == 0)
			return (squaredDist(p0, *p2) >= squaredDist(p0, *p1)) ? -1 : 1;
		return (dir == 2) ? -1 : 1;
	}

	vector<vec2> findConvexHull(vec2 points[], int n) {
		vector<vec2> convexHullPoints;
		int minY = points[0].y, min = 0;

		for (int i = 1; i < n; i++) {
			int y = points[i].y;
			//find bottom most or left most point
			if ((y < minY) || (minY == y) && points[i].x < points[min].x) {
				minY = points[i].y;
				min = i;
			}
		}

		swap(points[0], points[min]);    //swap min point to 0th location
		p0 = points[0];
		qsort(&points[1], n - 1, sizeof(vec2), comp);    //sort points from 1 place to end

		int arrSize = 1;    //used to locate items in modified array
		for (int i = 1; i < n; i++) {
			//when the angle of ith and (i+1)th elements are same, remove points
			while (i < n - 1 && direction(p0, points[i], points[i + 1]) == 0)
				i++;
			points[arrSize] = points[i];
			arrSize++;
		}

		if (arrSize < 3)
			return convexHullPoints;    //there must be at least 3 points, return empty list.
			//create a stack and add first three points in the stack

		stack<vec2> stk;
		stk.push(points[0]); stk.push(points[1]); stk.push(points[2]);
		for (int i = 3; i < arrSize; i++) {    //for remaining vertices
			while (direction(secondTop(stk), stk.top(), points[i]) != 2)
				stk.pop();    //when top, second top and ith point are not making left turn, remove point
			stk.push(points[i]);
		}

		while (!stk.empty()) {
			convexHullPoints.push_back(stk.top());    //add points from stack
			stk.pop();
		}

		return convexHullPoints;
	}
};

Polygon3D Polygon3D::coplanerIntersectArea(Polygon3D otherPoly) {
	vector<vec3> outVerts;

	float epsilon = 1.0f;

	if (fabs(-fdist - otherPoly.fdist) > epsilon || dotProduct(plane_z, otherPoly.plane_z) > -0.99f)
		return outVerts; // faces are not coplaner with opposite normals

	// project other polys verts onto the same coordinate system as this face
	vector<vec2> otherLocalVerts;
	for (int i = 0; i < otherPoly.verts.size(); i++) {
		otherLocalVerts.push_back(project(otherPoly.verts[i]));
	}
	otherPoly.localVerts = otherLocalVerts;

	vector<vec2> localOutVerts;

	// find intersection points
	for (int i = 0; i < localVerts.size(); i++) {
		vec2& va1 = localVerts[i];
		vec2& va2 = localVerts[(i + 1) % localVerts.size()];
		Line2D edgeA(va1, va2);

		if (otherPoly.isInside(va1, true)) {
			otherPoly.isInside(va1, true);
			push_unique_vert(localOutVerts, va1);
		}

		for (int k = 0; k < otherLocalVerts.size(); k++) {
			vec2& vb1 = otherLocalVerts[k];
			vec2& vb2 = otherLocalVerts[(k + 1) % otherLocalVerts.size()];
			Line2D edgeB(vb1, vb2);

			if (!edgeA.isAlignedWith(edgeB) && edgeA.doesIntersect(edgeB)) {
				push_unique_vert(localOutVerts, edgeA.intersect(edgeB));
			}

			if (isInside(vb1, true)) {
				push_unique_vert(localOutVerts, vb1);
			}
		}
	}

	if (localOutVerts.size() < 3) {
		return outVerts;
	}

	localOutVerts = GrahamScan::findConvexHull(&localOutVerts[0], localOutVerts.size());

	for (int i = 0; i < localOutVerts.size(); i++) {
		outVerts.push_back(unproject(localOutVerts[i]));
	}

	return outVerts;
}

bool Polygon3D::intersects(Polygon3D& otherPoly) {
	vec3 isect;
	const float eps = 0.5f;

	for (int i = 0; i < verts.size(); i++) {
		vec3 va = verts[i];
		vec3 vb = verts[(i + 1) % verts.size()];

		if (fabs(otherPoly.distance(va)) < eps ||fabs( otherPoly.distance(vb)) < eps) {
			// edge is touching the face, but otherwise fully on one side of it
			continue;
		}

		if (otherPoly.intersect(va, vb, isect)) {
			return true;
		}
	}

	for (int i = 0; i < otherPoly.verts.size(); i++) {
		vec3 va = otherPoly.verts[i];
		vec3 vb = otherPoly.verts[(i + 1) % verts.size()];

		if (fabs(distance(va)) < eps || fabs(distance(vb)) < eps) {
			// edge is touching the face, but otherwise fully on one side of it
			continue;
		}

		if (intersect(va, vb, isect)) {
			return true;
		}
	}

	return false;
}

bool Polygon3D::intersect(vec3 p1, vec3 p2, vec3& ipos) {
	float t1 = dotProduct(plane_z, p1) - fdist;
	float t2 = dotProduct(plane_z, p2) - fdist;

	if ((t1 >= 0.0f && t2 >= 0.0f) || (t1 < 0.0f && t2 < 0.0f)) {
		return false;
	}

	float frac = t1 / (t1 - t2);
	frac = clamp(frac, 0.0f, 1.0f);

	if (frac != frac) {
		return false; // NaN
	}

	ipos = p1 + (p2 - p1) * frac;

	return isInside(project(ipos));
}

bool Polygon3D::intersect2D(vec3 p1, vec3 p2, vec3& ipos) {
	vec2 p1_2d = project(p1);
	vec2 p2_2d = project(p2);

	Line2D line(p1_2d, p2_2d);

	if (isInside(p1_2d, false) == isInside(p2_2d, false)) {
		ipos = p1;
		return false;
	}

	for (int i = 0; i < localVerts.size(); i++) {
		vec2 e1 = localVerts[i];
		vec2 e2 = localVerts[(i + 1) % localVerts.size()];
		Line2D edge(e1, e2);

		if (edge.doesIntersect(line)) {
			ipos = unproject(edge.intersect(line));
			return true;
		}
	}

	ipos = p1;
	return false;
}