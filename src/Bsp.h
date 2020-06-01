#pragma once
#include <chrono>
#include <ctime> 
#include "Wad.h"
#include "Entity.h"
#include "bsplimits.h"
#include "rad.h"
#include <string.h>
#include "remap.h"
#include <set>

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

// maximum x/y hull extent a monster can have before it starts using hull 2
#define MAX_HULL1_EXTENT_MONSTER 18

// maximum x/y hull dimension a pushable can have before it starts using hull 2
#define MAX_HULL1_SIZE_PUSHABLE 34.0f

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

	// returns true if the plane was flipped
	bool update(vec3 newNormal, float fdist);
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

	BSPEDGE() {}
	BSPEDGE(uint16_t v1, uint16_t v2) { iVertex[0] = v1; iVertex[1] = v2; }
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

struct membuf : std::streambuf
{
	membuf(char* begin, int len) {
		this->setg(begin, begin, begin + len);
	}
};

struct ScalablePlane {
	int planeIdx;
	vec3 oldNormal;
	float oldDist;
	vec3 oldOrigin, origin;
	vec3 v1, v2;
};

struct ScalableTexinfo {
	int texinfoIdx;
	vec3 oldS, oldT;
	float oldShiftS, oldShiftT;
	int planeIdx;
	int faceIdx;
};

class Bsp
{
public:
	string path;
	string name;
	BSPHEADER header;
	byte ** lumps;
	bool valid;

	BSPPLANE* planes;
	BSPTEXTUREINFO* texinfos;
	byte* textures;
	BSPLEAF* leaves;
	BSPMODEL* models;
	BSPNODE* nodes;
	BSPCLIPNODE* clipnodes;
	BSPFACE* faces;
	vec3* verts;
	byte* lightdata;
	int32_t* surfedges;
	BSPEDGE* edges;
	uint16* marksurfs;
	byte* visdata;

	int planeCount;
	int texinfoCount;
	int leafCount;
	int modelCount;
	int nodeCount;
	int vertCount;
	int faceCount;
	int clipnodeCount;
	int marksurfCount;
	int surfedgeCount;
	int edgeCount;
	int textureCount;
	int lightDataLength;
	int visDataLength;
	
	vector<Entity*> ents;

	Bsp();
	Bsp(std::string fname);
	~Bsp();

	bool move(vec3 offset);
	void move_texinfo(int idx, vec3 offset);
	void write(string path);

	void print_info(bool perModelStats, int perModelLimit, int sortMode);
	void print_model_hull(int modelIdx, int hull);
	void print_clipnode_tree(int iNode, int depth);
	void recurse_node(int16_t node, int depth);
	int32_t pointContents(int iNode, vec3 p);
	bool isTouchingLeaf(int iNode, vec3 p, int leafIdx);

	// strips a collision hull from the given model index
	// and redirects to the given hull, if redirect>0
	void delete_hull(int hull_number, int modelIdx, int redirect);

	// strips a collision hull from all models
	// and redirects to the given hull, if redirect>0
	void delete_hull(int hull_number, int redirect);

	void dump_lightmap(int faceIdx, string outputPath);
	void dump_lightmap_atlas(string outputPath);

	void write_csg_outputs(string path);

	// get the bounding box for the world
	void get_bounding_box(vec3& mins, vec3& maxs);

	// get the bounding box for all vertexes in a BSP tree
	void get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs);

	// get all verts used by this model
	// TODO: split any verts shared with other models!
	vec3** getModelVerts(int modelIdx, int& numVerts);

	// gets verts formed by plane intersections with the nodes in this model
	vector<vec3> getModelPlaneIntersectVerts(int modelIdx);
	void getNodePlanes(int iNode, vector<BSPPLANE>& planeStack, vector<vector<BSPPLANE>>& nodePlanes);

	// this a cheat to recalculate plane normals after scaling a solid. Really I should get the plane
	// intersection code working for nonconvex solids, but that's looking like a ton of work.
	// Scaling/stretching really only needs 3 verts _anywhere_ on the plane to calculate new normals/origins.
	vector<ScalablePlane> getScalablePlanes(int modelIdx);
	void getScalableNodePlanes(int iNode, vector<ScalablePlane>& nodePlanes, set<int>& visited);
	void getScalableClipnodePlanes(int iNode, vector<ScalablePlane>& nodePlanes, set<int>& visited);
	ScalablePlane getScalablePlane(int planeIdx);
	vector<ScalableTexinfo> getScalableTexinfos(int modelIdx); // for scaling
	int addTextureInfo(BSPTEXTUREINFO& copy);

	// fixes up the model planes/nodes after vertex posisions have been modified
	// returns false if the model has non-planar faces
	// TODO: split any planes shared with other models
	bool vertex_manipulation_sync(int modelIdx);

	void load_ents();

	// call this after editing ents
	void update_ent_lump();

	vec3 get_model_center(int modelIdx);

	bool isValid();

	// delete structures not used by the map (needed after deleting models/hulls)
	STRUCTCOUNT remove_unused_model_structures();
	void delete_model(int modelIdx);

	// conditionally deletes hulls for entities that aren't using them
	STRUCTCOUNT delete_unused_hulls();

	// returns true if the map has eny entities that make use of hull 2
	bool has_hull2_ents();
	
	// check for bad indexes
	bool validate();

	// creates a solid cube
	int create_solid(vec3 mins, vec3 maxs, int textureIdx);

	int create_leaf(int contents);
	void create_node_box(vec3 mins, vec3 maxs, BSPMODEL* targetModel, int textureIdx);
	void create_clipnode_box(vec3 mins, vec3 maxs, BSPMODEL* targetModel, int targetHull = 0, bool skipEmpty = false);

	// copies a model from the sourceMap into this one
	void add_model(Bsp* sourceMap, int modelIdx);

	void replace_lump(int lumpIdx, void* newData, int newLength);

	bool is_invisible_solid(Entity* ent);

	// replace a model's clipnode hull with a axis-aligned bounding box
	void simplify_model_collision(int modelIdx, int hullIdx);

	// for use after scaling a model
	void regenerate_clipnodes(int modelIdx);
	int16 regenerate_clipnodes(int iNode, int hullIdx);
	int create_clipnode();
	int create_plane();

private:
	int remove_unused_lightmaps(bool* usedFaces);
	int remove_unused_visdata(bool* usedLeaves, BSPLEAF* oldLeaves, int oldLeafCount); // called after removing unused leaves
	int remove_unused_textures(bool* usedTextures, int* remappedIndexes);
	int remove_unused_structs(int lumpIdx, bool* usedStructs, int* remappedIndexes);

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
	void print_model_stat(STRUCTUSAGE* modelInfo, uint val, uint max, bool isMem);

	string get_model_usage(int modelIdx);
	vector<Entity*> get_model_ents(int modelIdx);

	void write_csg_polys(int16_t nodeIdx, FILE* fout, int flipPlaneSkip, bool debug);	

	// marks all structures that this model uses
	void mark_model_structures(int modelIdx, STRUCTUSAGE* STRUCTUSAGE);
	void mark_face_structures(int iFace, STRUCTUSAGE* usage);
	void mark_node_structures(int iNode, STRUCTUSAGE* usage);
	void mark_clipnode_structures(int iNode, STRUCTUSAGE* usage);

	// remaps structure indexes to new locations
	void remap_face_structures(int faceIdx, STRUCTREMAP* remap);
	void remap_model_structures(int modelIdx, STRUCTREMAP* remap);
	void remap_node_structures(int iNode, STRUCTREMAP* remap);
	void remap_clipnode_structures(int iNode, STRUCTREMAP* remap);

	void update_lump_pointers();
};
