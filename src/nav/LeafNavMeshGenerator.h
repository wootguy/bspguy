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
	vector<LeafNode> getHullLeaves(Bsp* map, int hull);

	// get smallest octree box that can contain the entire map
	void getOctreeBox(Bsp* map, vec3& min, vec3& max);

	// group polys that are close together for fewer collision checks later
	LeafOctree* createLeafOctree(Bsp* map, LeafNavMesh* mesh, int treeDepth);

	// merged polys adjacent to each other to reduce node count
	void mergeLeaves(Bsp* map, vector<LeafNode>& leaves);

	// removes tiny faces
	void cullTinyLeaves(vector<LeafNode>& leaves);

	// links nav leaves which have faces touching each other
	void linkNavLeaves(Bsp* map, LeafNavMesh* mesh);

	int tryFaceLinkLeaves(Bsp* map, LeafNavMesh* mesh, int srcLeafIdx, int dstLeafIdx);

	void markWalkableLinks(Bsp* bsp, LeafNavMesh* mesh);

	void calcPathCost(LeafLink& link, Bsp* bsp, vec3 start, vec3 end);
};