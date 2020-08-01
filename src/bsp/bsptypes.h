#pragma once
#include "types.h"
#include "bsplimits.h"

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
	SORT_FACES,
	SORT_MODES
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

struct BSPMIPTEX
{
	char szName[MAXTEXTURENAME];  // Name of texture
	uint32_t nWidth, nHeight;		  // Extends of the texture
	uint32_t nOffsets[MIPLEVELS];	  // Offsets to texture mipmaps, relative to the start of this structure
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

	bool isEmpty();
};

struct BSPEDGE {
	uint16_t iVertex[2]; // Indices into vertex array

	BSPEDGE();
	BSPEDGE(uint16_t v1, uint16_t v2);
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
