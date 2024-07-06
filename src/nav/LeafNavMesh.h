#pragma once
#include "Polygon3D.h"
#include <map>

#define MAX_MAP_CLIPNODE_LEAVES 65536 // doubled to account for each clipnode's child contents having its own ID

#define NAV_STEP_HEIGHT 18
#define NAV_JUMP_HEIGHT 44
#define NAV_CROUCHJUMP_HEIGHT 63	// 208 gravity 50%
#define NAV_CROUCHJUMP_STACK_HEIGHT 135
#define NAV_AUTOCLIMB_HEIGHT 117

#define NAV_BOTTOM_EPSILON 1.0f // move waypoints this far from the bottom of the node

class Bsp;
class Entity;

struct LeafLink {
	int16_t node; // which leaf is linked to. -1 = end of links
	Polygon3D linkArea; // region in which leaves are making contact
	vec3 bottom; // centered at the bottom of the polygon intersection
	float baseCost; // flat cost for using this path
	float costMultiplier; // cost applied to length of path
	bool useMiddleLink;
};

struct LeafNode {
	vector<LeafLink> links;
	uint16_t id;

	vec3 center;
	vec3 bottom;
	vec3 top;
	vec3 mins;
	vec3 maxs;
	vector<Polygon3D> leafFaces;

	LeafNode();

	// adds a link to node "node" on edge "edge" with height difference "zDist"
	bool addLink(int node, Polygon3D linkArea);
	int numLinks();

	// returns true if point is inside leaf volume
	bool isInside(vec3 p);
};


class LeafNavMesh {
public:
	vector<LeafNode> nodes;
	uint16_t leafMap[MAX_MAP_CLIPNODE_LEAVES]; // maps a BSP leaf index to nav mesh node index

	LeafNavMesh();

	LeafNavMesh(vector<LeafNode> polys);

	bool addLink(int from, int to, Polygon3D linkArea);

	void clear();

	vector<int> AStarRoute(int startNodeIdx, int endNodeIdx);

	vector<int> dijkstraRoute(int start, int end);

	float path_cost(int a, int b);

	int getNodeIdx(Bsp* map, Entity* ent);

private:
};