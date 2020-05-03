#pragma once

#include "util.h"
#include "mathlib.h"
#include "bsplimits.h"



#define MAX_SINGLEMAP ((MAX_SURFACE_EXTENT+1)*(MAX_SURFACE_EXTENT+1))
#define MAX_SURFACE_EXTENT  16 // if lightmap extent exceeds 16, the map will not be able to load in 'Software' renderer and HLDS.
#define ALLSTYLES 64 // HL limit. //--vluzacn
#define TEXTURE_STEP        16 // this constant was previously defined in lightmap.cpp. --vluzacn
#define TEX_SPECIAL     1    // sky or slime or null, no lightmap or 256 subdivision
#define NUM_AMBIENTS            4                  // automatic ambient sounds
#define HUNT_WALL_EPSILON (3 * ON_EPSILON) // place sample at least this distance away from any wall //--vluzacn
#define DEFAULT_EDGE_WIDTH 0.8
#define DEFAULT_SMOOTHING_VALUE     50.0
#define DEFAULT_SMOOTHING2_VALUE	-1.0

// DEFAULT_HUNT_OFFSET is how many units in front of the plane to place the samples
// Unit of '1' causes the 1 unit crate trick to cause extra shadows
#define DEFAULT_HUNT_OFFSET 0.5

#define Error printf
#define Log printf
#define assume(exp, message) {if (!(exp)) {Log("\n***** ERROR *****\nAssume '%s' failed\n at %s:%d\n %s\n\n", #exp, __FILE__, __LINE__, message);  exit(-1); }}
#define hlassert(exp)
#define hlassume(exp, message) {if (!(exp)) {Log("\n***** ERROR *****\nAssume '%s' failed\n at %s:%d\n %s\n\n", #exp, __FILE__, __LINE__, #message);  exit(-1); }}

typedef float vec_t;
typedef vec_t   vec3_t[3];                                 // x,y,z

typedef struct
{
	vec_t v[4][3];
}
matrix_t;

class Winding;

typedef enum
{
	LightOutside,                                          // Not lit
	LightShifted,                                          // used HuntForWorld on 100% dark face
	LightShiftedInside,                                    // moved to neighbhor on 2nd cleanup pass
	LightNormal,                                           // Normally lit with no movement
	LightPulledInside,                                     // Pulled inside by bleed code adjustments
	LightSimpleNudge,                                      // A simple nudge 1/3 or 2/3 towards center along S or T axist
}
light_flag_t;

typedef enum
{
	eModelLightmodeNull = 0,
	eModelLightmodeOpaque = 0x02,
	eModelLightmodeNonsolid = 0x08, // for opaque entities with {texture
}
eModelLightmodes;

typedef struct
{
	float           normal[3];
	float           dist;
	planetypes      type;                                  // PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
}
dplane_t;

typedef struct
{
	float           point[3];
}
dvertex_t;

typedef struct
{
	unsigned short  v[2];                                  // vertex numbers
}
dedge_t;

typedef struct
{
	bool valid;
	bool nudged;
	vec_t best_s; // FindNearestPosition will return this value
	vec_t best_t;
	vec3_t pos; // with DEFAULT_HUNT_OFFSET
}
position_t;

typedef struct
{
	bool valid;
	int facenum;
	vec3_t face_offset;
	vec3_t face_centroid;
	matrix_t worldtotex;
	matrix_t textoworld;
	Winding* facewinding;
	dplane_t faceplane;
	Winding* facewindingwithoffset;
	dplane_t faceplanewithoffset;
	Winding* texwinding;
	dplane_t texplane; // (0, 0, 1, 0) or (0, 0, -1, 0)
	vec3_t texcentroid;
	vec3_t start; // s_start, t_start, 0
	vec3_t step; // s_step, t_step, 0
	int w; // number of s
	int h; // number of t
	position_t* grid; // [h][w]
}
positionmap_t;

typedef struct texinfo_s
{
	float           vecs[2][4];                            // [s/t][xyz offset]
	int             miptex;
	int             flags;
}
texinfo_t;

typedef struct
{
	unsigned short	planenum;
	short           side;

	int             firstedge;                             // we must support > 64k edges
	short           numedges;
	short           texinfo;

	// lighting info
	byte            styles[MAXLIGHTMAPS];
	int             lightofs;                              // start of [numstyles*surfsize] samples
}
dface_t;

typedef struct facelist_s
{
	dface_t* face;
	facelist_s* next;
} facelist_t;

typedef struct
{
	dface_t* faces[2];
	vec3_t          interface_normal; // HLRAD_GetPhongNormal_VL: this field must be set when smooth==true
	vec3_t			vertex_normal[2];
	vec_t           cos_normals_angle; // HLRAD_GetPhongNormal_VL: this field must be set when smooth==true
	bool            coplanar;
	bool			smooth;
	facelist_t* vertex_facelist[2]; //possible smooth faces, not include faces[0] and faces[1]
	matrix_t		textotex[2]; // how we translate texture coordinates from one face to the other face
} edgeshare_t;

typedef struct
{
	vec_t* light;
	vec_t           facedist;
	vec3_t          facenormal;
	bool			translucent_b;
	vec3_t			translucent_v;
	int				miptex;

	int             numsurfpt;
	vec3_t          surfpt[MAX_SINGLEMAP];
	vec3_t* surfpt_position; //[MAX_SINGLEMAP] // surfpt_position[] are valid positions for light tracing, while surfpt[] are positions for getting phong normal and doing patch interpolation
	int* surfpt_surface; //[MAX_SINGLEMAP] // the face that owns this position
	bool			surfpt_lightoutside[MAX_SINGLEMAP];

	vec3_t          texorg;
	vec3_t          worldtotex[2];                         // s = (world - texorg) . worldtotex[0]
	vec3_t          textoworld[2];                         // world = texorg + s * textoworld[0]
	vec3_t			texnormal;

	vec_t           exactmins[2], exactmaxs[2];

	int             texmins[2], texsize[2];
	int             lightstyles[256];
	int             surfnum;
	dface_t* face;
	int				lmcache_density; // shared by both s and t direction
	int				lmcache_offset; // shared by both s and t direction
	int				lmcache_side;
	vec3_t(*lmcache)[ALLSTYLES]; // lm: short for lightmap // don't forget to free!
	vec3_t* lmcache_normal; // record the phong normals
	int* lmcache_wallflags; // wallflag_t
	int				lmcachewidth;
	int				lmcacheheight;
}
lightinfo_t;

typedef struct
{
	int edgenum; // g_dedges index
	int edgeside;
	int nextfacenum; // where to grow
	bool tried;

	vec3_t point1; // start point
	vec3_t point2; // end point
	vec3_t direction; // normalized; from point1 to point2

	bool noseam;
	vec_t distance; // distance from origin
	vec_t distancereduction;
	vec_t flippedangle;

	vec_t ratio; // if ratio != 1, seam is unavoidable
	matrix_t prevtonext;
	matrix_t nexttoprev;
}
samplefragedge_t;

typedef struct
{
	dplane_t planes[4];
}
samplefragrect_t;

typedef struct samplefrag_s
{
	samplefrag_s* next; // since this is a node in a list
	samplefrag_s* parentfrag; // where it grew from
	samplefragedge_t* parentedge;
	int facenum; // facenum

	vec_t flippedangle; // copied from parent edge
	bool noseam; // copied from parent edge

	matrix_t coordtomycoord; // v[2][2] > 0, v[2][0] = v[2][1] = v[0][2] = v[1][2] = 0.0
	matrix_t mycoordtocoord;

	vec3_t origin; // original s,t
	vec3_t myorigin; // relative to the texture coordinate on that face
	samplefragrect_t rect; // original rectangle that forms the boundary
	samplefragrect_t myrect; // relative to the texture coordinate on that face

	Winding* winding; // a fragment of the original rectangle in the texture coordinate plane; windings of different frags should not overlap
	dplane_t windingplane; // normal = (0,0,1) or (0,0,-1); if this normal is wrong, point_in_winding() will never return true
	Winding* mywinding; // relative to the texture coordinate on that face
	dplane_t mywindingplane;

	int numedges; // # of candicates for the next growth
	samplefragedge_t* edges; // candicates for the next growth
}
samplefrag_t;

typedef struct
{
	int maxsize;
	int size;
	samplefrag_t* head;
}
samplefraginfo_t;

// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
typedef struct
{
	int             contents;
	int             visofs;                                // -1 = no visibility info

	short           mins[3];                               // for frustum culling
	short           maxs[3];

	unsigned short  firstmarksurface;
	unsigned short  nummarksurfaces;

	byte            ambient_level[NUM_AMBIENTS];
}
dleaf_t;

typedef struct
{
	vec3_t mins, maxs;
	int headnode;
} opaquemodel_t;

typedef struct opaquenode_s
{
	planetypes type;
	vec3_t normal;
	vec_t dist;
	int children[2];
	int firstface;
	int numfaces;
} opaquenode_t;

typedef struct
{
	Winding* winding;
	dplane_t plane;
	int numedges;
	dplane_t* edges;
	int texinfo;
	bool tex_alphatest;
	vec_t tex_vecs[2][4];
	int tex_width;
	int tex_height;
	const byte* tex_canvas;
} opaqueface_t;

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct
{
	int             planenum;
	short           children[2];                           // negative numbers are -(leafs+1), not nodes
	short           mins[3];                               // for sphere culling
	short           maxs[3];
	unsigned short  firstface;
	unsigned short  numfaces;                              // counting both sides
}
dnode_t;

typedef struct tnode_s
{
	planetypes      type;
	vec3_t          normal;
	float           dist;
	int             children[2];
	int             pad;
} tnode_t;

typedef struct
{
	char name[16]; // not always same with the name in texdata
	int width, height;
	byte* canvas; //[height][width]
	byte palette[256][3];
	vec3_t reflectivity;
} radtexture_t;

typedef struct
{
	float           mins[3], maxs[3];
	float           origin[3];
	int             headnode[MAX_MAP_HULLS];
	int             visleafs;                              // not including the solid leaf 0
	int             firstface, numfaces;
}
dmodel_t;

typedef struct
{
	int entitynum;
	int modelnum;
	vec3_t origin;

	vec3_t transparency_scale;
	bool transparency;
	int style; // -1 = no style; transparency must be false if style >= 0
	// style0 and same style will change to this style, other styles will be blocked.
	bool block; // this entity can't be seen inside, so all lightmap sample should move outside.

} opaqueList_t;

typedef struct
{
	int numclipplanes;
	dplane_t* clipplanes;
}
intersecttest_t;

typedef struct
{
	bool leftShift, topShift;

} lightmap_shift_t;

extern dface_t g_dfaces[MAX_MAP_FACES];
extern vec3_t g_face_offset[MAX_MAP_FACES];
extern dplane_t backplanes[MAX_MAP_PLANES];
extern dplane_t g_dplanes[MAX_MAP_PLANES];
extern texinfo_t g_texinfo[MAX_MAP_TEXINFOS];
extern int g_dsurfedges[MAX_MAP_SURFEDGES];
extern edgeshare_t g_edgeshare[MAX_MAP_EDGES];
extern dedge_t g_dedges[MAX_MAP_EDGES];
extern dvertex_t g_dvertexes[MAX_MAP_VERTS];
extern positionmap_t g_face_positions[MAX_MAP_FACES];
extern eModelLightmodes g_face_lightmode[MAX_MAP_FACES];
extern vec3_t g_face_centroids[MAX_MAP_EDGES];
extern dleaf_t g_dleafs[MAX_MAP_LEAVES];

extern opaquemodel_t* opaquemodels;
extern opaquenode_t* opaquenodes;
extern opaqueface_t* opaquefaces;
extern opaqueList_t* g_opaque_face_list;
extern unsigned g_opaque_face_count; // opaque entity count //HLRAD_OPAQUE_NODE

extern dnode_t g_dnodes[MAX_MAP_NODES];
extern tnode_t tnodes[MAX_MAP_NODES];
extern dmodel_t g_dmodels[MAX_MAP_MODELS];

extern int g_nummodels;
extern int g_numnodes;
extern int g_numfaces;
extern radtexture_t* g_textures;

extern float g_blur;

class Bsp;

void qrad_init_globals(Bsp* bsp);
void qrad_cleanup_globals();
lightmap_shift_t qrad_get_lightmap_shift(Bsp* bsp, int faceIdx);

const dplane_t* getPlaneFromFace(const dface_t* const face);
void CreateOpaqueNodes();
void PairEdges();
void CalcFaceCentroid(const int fn, Winding* w);
void FindFacePositions(int facenum);

void CalcFaceExtents(lightinfo_t* l);
void CalcFaceVectors(lightinfo_t* l);
void CalcPoints(lightinfo_t* l, light_flag_t* LuxelFlags);