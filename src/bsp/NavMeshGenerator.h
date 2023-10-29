#pragma once
#include "Polygon3D.h"

class NavMesh;
class Bsp;
class PolygonOctree;

// generates a navigation mesh for a BSP
class NavMeshGenerator {
public:
	NavMeshGenerator() {}

	// generate a nav mesh from the bsp
	// returns polygons used to construct the mesh
	NavMesh* generate(Bsp* map, int hull);

private:
	int octreeDepth = 6;

	// get faces of the hull that form the borders of the map
	vector<Polygon3D*> getHullFaces(Bsp* map, int hull);

	// get smallest octree box that can contain the entire map
	void getOctreeBox(Bsp* map, vec3& min, vec3& max);

	// group polys that are close together for fewer collision checks later
	PolygonOctree* createPolyOctree(Bsp* map, const vector<Polygon3D*>& faces, int treeDepth);

	// splits faces along their intersections with each other to clip polys that extend out
	// into the void, then tests each poly to see if it faces into the map or into the void.
	// Returns clipped faces that face the interior of the map
	vector<Polygon3D> getInteriorFaces(Bsp* map, int hull, vector<Polygon3D*>& faces);

	// merged polys adjacent to each other to reduce node count
	void mergeFaces(Bsp* map, vector<Polygon3D>& faces);

	// removes tiny faces
	void cullTinyFaces(vector<Polygon3D>& faces);

	// links nav polys that share an edge from a top-down view
	// climbability depends on game settings (gravity, stepsize, autoclimb, grapple/gauss weapon, etc.)
	void linkNavPolys(NavMesh* mesh);
};