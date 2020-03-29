#include "Wad.h"
#include "Entity.h"

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

struct BSPLEAF
{
	int32_t nContents;                         // Contents enumeration
	int32_t nVisOffset;                        // Offset into the visibility lump
	int16_t nMins[3], nMaxs[3];                // Defines bounding box
	uint16_t iFirstMarkSurface, nMarkSurfaces; // Index and count into marksurfaces array
	uint8_t nAmbientLevels[4];                 // Ambient sound levels
};

struct BSPEDGE {
	uint16_t iVertex[2]; // Indices into vertex array
};

struct BSPTEXDATA
{
	int32_t numTex;
	int32_t* offset;
	int32_t* len;
	WADTEX** tex;
};

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
	
	vector<Entity*> ents;

	Bsp();
	Bsp(std::string fname);
	~Bsp();

	void merge(Bsp& other);
	void write(string path);

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

	// remapped structure locations for the other bsp file when merging
	vector<int> texRemap;
	vector<int> texInfoRemap;
	vector<int> planeRemap;
	vector<int> edgeRemap;
	vector<int> surfEdgeRemap;
	vector<int> markSurfRemap;
	vector<int> vertRemap;
	vector<int> leavesRemap;
	vector<int> facesRemap;
};