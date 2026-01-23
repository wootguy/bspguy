#pragma once
#include <string.h>
class Bsp;

// excludes entities
struct STRUCTCOUNT {
	int planes;
	int texInfos;
	int leaves;
	int nodes;
	int clipnodes;
	int verts;
	int faces;
	int textures;
	int markSurfs;
	int surfEdges;
	int edges;
	int models;
	int lightstyles;
	int lightdata;
	int visdata;

	STRUCTCOUNT();
	STRUCTCOUNT(Bsp* map);

	void add(const STRUCTCOUNT& other);
	void sub(const STRUCTCOUNT& other);
	bool allZero();
	void print_delete_stats(int indent);
};

// used to mark structures that are in use by a model
struct STRUCTUSAGE
{
	bool* nodes = NULL;
	bool* clipnodes = NULL;
	bool* leaves = NULL;
	bool* planes = NULL;
	bool* verts = NULL;
	bool* texInfo = NULL;
	bool* faces = NULL;
	bool* textures = NULL;
	bool* markSurfs = NULL;
	bool* surfEdges = NULL;
	bool* edges = NULL;

	STRUCTCOUNT count; // size of each array
	STRUCTCOUNT sum;

	int modelIdx = 0;

	STRUCTUSAGE() {}
	STRUCTUSAGE(Bsp* map);
	~STRUCTUSAGE();

	void init(Bsp* map);
	void clear();
	void compute_sum();
	void merge(STRUCTUSAGE& other);
};

// used to remap structure indexes to new locations
struct STRUCTREMAP
{
	int* nodes;
	int* clipnodes;
	int* leaves;
	int* planes;
	int* verts;
	int* texInfo;
	int* faces;
	int* textures;
	int* markSurfs;
	int* surfEdges;
	int* edges;

	// don't try to update the same nodes twice
	bool* visitedNodes;
	bool* visitedClipnodes;
	bool* visitedLeaves;
	bool* visitedFaces;

	STRUCTCOUNT count; // size of each array

	STRUCTREMAP(Bsp* map);
	~STRUCTREMAP();
};
