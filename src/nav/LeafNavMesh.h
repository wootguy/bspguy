#pragma once
#include "Polygon3D.h"
#include <map>

#define MAX_MAP_CLIPNODE_LEAVES 65536 // doubled to account for each clipnode's child contents having its own ID
#define NAV_INVALID_IDX 65535

#define NAV_STEP_HEIGHT 18
#define NAV_JUMP_HEIGHT 44
#define NAV_CROUCHJUMP_HEIGHT 63	// 208 gravity 50%
#define NAV_CROUCHJUMP_STACK_HEIGHT 135
#define NAV_AUTOCLIMB_HEIGHT 117
#define NAV_HULL 3

#define NAV_BOTTOM_EPSILON 1.0f // move waypoints this far from the bottom of the node

#define NAV_LEAF_PARENT 1 // node was split by entity leaves and should not be used
#define NAV_LEAF_CHILD 2 // node was created by splitting a world leaf by an entity

class Bsp;
class Entity;
class LeafOctree;
class VertexBuffer;

struct LeafLink {
	uint16_t node; // which leaf is linked to
	vec3 pos; // link position
	float baseCost; // flat cost for using this path
	float costMultiplier; // cost applied to length of path
	
	// for debugging
	Polygon3D linkArea; // region in which leaves are making contact
};

struct LeafNode {
	vector<LeafLink> links;
	uint16_t id;
	vec3 origin; // the best position for pathing (not necessarily the center)
	int16_t entidx; // 0 for world leaves, else an entity leaf which may be relocated, enabled, or disabled
	uint16_t parentIdx; // parent leaf idx if this node is the child of another leaf, else 65535
	uint16_t childIdx; // first child idx if this node contains split leaves, else 65535


	// for debugging
	vec3 center;
	vec3 mins, maxs; // for octree insertion, not needed after generation
	vector<Polygon3D> leafFaces;
	VertexBuffer* face_buffer;
	VertexBuffer* wireframe_buffer;

	LeafNode();

	bool addLink(int node, Polygon3D linkArea);

	bool addLink(int node, vec3 linkPos);

	// returns true if point is inside leaf volume
	bool isInside(vec3 p, float epsilon=0.0f);

	bool intersects(Polygon3D& poly);
};


class LeafNavMesh {
public:
	vector<LeafNode> nodes;
	LeafOctree* octree; // finds nearby leaves from any point in space, even outside of the BSP tree
	uint16_t leafMap[MAX_MAP_CLIPNODE_LEAVES]; // maps a BSP leaf index to nav mesh node index

	LeafNavMesh();

	LeafNavMesh(vector<LeafNode> polys, LeafOctree* octree);

	bool addLink(int from, int to, Polygon3D linkArea);

	void clear();

	vector<int> AStarRoute(int startNodeIdx, int endNodeIdx);

	vector<int> dijkstraRoute(int start, int end);

	float path_cost(int a, int b);

	int getNodeIdx(Bsp* map, Entity* ent);

	// accounts for leaves that have been split by entities
	uint16_t getNodeIdx(Bsp* map, vec3 pos);

	// splits nodes again when entities change
	void refreshNodes(Bsp* map);

private:	
};