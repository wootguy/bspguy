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
#include "bsptypes.h"

struct membuf : std::streambuf
{
	membuf(char* begin, int len) {
		this->setg(begin, begin, begin + len);
	}
};

struct ScalableTexinfo {
	int texinfoIdx;
	vec3 oldS, oldT;
	float oldShiftS, oldShiftT;
	int planeIdx;
	int faceIdx;
};

struct TransformVert {
	vec3 pos;
	vec3* ptr; // face vertex to move with (null for invisible faces)
	vector<int> iPlanes;
	vec3 startPos; // for dragging
	vec3 undoPos; // for undoing invalid solid stuff
	bool selected;
};

struct HullEdge {
	int verts[2]; // index into modelVerts/hullVerts
	int planes[2]; // index into iPlanes
	bool selected;
};

struct Face {
	vector<int> verts; // index into hullVerts
	BSPPLANE plane;
	int planeSide;
	int iTextureInfo;
};

struct Solid {
	vector<Face> faces;

	vector<TransformVert> hullVerts; // control points for hull 0
	vector<HullEdge> hullEdges; // for vertex manipulation (holds indexes into hullVerts)
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

	bool move(vec3 offset, int modelIdx=-1);
	void move_texinfo(int idx, vec3 offset);
	void write(string path);

	void print_info(bool perModelStats, int perModelLimit, int sortMode);
	void print_model_hull(int modelIdx, int hull);
	void print_clipnode_tree(int iNode, int depth);
	void recurse_node(int16_t node, int depth);
	int32_t pointContents(int iNode, vec3 p);

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
	vector<TransformVert> getModelVerts(int modelIdx);

	// gets verts formed by plane intersections with the nodes in this model
	bool getModelPlaneIntersectVerts(int modelIdx, vector<TransformVert>& outVerts);
	bool getModelPlaneIntersectVerts(int modelIdx, const vector<int>& planes, vector<TransformVert>& outVerts);
	void getNodePlanes(int iNode, vector<int>& nodePlanes);
	bool is_convex(int modelIdx);
	bool is_node_hull_convex(int iNode);

	// this a cheat to recalculate plane normals after scaling a solid. Really I should get the plane
	// intersection code working for nonconvex solids, but that's looking like a ton of work.
	// Scaling/stretching really only needs 3 verts _anywhere_ on the plane to calculate new normals/origins.
	vector<ScalableTexinfo> getScalableTexinfos(int modelIdx); // for scaling
	int addTextureInfo(BSPTEXTUREINFO& copy);

	// fixes up the model planes/nodes after vertex posisions have been modified
	// returns false if the model has non-planar faces
	// TODO: split any planes shared with other models
	bool vertex_manipulation_sync(int modelIdx, vector<TransformVert>& hullVerts, bool convexCheckOnly);

	void load_ents();

	// call this after editing ents
	void update_ent_lump();

	vec3 get_model_center(int modelIdx);

	bool isValid(); // check if any lumps are overflowed

	// delete structures not used by the map (needed after deleting models/hulls)
	STRUCTCOUNT remove_unused_model_structures();
	void delete_model(int modelIdx);

	// conditionally deletes hulls for entities that aren't using them
	STRUCTCOUNT delete_unused_hulls(bool noProgress=false);

	// returns true if the map has eny entities that make use of hull 2
	bool has_hull2_ents();
	
	// check for bad indexes
	bool validate();

	// creates a solid cube
	int create_solid(vec3 mins, vec3 maxs, int textureIdx);

	// creates a new solid from the given solid definition (must be convex).
	int create_solid(Solid& solid, int targetModelIdx=-1);

	int create_leaf(int contents);
	void create_node_box(vec3 mins, vec3 maxs, BSPMODEL* targetModel, int textureIdx);
	void create_nodes(Solid& solid, BSPMODEL* targetModel);
	// returns index of the solid node
	int create_clipnode_box(vec3 mins, vec3 maxs, BSPMODEL* targetModel, int targetHull = 0, bool skipEmpty = false);

	// copies a model from the sourceMap into this one
	void add_model(Bsp* sourceMap, int modelIdx);
	
	// create a new texture from raw RGB data, and embeds into the bsp. 
	// Returns -1 on failure, else the new texture index
	int add_texture(const char* name, byte* data, int width, int height);

	void replace_lump(int lumpIdx, void* newData, int newLength);

	bool is_invisible_solid(Entity* ent);

	// replace a model's clipnode hull with a axis-aligned bounding box
	void simplify_model_collision(int modelIdx, int hullIdx);

	// for use after scaling a model. Convex only.
	// Skips axis-aligned planes (bounding box should have been generated beforehand)
	void regenerate_clipnodes(int modelIdx);
	int16 regenerate_clipnodes(int iNode, int hullIdx);

	int create_clipnode();
	int create_plane();
	int create_model();
	int create_texinfo();

	// if the face's texinfo is not unique, a new one is created and returned. Otherwise, it's current texinfo is returned
	BSPTEXTUREINFO* get_unique_texinfo(int faceIdx);

	int get_model_from_face(int faceIdx);

	vector<STRUCTUSAGE*> get_sorted_model_infos(int sortMode);

private:
	int remove_unused_lightmaps(bool* usedFaces);
	int remove_unused_visdata(bool* usedLeaves, BSPLEAF* oldLeaves, int oldLeafCount); // called after removing unused leaves
	int remove_unused_textures(bool* usedTextures, int* remappedIndexes);
	int remove_unused_structs(int lumpIdx, bool* usedStructs, int* remappedIndexes);

	// for each model, split structures that are shared with models that both have and don't have an origin
	void split_shared_model_structures(int* modelsToMove, bool ignoreLeavesInModelsToMove);

	void resize_lightmaps(LIGHTMAP* oldLightmaps, LIGHTMAP* newLightmaps, BSPMODEL* target);

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
	// TODO: don't mark faces in submodel leaves (unused)
	void mark_model_structures(int modelIdx, STRUCTUSAGE* STRUCTUSAGE, bool skipLeaves);
	void mark_face_structures(int iFace, STRUCTUSAGE* usage);
	void mark_node_structures(int iNode, STRUCTUSAGE* usage, bool skipLeaves);
	void mark_clipnode_structures(int iNode, STRUCTUSAGE* usage);

	// remaps structure indexes to new locations
	void remap_face_structures(int faceIdx, STRUCTREMAP* remap);
	void remap_model_structures(int modelIdx, STRUCTREMAP* remap);
	void remap_node_structures(int iNode, STRUCTREMAP* remap);
	void remap_clipnode_structures(int iNode, STRUCTREMAP* remap);

	void update_lump_pointers();
};
