#pragma once
#include "Polygon3D.h"

#define MAX_NAV_POLYS 4096
#define MAX_NAV_POLY_VERTS 16
#define MAX_NAV_LINKS 32

#define FL_LINK_LONGJUMP	(1<<1) // must use the longjump to reach poly
#define FL_LINK_TELEPORT	(1<<2) // must use teleport in source poly to reach target poly

#define NAV_STEP_HEIGHT 18
#define NAV_JUMP_HEIGHT 44
#define NAV_CROUCHJUMP_HEIGHT 63	// 208 gravity 50%
#define NAV_AUTOCLIMB_HEIGHT 117

struct NavLink {
	uint8_t srcEdge : 4; // edge to move from in source poly
	uint8_t dstEdge : 4; // edge to move to in target/destination poly
	uint8_t flags;
	int16_t node; // which poly is linked to. -1 = end of links
	int16_t zDist; // minimum height difference between the connecting edges
};

struct NavNode {
	NavLink links[MAX_NAV_LINKS];
	uint32_t flags;
	uint16_t id;

	// adds a link to node "node" on edge "edge" with height difference "zDist"
	bool addLink(int node, int srcEdge, int dstEdge, int16_t zDist, uint8_t flags);
	int numLinks();
};


class NavMesh {
public:
	NavNode nodes[MAX_NAV_POLYS];
	Polygon3D polys[MAX_NAV_POLYS];

	int numPolys;

	NavMesh();

	NavMesh(vector<Polygon3D> polys);

	bool addLink(int from, int to, int srcEdge, int dstEdge, int16_t zDist, uint8_t flags);

	void clear();

	vector<Polygon3D> getPolys();

private:
};