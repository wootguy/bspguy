#pragma once
#include "Polygon3D.h"
#include "PolyOctree.h"
#include <set>
#include "Bsp.h"

#define MAX_NAV_POLYS 8192
#define MAX_NAV_POLY_VERTS 12
#define MAX_EDGE_LINKS 8

struct NavLink {
	uint16_t node; // which poly is linked to. 0 = end of links
	int16_t zDist; // minimum height difference between the connecting edges
};

struct NavPolyNode {
	NavLink edges[MAX_NAV_POLY_VERTS][MAX_EDGE_LINKS];
	uint32_t flags;
};


class NavMesh {
public:
	NavPolyNode nodes[MAX_NAV_POLYS];
	Polygon3D polys[MAX_NAV_POLYS];

	int numPolys;

	NavMesh();

	NavMesh(vector<Polygon3D> polys);

	void clear();

	vector<Polygon3D> getPolys();

private:
};