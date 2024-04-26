#pragma once
#include "util.h"
#include "Bsp.h"

struct MergeResult {
	Bsp* map;

	// merge failed if map is null, and below are suggested fixes
	string fpath;
	vec3 moveFixes;
	vec3 moveFixes2;
	bool overflow;
};

// bounding box for a map, used for arranging maps for merging
struct MAPBLOCK
{
	vec3 mins, maxs, size, offset;
	Bsp* map;
	string merge_name;

	bool intersects(MAPBLOCK& other) {
		return (mins.x <= other.maxs.x && maxs.x >= other.mins.x) &&
			(mins.y <= other.maxs.y && maxs.y >= other.mins.y) &&
			(mins.z <= other.maxs.z && maxs.z >= other.mins.z);
	}

	void suggest_intersection_fix(MAPBLOCK& other, MergeResult& result) {
		float xdelta_neg = other.maxs.x - mins.x;
		float xdelta_pos = maxs.x - other.mins.x;
		float ydelta_neg = other.maxs.y - mins.y;
		float ydelta_pos = maxs.y - other.mins.y;
		float zdelta_neg = other.maxs.z - mins.z;
		float zdelta_pos = maxs.z - other.mins.z;

		int xdelta = xdelta_neg < xdelta_pos ? ceilf(xdelta_neg + 1.5f) : -ceilf(xdelta_pos + 1.5f);
		int ydelta = ydelta_neg < ydelta_pos ? ceilf(ydelta_neg + 1.5f) : -ceilf(ydelta_pos + 1.5f);
		int zdelta = zdelta_neg < zdelta_pos ? ceilf(zdelta_neg + 1.5f) : -ceilf(zdelta_pos + 1.5f);

		result.moveFixes = vec3(ceilf(xdelta_neg + 1.5f), ceilf(ydelta_neg + 1.5f), ceilf(zdelta_neg + 1.5f));
		result.moveFixes2 = vec3(-ceilf(xdelta_pos + 1.5f), -ceilf(ydelta_pos + 1.5f), -ceilf(zdelta_pos + 1.5f));
	}
};

class BspMerger {
public:
	BspMerger();

	// merges all maps into one
	// noripent - don't change any entity logic
	// noscript - don't add support for the bspguy map script (worse performance + buggy, but simpler)
	// nomove - abort the merge if the maps overlap
	MergeResult merge(vector<Bsp*> maps, vec3 gap, string output_name, bool noripent, bool noscript, bool nomove);

private:
	int merge_ops = 0;

	// wrapper around BSP data merging for nicer console output
	void merge(MAPBLOCK& dst, MAPBLOCK& src, string resultName);

	// merge BSP data
	bool merge(Bsp& mapA, Bsp& mapB);

	vector<vector<vector<MAPBLOCK>>> separate(vector<Bsp*>& maps, vec3 gap, bool nomove, MergeResult& result);

	// for maps in a series:
	// - changelevels should be replaced with teleports or respawn triggers
	// - monsters should spawn only when the current map is active
	// - entities might need map name prefixes
	// - entities in previous levels should be cleaned up
	void update_map_series_entity_logic(Bsp* mergedMap, vector<MAPBLOCK>& sourceMaps, vector<Bsp*>& mapOrder, string output_name, string firstMapName, bool noscript);

	// renames any entity that shares a name with an entity in another map
	int force_unique_ent_names_per_map(Bsp* mergedMap);

	BSPPLANE separate(Bsp& mapA, Bsp& mapB);

	void merge_ents(Bsp& mapA, Bsp& mapB);
	void merge_planes(Bsp& mapA, Bsp& mapB);
	void merge_textures(Bsp& mapA, Bsp& mapB);
	void merge_vertices(Bsp& mapA, Bsp& mapB);
	void merge_texinfo(Bsp& mapA, Bsp& mapB);
	void merge_faces(Bsp& mapA, Bsp& mapB);
	void merge_leaves(Bsp& mapA, Bsp& mapB);
	void merge_marksurfs(Bsp& mapA, Bsp& mapB);
	void merge_edges(Bsp& mapA, Bsp& mapB);
	void merge_surfedges(Bsp& mapA, Bsp& mapB);
	void merge_nodes(Bsp& mapA, Bsp& mapB);
	void merge_clipnodes(Bsp& mapA, Bsp& mapB);
	void merge_models(Bsp& mapA, Bsp& mapB);
	void merge_vis(Bsp& mapA, Bsp& mapB);
	void merge_lighting(Bsp& mapA, Bsp& mapB);

	void create_merge_headnodes(Bsp& mapA, Bsp& mapB, BSPPLANE separationPlane);


	// remapped structure indexes for mapB when merging
	vector<int> texRemap;
	vector<int> texInfoRemap;
	vector<int> planeRemap;
	vector<int> leavesRemap;

	// remapped leaf indexes for mapA's submodel leaves
	vector<int> modelLeafRemap;

	int thisLeafCount;
	int otherLeafCount;
	int thisFaceCount;
	int thisWorldFaceCount;
	int otherFaceCount;
	int thisNodeCount;
	int thisClipnodeCount;
	int thisWorldLeafCount; // excludes solid leaf 0
	int otherWorldLeafCount; // excluding solid leaf 0
	int thisSurfEdgeCount;
	int thisMarkSurfCount;
	int thisEdgeCount;
	int thisVertCount;
};