#pragma once
#include "vectors.h"

struct Line2D {
	vec2 start;
	vec2 end;
	vec2 dir;

	Line2D() {}

	Line2D(vec2 start, vec2 end);

	// distance between this point and the axis of this line
	float distanceAxis(vec2 p);

	// distance between this point and line segment, accounting for points beyond the start/end
	float distance(vec2 p);

	// projects a point onto the line segment
	vec2 project(vec2 p);

	bool doesIntersect(const Line2D& l2);

	// call doesIntersect for line segments first, this returns the intersection point for infinite lines
	vec2 intersect(const Line2D& l2);

	// returns true if the lines align on the same axis
	bool isAlignedWith(const Line2D& other);

	// returns the the distance that the lines overlap, and sets range of overlap for each segment
	// t0-t1 = 0-1 range for this segment
	// t2-t2 = 0-1 range for other segment
	float getOverlapRanges(Line2D& other, float& t0, float& t1, float& t2, float& t3);
};
