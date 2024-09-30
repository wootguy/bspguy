#include "Line2D.h"
#include "util.h"
#include <algorithm>

Line2D::Line2D(vec2 start, vec2 end) {
	this->start = start;
	this->end = end;
	dir = (end - start).normalize();
}

float Line2D::distanceAxis(vec2 p) {
	return crossProduct(dir, start - p);
}

float Line2D::distance(vec2 p) {
	float len = (end - start).length();
	float t = dotProduct(p - start, dir) / len;

	if (t < 0) {
		return (p - start).length();
	} else if (t > 1) {
		return (p - end).length();
	}

	return distanceAxis(p);
}

vec2 Line2D::project(vec2 p) {
	float dot = dotProduct(p - start, dir);
	return start + dir*dot;
}

bool Line2D::isAlignedWith(const Line2D& other) {
	if (fabs(dotProduct(dir, other.dir)) < 0.999f) {
		return false; // lines not colinear
	}

	// Calculate the cross products
	float cross1 = crossProduct(dir, other.start - start);
	float cross2 = crossProduct(dir, other.end - start);

	// If the cross products have same signs, the lines don't overlap
	return cross1 * cross2 < EPSILON;
}

float Line2D::getOverlapRanges(Line2D& other, float& t0, float& t1, float& t2, float& t3) {
	float d1 = dotProduct(start, dir);
	float d2 = dotProduct(end, dir);
	float d3 = dotProduct(other.start, dir);
	float d4 = dotProduct(other.end, dir);

	bool flipOtherEdge = d4 < d3;
	if (flipOtherEdge) {
		float temp = d3;
		d3 = d4;
		d4 = temp;
	}

	float overlapStart = max(d1, d3);
	float overlapEnd = min(d2, d4);
	float overlapDist = overlapEnd - overlapStart;

	if (overlapDist < 0.0f) {
		return 0;
	}

	float len1 = d2 - d1;
	float len2 = d4 - d3;

	if (len1 == 0 || len2 == 0) {
		//logf("%s: 0 length segments\n", __func__);
		return 0;
	}

	t0 = (overlapStart - d1) / len1;
	t1 = (overlapEnd - d1) / len1;

	t2 = (overlapStart - d3) / len2;
	t3 = (overlapEnd - d3) / len2;
	if (flipOtherEdge) {
		t2 = 1.0f - t2;
		t3 = 1.0f - t3;
	}

	return overlapDist;
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