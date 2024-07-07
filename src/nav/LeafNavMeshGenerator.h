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
	LeafNavMesh* generate(Bsp* map);

private:
	int octreeDepth = 6;

	// get leaves of the bsp tree with the given contents
	vector<LeafNode> getHullLeaves(Bsp* map, int modelIdx, int contents);

	// get smallest octree box that can contain the entire map
	void getOctreeBox(Bsp* map, vec3& min, vec3& max);

	// group polys that are close together for fewer collision checks later
	LeafOctree* createLeafOctree(Bsp* map, vector<LeafNode>& mesh, int treeDepth);

	// merged polys adjacent to each other to reduce node count
	void mergeLeaves(Bsp* map, vector<LeafNode>& leaves);

	// removes tiny faces
	void cullTinyLeaves(vector<LeafNode>& leaves);

	// links nav leaves which have faces touching each other
	void linkNavLeaves(Bsp* map, LeafNavMesh* mesh);

	// use ladder entities to create cheaper paths between leaves
	void linkLadderLeaves(Bsp* map, LeafNavMesh* mesh);

	int tryFaceLinkLeaves(Bsp* map, LeafNavMesh* mesh, int srcLeafIdx, int dstLeafIdx);

	void calcPathCosts(Bsp* bsp, LeafNavMesh* mesh);

	void addPathCost(LeafLink& link, Bsp* bsp, vec3 start, vec3 end, bool isDrop);
};