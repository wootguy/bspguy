#pragma once
#include "bsplimits.h"
#include "rad.h"
#include <string.h>
#include "remap.h"
#include "bsptypes.h"
#include "Polygon3D.h"
#include <streambuf>
#include <set>

class Entity;
class Wad;
struct WADTEX;

#define OOB_CLIP_X 1
#define OOB_CLIP_X_NEG 2
#define OOB_CLIP_Y 4
#define OOB_CLIP_Y_NEG 8
#define OOB_CLIP_Z 16
#define OOB_CLIP_Z_NEG 32

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
	BSPHEADER header = BSPHEADER();
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

	// VIS data is a compressed 2D array.
	// Example binary for uncompressed vis data in a map with 4 leaves:
	//     0000 ... (no leaves are visible from leaf 1)
	//     1001 ... (leaves 1 and 4 are visible from leaf 2)
	//     1111 ... (all leaves are visible from leaf 3)
	// There are only 3 rows because the shared solid leaf 0 is excluded from both columns and rows.
	// Dots "..." indicate padding. Rows are padded to multiples of 8 bytes/64 leaves.
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
	Bsp(const Bsp& other);
	Bsp(std::string fname);
	~Bsp();

	// if modelIdx=0, the world is moved and all entities along with it
	bool move(vec3 offset, int modelIdx=0);

	void move_texinfo(int idx, vec3 offset);
	void write(string path);

	void print_info(bool perModelStats, int perModelLimit, int sortMode);
	void print_model_hull(int modelIdx, int hull);
	void print_clipnode_tree(int iNode, int depth);
	void recurse_node(int16_t node, int depth);
	int32_t pointContents(int iNode, vec3 p, int hull, vector<int>& nodeBranch, int& leafIdx, int& childIdx);
	int32_t pointContents(int iNode, vec3 p, int hull);
	bool recursiveHullCheck(int hull, int num, float p1f, float p2f, vec3 p1, vec3 p2, TraceResult* trace);
	void traceHull(vec3 start, vec3 end, int hull, TraceResult* ptr);
	const char* getLeafContentsName(int32_t contents);

	// returns true if leaf is in the PVS from the given position
	bool is_leaf_visible(int ileaf, vec3 pos);

	bool is_face_visible(int faceIdx, vec3 pos, vec3 angles);

	int count_visible_polys(vec3 pos, vec3 angles);

	// get leaf index from world position
	int get_leaf(vec3 pos, int hull);

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

	// true if the center of this face is touching an empty leaf
	bool isInteriorFace(const Polygon3D& poly, int hull);

	// get cuts required to create bounding volumes for each solid leaf in the model
	vector<NodeVolumeCuts> get_model_leaf_volume_cuts(int modelIdx, int hullIdx, int16_t contents);
	void get_clipnode_leaf_cuts(int iNode, vector<BSPPLANE>& clipOrder, vector<NodeVolumeCuts>& output, int16_t contents);
	void get_node_leaf_cuts(int iNode, vector<BSPPLANE>& clipOrder, vector<NodeVolumeCuts>& output, int16_t contents);

	// this a cheat to recalculate plane normals after scaling a solid. Really I should get the plane
	// intersection code working for nonconvex solids, but that's looking like a ton of work.
	// Scaling/stretching really only needs 3 verts _anywhere_ on the plane to calculate new normals/origins.
	vector<ScalableTexinfo> getScalableTexinfos(int modelIdx); // for scaling
	int addTextureInfo(BSPTEXTUREINFO& copy);

	// fixes up the model planes/nodes after vertex posisions have been modified
	// returns false if the model has non-planar faces
	// TODO: split any planes shared with other models
	bool vertex_manipulation_sync(int modelIdx, vector<TransformVert>& hullVerts, bool convexCheckOnly, bool regenClipnodes);

	void load_ents();

	// call this after editing ents
	void update_ent_lump(bool stripNodes=false);

	vec3 get_model_center(int modelIdx);

	// returns the number of lightmaps applied to the face, or 0 if it has no lighting
	int lightmap_count(int faceIdx);

	// gets highest value light style in the map
	int lightstyle_count();

	// combines style lightmap to the base lightmap for all faces
	void bake_lightmap(int style);

	// returns the number of lightmaps that were baked into the base lightmap, if no light referenced them
	// also forces toggled light styles to be contiguous and start at the lowest offset (for merging)
	int remove_unused_lightstyles();

	// move lightstyle indexes by the given amount (for merging)
	bool shift_lightstyles(uint32_t shift);

	bool isValid(); // check if any lumps are overflowed

	// delete structures not used by the map (needed after deleting models/hulls)
	STRUCTCOUNT remove_unused_model_structures();
	void delete_model(int modelIdx);

	// conditionally deletes hulls for entities that aren't using them
	STRUCTCOUNT delete_unused_hulls(bool noProgress=false);

	// deletes data outside the map bounds
	void delete_oob_data(int clipFlags);

	void delete_oob_clipnodes(int iNode, int16_t* parentBranch, vector<BSPPLANE>& clipOrder, 
		int oobFlags, bool* oobHistory, bool isFirstPass, int& removedNodes);
	
	void delete_oob_nodes(int iNode, int16_t* parentBranch, vector<BSPPLANE>& clipOrder, 
		int oobFlags, bool* oobHistory, bool isFirstPass, int& removedNodes);

	// deletes data inside a bounding box
	void delete_box_data(vec3 clipMins, vec3 clipMaxs);
	void delete_box_clipnodes(int iNode, int16_t* parentBranch, vector<BSPPLANE>& clipOrder,
		vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes);
	void delete_box_nodes(int iNode, int16_t* parentBranch, vector<BSPPLANE>& clipOrder,
		vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes);

	// assumes contiguous leaves starting at 0. Only works for worldspawn, which is the only model which
	// should have leaves anyway.
	void count_leaves(int iNode, int& leafCount);

	// searches for entities that have very similar models,
	// then updates the entities to share a single model reference
	// this reduces the precached model count even though the models are still present in the bsp
	void deduplicate_models();
	
	// scales up texture axes for any face with bad surface extents
	// connected planar faces which use the same texture will also be scaled up to prevent seams
	// showing between faces with different texture scales
	// scaleNotSubdivide:true = scale face textures to lower extents
	// scaleNotSubdivide:false = subdivide face textures to lower extents
	// downscaleOnly:true = don't scale or subdivide anything, just downscale the textures
	// maxTextureDim = downscale textures first if they are larger than this (0 = disable)
	void fix_bad_surface_extents(bool scaleNotSubdivide, bool downscaleOnly, int maxTextureDim);

	// subdivide a face until it has valid surface extents
	void fix_bad_surface_extents_with_subdivide(int faceIdx);

	// reduces size of textures that exceed game limits and adjusts face scales accordingly
	void downscale_invalid_textures(vector<Wad*>& wads);

	// downscales a texture to the maximum specified width/height
	// allowWad:true = texture coordinates will be scaled even if the the texture is from a WAD and must be scaled separately
	// returns true if was downscaled
	bool downscale_texture(int textureId, int maxDim, bool allowWad);

	bool downscale_texture(int textureId, int newWidth, int newHeight);

	bool rename_texture(const char* oldName, const char* newName);

	bool embed_texture(int textureId, vector<Wad*>& wads);

	bool unembed_texture(int textureId, vector<Wad*>& wads);

	// adds a texture reference to the BSP (does not embed it)
	// returns an iMipTex for use in texture infos
	int add_texture_from_wad(WADTEX* tex);

	vector<string> get_wad_names();

	// returns the WAD or BSP name the texture is loaded from
	string get_texture_source(string texname, vector<Wad*>& wads);

	void remove_unused_wads(vector<Wad*>& wads);

	// updates texture coordinates after a texture has been resized
	void adjust_resized_texture_coordinates(int textureId, int oldWidth, int oldHeight);

	// moves entity models to (0,0,0), duplicating the BSP model if necessary
	int zero_entity_origins(string classname);

	// reference vector for computing ut angle
	vec3 get_face_center(int faceIdx);

	// get reference vectors for texture rotations
	vec3 get_face_ut_reference(int faceIdx);

	// scales up texture sizes on models that aren't used by visible entities
	void allocblock_reduction();

	// gets estimated number of allocblocks filled
	// actual amount will vary because there is some wasted space when the engine generates lightmap atlases
	float calc_allocblock_usage();

	// subdivides along the axis with the most texture pixels (for biggest surface extent reduction)
	bool subdivide_face(int faceIdx);

	// select faces connected to the given one, which lie on the same plane and use the same texture
	set<int> selectConnectedTexture(int modelId, int faceId);

	// returns true if the map has eny entities that make use of hull 2
	bool has_hull2_ents();
	
	// check for bad indexes
	bool validate();

	bool validate_vis_data();

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
	void append_lump(int lumpIdx, void* newData, int appendLength);

	bool is_invisible_solid(Entity* ent);

	// replace a model's clipnode hull with a axis-aligned bounding box
	void simplify_model_collision(int modelIdx, int hullIdx);

	// for use after scaling a model. Convex only.
	// Skips axis-aligned planes (bounding box should have been generated beforehand)
	void regenerate_clipnodes(int modelIdx, int hullIdx);
	int16 regenerate_clipnodes_from_nodes(int iNode, int hullIdx);

	int create_clipnode();
	int create_plane();
	int create_model();
	int create_texinfo();

	int duplicate_model(int modelIdx);

	int merge_models(int modelIdxA, int modelIdxB);

	// returns a plane which bisects the area between the 2 bounding boxes.
	// returns a plane with nType -1 if the boxes overlap
	static BSPPLANE get_separation_plane(vec3 minsA, vec3 maxsA, vec3 minsB, vec3 maxsB);

	// if the face's texinfo is not unique, a new one is created and returned. Otherwise, it's current texinfo is returned
	BSPTEXTUREINFO* get_unique_texinfo(int faceIdx);

	int get_model_from_face(int faceIdx);

	vector<STRUCTUSAGE*> get_sorted_model_infos(int sortMode);

	// split structures that are shared between the target and other models
	void split_shared_model_structures(int modelIdx);

	// true if the model is sharing planes/clipnodes with other models
	bool does_model_use_shared_structures(int modelIdx);

	// returns the current lump contents
	LumpState duplicate_lumps(int targets);

	void replace_lumps(LumpState& state);

	int delete_embedded_textures();

	BSPMIPTEX * find_embedded_texture(const char * name);

	void update_lump_pointers();

private:
	bool* pvsFaces = NULL; // flags which faces are marked for rendering in the PVS
	int pvsFaceCount = 0;

	int remove_unused_lightmaps(bool* usedFaces);
	int remove_unused_visdata(STRUCTREMAP* remap, BSPLEAF* oldLeaves, int oldLeafCount, int oldWorldspawnLeafCount); // called after removing unused leaves
	int remove_unused_textures(bool* usedTextures, int* remappedIndexes);
	int remove_unused_structs(int lumpIdx, bool* usedStructs, int* remappedIndexes);

	void resize_lightmaps(LIGHTMAP* oldLightmaps, LIGHTMAP* newLightmaps);

	bool load_lumps(string fname);

	// lightmaps that are resized due to precision errors should not be stretched to fit the new canvas.
	// Instead, the texture should be shifted around, depending on which parts of the canvas is "lit" according
	// to the qrad code. Shifts apply to one or both of the lightmaps, depending on which dimension is bigger.
	void get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY);

	void print_model_bsp(int modelIdx);
	void print_leaf(BSPLEAF leaf);
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

};
