#include <chrono>
#include <ctime> 
#include "Wad.h"
#include "Entity.h"
#include "bsplimits.h"
#include "rad.h"
#include <string.h>

#define BSP_MODEL_BYTES 64 // size of a BSP model in bytes

#define LUMP_ENTITIES      0
#define LUMP_PLANES        1
#define LUMP_TEXTURES      2
#define LUMP_VERTICES      3
#define LUMP_VISIBILITY    4
#define LUMP_NODES         5
#define LUMP_TEXINFO       6
#define LUMP_FACES         7
#define LUMP_LIGHTING      8
#define LUMP_CLIPNODES     9
#define LUMP_LEAVES       10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES        12
#define LUMP_SURFEDGES    13
#define LUMP_MODELS       14
#define HEADER_LUMPS      15

#define CONTENTS_EMPTY        -1
#define CONTENTS_SOLID        -2
#define CONTENTS_WATER        -3
#define CONTENTS_SLIME        -4
#define CONTENTS_LAVA         -5
#define CONTENTS_SKY          -6
#define CONTENTS_ORIGIN       -7
#define CONTENTS_CLIP         -8
#define CONTENTS_CURRENT_0    -9
#define CONTENTS_CURRENT_90   -10
#define CONTENTS_CURRENT_180  -11
#define CONTENTS_CURRENT_270  -12
#define CONTENTS_CURRENT_UP   -13
#define CONTENTS_CURRENT_DOWN -14
#define CONTENTS_TRANSLUCENT  -15

#define PLANE_X 0     // Plane is perpendicular to given axis
#define PLANE_Y 1
#define PLANE_Z 2
#define PLANE_ANYX 3  // Non-axial plane is snapped to the nearest
#define PLANE_ANYY 4
#define PLANE_ANYZ 5

static char* g_lump_names[HEADER_LUMPS] = {
	"ENTITIES",
	"PLANES",
	"TEXTURES",
	"VERTICES",
	"VISIBILITY",
	"NODES",
	"TEXINFO",
	"FACES",
	"LIGHTING",
	"CLIPNODES",
	"LEAVES",
	"MARKSURFACES",
	"EDGES",
	"SURFEDGES",
	"MODELS"
};

enum MODEL_SORT_MODES {
	SORT_VERTS,
	SORT_NODES,
	SORT_CLIPNODES,
	SORT_FACES
};

struct BSPLUMP
{
	int nOffset; // File offset to data
	int nLength; // Length of data
};

struct BSPHEADER
{
	int32_t nVersion;           // Must be 30 for a valid HL BSP file
	BSPLUMP lump[HEADER_LUMPS]; // Stores the directory of lumps
};

struct BSPPLANE {
	vec3 vNormal;
	float fDist;
	int32_t nType;
};

typedef struct
{
	BSPPLANE planes[4];
}
samplefragrect_t;

typedef struct samplefrag_s
{
	int facenum; // facenum
	samplefragrect_t rect; // original rectangle that forms the boundary
	Winding* mywinding; // relative to the texture coordinate on that face
}
samplefrag_t;

struct CSGPLANE {
	double normal[3];
	double origin[3];
	double dist;
	int32_t nType;
};

struct BSPTEXTUREINFO {
	vec3 vS;
	float shiftS;
	vec3 vT;
	float shiftT;
	uint32_t iMiptex;
	uint32_t nFlags;
};

struct BSPFACE {
	uint16_t iPlane;          // Plane the face is parallel to
	uint16_t nPlaneSide;      // Set if different normals orientation
	uint32_t iFirstEdge;      // Index of the first surfedge
	uint16_t nEdges;          // Number of consecutive surfedges
	uint16_t iTextureInfo;    // Index of the texture info structure
	uint8_t nStyles[4];       // Specify lighting styles
	uint32_t nLightmapOffset; // Offsets into the raw lightmap data
};

// for a single face
struct LIGHTMAP
{
	int width, height;
	int layers; // for when multiple lights hit the same face (nStyles[0-3] != 255)
	light_flag_t luxelFlags[MAX_SINGLEMAP];
};

struct BSPLEAF
{
	int32_t nContents;                         // Contents enumeration
	int32_t nVisOffset;                        // Offset into the visibility lump
	int16_t nMins[3], nMaxs[3];                // Defines bounding box
	uint16_t iFirstMarkSurface, nMarkSurfaces; // Index and count into marksurfaces array
	uint8_t nAmbientLevels[4];                 // Ambient sound levels

	bool isEmpty() {
		BSPLEAF emptyLeaf;
		memset(&emptyLeaf, 0, sizeof(BSPLEAF));
		emptyLeaf.nContents = CONTENTS_SOLID;

		return memcmp(&emptyLeaf, this, sizeof(BSPLEAF)) == 0;
	}
};

struct BSPEDGE {
	uint16_t iVertex[2]; // Indices into vertex array
};

struct BSPMODEL
{
	vec3 nMins;
	vec3 nMaxs;
	vec3 vOrigin;                  // Coordinates to move the // coordinate system
	int32_t iHeadnodes[MAX_MAP_HULLS]; // Index into nodes array
	int32_t nVisLeafs;                 // ???
	int32_t iFirstFace, nFaces;        // Index and count into faces
};

struct BSPNODE
{
	uint32_t iPlane;            // Index into Planes lump
	int16_t iChildren[2];       // If > 0, then indices into Nodes // otherwise bitwise inverse indices into Leafs
	int16_t nMins[3], nMaxs[3]; // Defines bounding box
	uint16_t firstFace, nFaces; // Index and count into Faces
};

struct BSPCLIPNODE
{
	int32_t iPlane;       // Index into planes
	int16_t iChildren[2]; // negative numbers are contents
};

struct BSPTEXDATA
{
	int32_t numTex;
	int32_t* offset;
	int32_t* len;
	WADTEX** tex;
};

struct SURFACEINFO
{
	int extents[2];
};

class Bsp;
struct MOVEINFO;
struct REMAPINFO;

struct membuf : std::streambuf
{
	membuf(char* begin, int len) {
		this->setg(begin, begin, begin + len);
	}
};

class Bsp
{
public:
	string path;
	string name;
	BSPHEADER header;
	byte ** lumps;
	bool valid;
	
	vector<Entity*> ents;

	Bsp();
	Bsp(std::string fname);
	~Bsp();

	bool move(vec3 offset);
	void write(string path);

	void print_info(bool perModelStats, int perModelLimit, int sortMode);
	void print_model_hull(int modelIdx, int hull);
	void print_clipnode_tree(int iNode, int depth);
	void recurse_node(int16_t node, int depth);
	int32_t pointContents(int iNode, vec3 p);

	// strips a collision hull from the given model index
	// ignoreSharedIfSameHull = don't preserve clipnodes shared with other models if hull matches hull_number
	int strip_clipping_hull(int hull_number, int modelIdx, bool ignoreSharedIfSameHull);

	// strips a collision hull from all models and the world
	int strip_clipping_hull(int hull_number);

	void dump_lightmap(int faceIdx, string outputPath);
	void dump_lightmap_atlas(string outputPath);

	void write_csg_outputs(string path);

	void get_bounding_box(vec3& mins, vec3& maxs);

	void load_ents();

	// call this after editing ents
	void update_ent_lump();

	vec3 get_model_center(int modelIdx);

private:

	// for each model, split structures that are shared with models that both have and don't have an origin
	void split_shared_model_structures();

	void resize_lightmaps(LIGHTMAP* oldLightmaps, LIGHTMAP* newLightmaps);

	bool load_lumps(string fname);

	// lightmaps that are resized due to precision errors should not be stretched to fit the new canvas.
	// Instead, the texture should be shifted around, depending on which parts of the canvas is "lit" according
	// to the qrad code. Shifts apply to one or both of the lightmaps, depending on which dimension is bigger.
	void get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY);

	void print_model_bsp(int modelIdx);
	void print_leaf(BSPLEAF leaf);
	void print_contents(int contents);
	void print_node(BSPNODE node);
	void print_stat(string name, uint val, uint max, bool isMem);
	void print_model_stat(MOVEINFO* modelInfo, uint val, uint max, bool isMem);

	string get_model_usage(int modelIdx);

	void write_csg_polys(int16_t nodeIdx, FILE* fout, int flipPlaneSkip, bool debug);	

	// mark clipnodes that are children of this iNode.
	// markList should be big enough to hold every clipnode in the map
	void mark_clipnodes(int iNode, bool* markList);

	// marks all structures that this model uses
	void mark_model_structures(int modelIdx, MOVEINFO* moveInfo);
	void mark_node_structures(int iNode, MOVEINFO* markList);
	void mark_clipnode_structures(int iNode, MOVEINFO* markList);

	// remaps structure indexes to new locations
	void remap_model_structures(int modelIdx, REMAPINFO* remapInfo);
	void remap_node_structures(int iNode, REMAPINFO* remapInfo);
	void remap_clipnode_structures(int iNode, REMAPINFO* remapInfo);

	chrono::system_clock::time_point last_progress;
	char* progress_title;
	char* last_progress_title;
	int progress;
	int progress_total;

	void print_move_progress();
};



// used to mark structures that were moved as a result of moving a model
struct MOVEINFO
{
	bool* nodes;
	bool* clipnodes;
	bool* leaves;
	bool* planes;
	bool* verts;
	bool* texInfo;
	bool* faces;

	int planeCount;
	int texInfoCount;
	int leafCount;
	int nodeCount;
	int clipnodeCount;
	int vertCount;
	int faceCount;

	int planeSum;
	int texInfoSum;
	int leafSum;
	int nodeSum;
	int clipnodeSum;
	int vertSum;
	int faceSum;

	int modelIdx;

	MOVEINFO() {
		modelIdx = 0;
		planeSum = texInfoSum = leafSum = nodeSum = clipnodeSum = vertSum = faceSum = 0;
		planeCount = texInfoCount = leafCount = nodeCount = clipnodeCount = vertCount = faceCount = 0;
		nodes = clipnodes = leaves = planes = verts = texInfo = faces = NULL;
	}

	MOVEINFO(Bsp* map) {
		init(map);
	}

	void init(Bsp* map) {
		planeCount = map->header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
		texInfoCount = map->header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
		leafCount = map->header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
		nodeCount = map->header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
		clipnodeCount = map->header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
		vertCount = map->header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
		faceCount = map->header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

		nodes = new bool[nodeCount];
		clipnodes = new bool[clipnodeCount];
		leaves = new bool[leafCount];
		planes = new bool[planeCount];
		verts = new bool[vertCount];
		texInfo = new bool[texInfoCount];
		faces = new bool[faceCount];

		memset(nodes, 0, nodeCount * sizeof(bool));
		memset(clipnodes, 0, clipnodeCount * sizeof(bool));
		memset(leaves, 0, leafCount * sizeof(bool));
		memset(planes, 0, planeCount * sizeof(bool));
		memset(verts, 0, vertCount * sizeof(bool));
		memset(texInfo, 0, texInfoCount * sizeof(bool));
		memset(faces, 0, faceCount * sizeof(bool));
	}

	void compute_sums() {
		planeSum = texInfoSum = leafSum = nodeSum = clipnodeSum = vertSum = faceSum = 0;
		for (int i = 0; i < planeCount; i++) planeSum += planes[i];
		for (int i = 0; i < texInfoCount; i++) texInfoSum += texInfo[i];
		for (int i = 0; i < leafCount; i++) leafSum += leaves[i];
		for (int i = 0; i < nodeCount; i++) nodeSum += nodes[i];
		for (int i = 0; i < clipnodeCount; i++) clipnodeSum += clipnodes[i];
		for (int i = 0; i < vertCount; i++) vertSum += verts[i];
		for (int i = 0; i < faceCount; i++) faceSum += faces[i];
	}

	~MOVEINFO() {
		delete[] nodes;
		delete[] clipnodes;
		delete[] leaves;
		delete[] planes;
		delete[] verts;
		delete[] texInfo;
		delete[] faces;
	}
};

// used to remap structure indexes to new locations
struct REMAPINFO
{
	int* nodes;
	int* clipnodes;
	int* leaves;
	int* planes;
	int* verts;
	int* texInfo;

	bool* visitedNodes; // don't try to update the same nodes twice
	bool* visitedClipnodes; // don't try to update the same nodes twice

	int planeCount;
	int texInfoCount;
	int leafCount;
	int nodeCount;
	int clipnodeCount;
	int vertCount;

	REMAPINFO(Bsp* map) {
		planeCount = map->header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
		texInfoCount = map->header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
		leafCount = map->header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
		nodeCount = map->header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
		clipnodeCount = map->header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
		vertCount = map->header.lump[LUMP_VERTICES].nLength / sizeof(vec3);

		nodes = new int[nodeCount];
		clipnodes = new int[clipnodeCount];
		leaves = new int[leafCount];
		planes = new int[planeCount];
		verts = new int[vertCount];
		texInfo = new int[texInfoCount];

		visitedNodes = new bool[nodeCount];
		visitedClipnodes = new bool[clipnodeCount];

		memset(nodes, 0, nodeCount * sizeof(int));
		memset(clipnodes, 0, clipnodeCount * sizeof(int));
		memset(leaves, 0, leafCount * sizeof(int));
		memset(planes, 0, planeCount * sizeof(int));
		memset(verts, 0, vertCount * sizeof(int));
		memset(texInfo, 0, texInfoCount * sizeof(int));

		memset(visitedClipnodes, 0, clipnodeCount * sizeof(bool));
		memset(visitedNodes, 0, nodeCount * sizeof(bool));
	}

	~REMAPINFO() {
		delete[] nodes;
		delete[] clipnodes;
		delete[] leaves;
		delete[] planes;
		delete[] verts;
		delete[] texInfo;
		delete[] visitedClipnodes;
		delete[] visitedNodes;
	}
};