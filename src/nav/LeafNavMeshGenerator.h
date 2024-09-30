#pragma once
#include "Polygon3D.h"
#include "LeafNavMesh.h"
#include "Clipper.h"

class Bsp;
class LeafOctree;

// generates a navigation mesh for a BSP
class LeafNavMeshGenerator {
public:
	LeafNavMeshGenerator() {}

	// generate a nav mesh from the bsp
	// returns polygons used to construct the mesh
	LeafNavMesh* generate(Bsp* map);

	// splits leaves by solid entity faces
	void splitEntityLeaves(Bsp* map, LeafNavMesh* mesh);

	// finds best origin for a leaf
	void setLeafOrigins(Bsp* map, LeafNavMesh* mesh, int offset);

	// links nav leaves which have faces touching each other
	void linkNavLeaves(Bsp* map, LeafNavMesh* mesh, int offset);

	// use on a mesh that has already had all parent leaves linked
	void linkNavChildLeaves(Bsp* map, LeafNavMesh* mesh, int offset);

	// use entities to create cheaper paths between leaves
	void linkEntityLeaves(Bsp* map, LeafNavMesh* mesh, int offset);

private:
	int octreeDepth = 6;

	// get leaves of the bsp tree with the given contents
	vector<LeafNode> getHullLeaves(Bsp* map, int modelIdx, int contents);

	bool getHullForClipperMesh(CMesh& mesh, LeafNode& node);

	// get smallest octree box that can contain the entire map
	void getOctreeBox(Bsp* map, vec3& min, vec3& max);

	// group leaves that are close together for fewer collision checks later
	LeafOctree* createLeafOctree(Bsp* map, vector<LeafNode>& mesh, int treeDepth);

	// splits a leaf by one or more entities
	// includeSolidNode = true if the entity can be passed through without noclip
	void splitLeafByEnts(Bsp* map, LeafNavMesh* mesh, LeafNode& node, vector<LeafNode>& entNodes, bool includeSolidNode);

	// find point on poly which is closest to a floor, using distance to the bias point as a tie breaker
	vec3 getBestPolyOrigin(Bsp* map, Polygon3D& poly, vec3 bias);

	void linkEntityLeaves(Bsp* map, LeafNavMesh* mesh, LeafNode& entNode, vector<bool>& regionLeaves);

	// returns a combined node for an entity, which is the bounding box of all its model leaves
	LeafNode getSolidEntityNode(Bsp* map, LeafNavMesh* mesh, int bspModelIdx, vec3 origin);

	// returns a node for an entity, which is its bounding box
	LeafNode& addPointEntityNode(Bsp* map, LeafNavMesh* mesh, int entidx, vec3 mins, vec3 maxs);

	int tryFaceLinkLeaves(Bsp* map, LeafNavMesh* mesh, int srcLeafIdx, int dstLeafIdx);

	void calcPathCosts(Bsp* bsp, LeafNavMesh* mesh);

	void calcPathCost(Bsp* bsp, LeafNavMesh* mesh, LeafNode& node, LeafLink& link);

	void addPathCost(LeafLink& link, Bsp* bsp, vec3 start, vec3 end, bool isDrop);
};