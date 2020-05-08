#include "util.h"
#include "Bsp.h"

// bounding box for a map, used for arranging maps for merging
struct MAPBLOCK
{
	vec3 mins, maxs, size, offset;
	Bsp* map;
	string merge_name;

	bool intersects(MAPBLOCK& other) {
		return (mins.x <= other.maxs.x && maxs.x >= other.mins.x) &&
			(mins.y <= other.maxs.y && maxs.y >= other.mins.y) &&
			(mins.z <= other.maxs.z && maxs.y >= other.mins.z);
	}
};

class BspMerger {
public:
	BspMerger();

	// merges all maps into one
	Bsp* merge(vector<Bsp*> maps, vec3 gap);

private:
	int merge_ops = 0;

	// wrapper around BSP data merging for nicer console output
	void merge(MAPBLOCK& dst, MAPBLOCK& src, string resultName);

	// merge BSP data
	bool merge(Bsp& mapA, Bsp& mapB);

	vector<vector<vector<MAPBLOCK>>> separate(vector<Bsp*>& maps, vec3 gap);

	BSPPLANE separate(Bsp& mapA, Bsp& mapB);

	int getMipTexDataSize(int width, int height);

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

	void print_merge_progress();

	bool shiftVis(uint64* vis, int len, int offsetLeaf, int shift);

	// decompress this map's vis data into arrays of bits where each bit indicates if a leaf is visible or not
	// iterationLeaves = number of leaves to decompress vis for
	// visDataLeafCount = total leaves in this map (exluding the shared solid leaf 0)
	// newNumLeaves = total leaves that will be in the map after merging is finished (again, excluding solid leaf 0)
	void decompress_vis_lump(BSPLEAF* leafLump, byte* visLump, byte* output,
		int iterationLeaves, int visDataLeafCount, int newNumLeaves,
		int shiftOffsetBit, int shiftAmount);

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
	int thisNodeCount;
	int thisClipnodeCount;
	int thisWorldLeafCount; // excludes solid leaf 0
	int otherWorldLeafCount; // excluding solid leaf 0
	int thisSurfEdgeCount;
	int thisMarkSurfCount;
	int thisEdgeCount;
	int thisVertCount;

	chrono::system_clock::time_point last_progress;
	char* progress_title;
	char* last_progress_title;
	int progress;
	int progress_total;
};