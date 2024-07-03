#pragma once
#include "Polygon3D.h"
#include "LeafNavMesh.h"

class Bsp;
class LeafOctree;

// generates a navigation mesh for a BSP
class LeafNavMeshGenerator {
public:
	LeafNavMeshGenerator() {}

	// generate a nav mesh from the bsp
	// returns polygons used to construct the mesh
	LeafNavMesh* generate(Bsp* map, int hull);

private:
	int octreeDepth = 6;

	// get empty leaves of the bsp tree
	vector<LeafMesh> getHullLeaves(Bsp* map, int hull);

	// get smallest octree box that can contain the entire map
	void getOctreeBox(Bsp* map, vec3& min, vec3& max);

	// group polys that are close together for fewer collision checks later
	LeafOctree* createLeafOctree(Bsp* map, LeafNavMesh* mesh, int treeDepth);

	// merged polys adjacent to each other to reduce node count
	void mergeLeaves(Bsp* map, vector<LeafMesh>& leaves);

	// removes tiny faces
	void cullTinyLeaves(vector<LeafMesh>& leaves);

	// links nav polys that share an edge from a top-down view
	// climbability depends on game settings (gravity, stepsize, autoclimb, grapple/gauss weapon, etc.)
	void linkNavLeaves(Bsp* map, LeafNavMesh* mesh);

	int tryFaceLinkLeaves(Bsp* map, LeafNavMesh* mesh, int srcLeafIdx, int dstLeafIdx);

	void markWalkableLinks(Bsp* bsp, LeafNavMesh* mesh);

	bool isWalkable(Bsp* bsp, vec3 start, vec3 end);
};