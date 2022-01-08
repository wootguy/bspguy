#pragma once
class Bsp;

// excludes entities
struct STRUCTCOUNT {
	unsigned int planes;
	unsigned int texInfos;
	unsigned int leaves;
	unsigned int nodes;
	unsigned int clipnodes;
	unsigned int verts;
	unsigned int faces;
	unsigned int textures;
	unsigned int markSurfs;
	unsigned int surfEdges;
	unsigned int edges;
	unsigned int models;
	unsigned int lightdata;
	unsigned int visdata;

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
	bool* nodes;
	bool* clipnodes;
	bool* leaves;
	bool* planes;
	bool* verts;
	bool* texInfo;
	bool* faces;
	bool* textures;
	bool* markSurfs;
	bool* surfEdges;
	bool* edges;

	STRUCTCOUNT count; // size of each array
	STRUCTCOUNT sum;

	int modelIdx;

	STRUCTUSAGE(Bsp* map);
	~STRUCTUSAGE();

	void compute_sum();
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
