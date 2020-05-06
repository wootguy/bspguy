#include <chrono>
#include <ctime> 
#include "Wad.h"
#include "Entity.h"
#include "bsplimits.h"
#include "rad.h"

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
	int layers; // for when multiple lights hit the same face (nStyles != 0)
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

	int mismatchCount = 0;
	
	vector<Entity*> ents;

	Bsp();
	Bsp(std::string fname);
	~Bsp();

	bool move(vec3 offset);
	bool merge(Bsp& other);
	void write(string path);

	void print_info();
	void print_bsp();
	void recurse_node(int16_t node, int depth);
	int32_t pointContents(int iNode, vec3 p);

	void dump_lightmap(int faceIdx, string outputPath);
	void dump_lightmap_atlas(string outputPath);

	void write_csg_outputs(string path);

	void get_bounding_box(vec3& mins, vec3& maxs);

private:
	bool load_lumps(string fname);
	void load_ents();
	void merge_ents(Bsp& other);
	void merge_planes(Bsp& other);
	void merge_textures(Bsp& other);
	void merge_vertices(Bsp& other);
	void merge_texinfo(Bsp& other);
	void merge_faces(Bsp& other);
	void merge_leaves(Bsp& other);
	void merge_marksurfs(Bsp& other);
	void merge_edges(Bsp& other);
	void merge_surfedges(Bsp& other);
	void merge_nodes(Bsp& other);
	void merge_clipnodes(Bsp& other);
	void merge_models(Bsp& other);
	void merge_vis(Bsp& other);
	void merge_lighting(Bsp& other);

	// lightmaps that are resized due to precision errors should not be stretched to fit the new canvas.
	// Instead, the texture should be shifted around, depending on which parts of the canvas is "lit" according
	// to the qrad code. Shifts apply to one or both of the lightmaps, depending on which dimension is bigger.
	void get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY);

	void decompress_vis_lump(BSPLEAF* leafLump, byte* visLump, byte* output,
		int iterationLeaves, int visDataLeafCount, int newNumLeaves,
		int shiftOffsetBit, int shiftAmount);

	void shiftVis(uint64* vis, int len, int offsetLeaf, int shift);

	// Finds an axis-aligned hyperplane that separates the BSPs and
	// adds the plane and new root node to the bsp.
	// returns false if maps overlap and can't be separated.
	BSPPLANE separate(Bsp& other);

	// creates new headnodes from the plane that separates the two maps
	// Must be called after planes are merged but before nodes/clipnodes.
	void create_merge_headnodes(Bsp& other, BSPPLANE separationPlane);

	void print_leaf(BSPLEAF leaf);
	void print_node(BSPNODE node);
	void print_stat(string name, uint val, uint max, bool isMem);
	void print_merge_progress(); // also increments progress counter

	void write_csg_polys(int16_t nodeIdx, FILE* fout, int flipPlaneSkip, bool debug);

	void update_ent_lump();

	// remapped structure indexes for the other bsp file when merging
	vector<int> texRemap;
	vector<int> texInfoRemap;
	vector<int> planeRemap;
	vector<int> leavesRemap;

	// remapped leaf indexes for this map's submodel leaves
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