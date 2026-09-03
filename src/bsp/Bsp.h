#pragma once
#include "bsplimits.h"
#include "rad.h"
#include <string.h>
#include "remap.h"
#include "bsptypes.h"
#include "Polygon3D.h"
#include <streambuf>
#include <set>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "colors.h"
#include "Wad.h"

class Entity;
class Wad;
struct WADTEX;
class LeafNavMesh;
class TextureAtlas;

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

#define BSPGUY_DATA_VERSION 1

enum BspguyDataTypes {
	BSPGUY_BSP_MODEL
};

struct BspModelData {
	vector<BSPPLANE> planes;
	vector<vec3> verts;
	vector<BSPEDGE> edges;
	vector<int32_t> surfEdges;
	vector<BSPTEXTUREINFO> texinfos;
	vector<BSPFACE> faces;
	vector<COLOR3> lightmaps;
	vector<BSPNODE> nodes;
	vector<BSPCLIPNODE> clipnodes;
	vector<BSPLEAF> leaves;
	vector<WADTEX> textures;
	BSPMODEL model;

	BspModelData();
	~BspModelData();

	string serialize();
	bool deserialize(string dat);
};

struct AtlasLightmap {
	int idx;
	int layer;
	int lightmapSz;
	uint16_t x, y, w, h; // position in atlas
};

struct GraphNode {
	uint64_t nodeIdx;
	int depth;
	uint64_t links[2];
	int nodeType; // -1 = solid, 0 = node, 1 = leaf
	float x, y;
	float srcX, srcY;
	int offset;
	bool thread;
};

struct NodeDepth {
	int nodeIdx;
	int depth;
};

// header for Quake 1 external lightmap lump for colored lighting
struct QLITHEADER {
	char magic[4]; // magic QLIT bytes
	int version;
};

enum BspFormat {
	BSP_QUAKE1,			// Quake 1, version 29
	BSP_QUAKE1_BSP2,	// 32bit structs, various source ports
	BSP_QUAKE1_2PSB,	// early version of the BSP2 format
	BSP_HALFLIFE,		// half-life, version 30
	BSP_BLUESHIFT,		// half-life blue shift, version 30
	BSP_GUY,			// an internal format that can be converted to any of the others
	BSP_UNKNOWN,		// unknown BSP format
	BSP_FORMAT_TYPES
};

extern const char* g_bsp_format_names[BSP_FORMAT_TYPES];
extern vec3 default_hull_extents[MAX_MAP_HULLS];
extern int g_sort_mode;

class Bsp
{
public:
	string path;
	string name;
	BSPHEADER header = BSPHEADER();
	byte ** lumps;
	bool valid;
	int lastSaveFormat = -1; // bsp format the user last saved as
	int lastLoadformat = -1; // bsp format loaded from disk

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
	BSPMARKSURF* marksurfs;

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
	int texDataLength;
	
	vector<Entity*> ents;

	Bsp();
	Bsp(const Bsp& other);
	Bsp(std::string fname);
	~Bsp();

	// pass -1 version to use version loaded from file
	int formatForFileVersion(int bspVersion=-1);

	int formatForGameEngine(int engine);

	// if modelIdx=0, the world is moved and all entities along with it
	bool move(vec3 offset, int modelIdx=0);

	void move_texinfo(int idx, vec3 offset);
	bool write(string path, bool force = false);

	void print_info(bool perModelStats, int perModelLimit, int sortMode);
	void print_model_hull(int modelIdx, int hull);
	void print_clipnode_tree(int iNode, int depth);
	void recurse_node(int32_t node, int depth);
	int32_t pointContents(int iNode, vec3 p, int hull, vector<int>& nodeBranch, int& leafIdx, int& childIdx);
	int32_t pointContents(int iNode, vec3 p, int hull);
	bool recursiveHullCheck(int hull, int num, float p1f, float p2f, vec3 p1, vec3 p2, TraceResult* trace);
	bool traceHull(vec3 start, vec3 end, int hull, TraceResult* ptr); // true if intersected anything
	int traceFace(vec3 start, vec3 end, int& u, int& v); // returns -1 if no face intersected. u and v set to texture coordinate intersect
	const char* getLeafContentsName(int32_t contents);

	// get lightmap value underneath this point, for entity illumination
	COLOR3 get_lighting(vec3 pos);

	// returns true if leaf is in the PVS from the given position
	bool is_leaf_visible(int ileaf, vec3 pos);

	// return PVS of the given leaf (leaf indexes which are potentially visible)
	vector<int> get_pvs(int ileaf);

	// applyMode = -1: remove leaves, 0: replace leaves, 1: add leaves
	void apply_pvs(vector<int>& targetLeaves, vector<int>& pvsLeaves, int applyMode);

	// select all leaves connected to the given leaves
	// ignoreLeaves will not be connected thru
	vector<int> get_connected_leaves(LeafNavMesh* mesh, const vector<int>& ileaves, const unordered_set<int>& ignoreLeaves);

	// returns the node path to the given leaf
	int get_node_branch(int iNode, vector<int>& branch, int ileaf);

	// returns all leaves underneath a node
	void get_child_leaves(int iNode, vector<int>& leaves);

	// get parent nodes for the given leaves
	void get_leaf_parents(int iNode, unordered_set<int>& leaves);

	// find all leaves which terminate the BSP tree, excluding solid leaves
	void get_terminal_leaves(int iNode, vector<int>& terminalLeaves);

	// descend the node until hitting a terminal leaf or 2-node fork
	void get_simple_leaf_branch(int iNode, vector<int>& branch);

	void add_face_to_leaf(int faceIdx, int leafIdx);

	// find leaves which make contact with the given faces and add the leaf to it
	// returns true if added to any leaves
	bool add_face_to_touched_leaves(int faceIdx);

	// merge all branches of the BSP tree which have a leaf at every node until termination
	void merge_simple_leaf_chains();

	// merge all sky leaves into one (assumes players should never see inside them)
	void merge_sky_leaves();

	// find the lowest node in the BSP tree that contains all given leaves
	int get_lowest_common_node(vector<int>& leaves);

	// returns true if any children fork into 2 nodes instead of 2 leaves, or 1 leaf and 1 node
	bool node_branch_has_forks(int iNode);

	// Merges leaves into one, preserving the PVS but losing all contained face
	// discardVis = discard face visibility data, for model conversion. inodes are leaves instead of nodes
	// returns number of leaves reduced, or 0 on failure
	int merge_leaves(vector<int>& inodes, bool discardVis);
	int merge_leaves(vector<int>& inodes, byte* decompressedVis, bool discardVis);

	// decompress vis lump
	byte* decompress_vis();

	// assumes leaf count didn't change since decompress. Deletes decompressed vis buffer
	void compress_vis(byte* decompressedVis);

	// get data needed to render the bsp tree as a graph. Returns max depth
	int get_leaf_graph(int iNode, vector<GraphNode>& gnodes, int depth, bool includeSolid);

	// count instances of each leaf in the BSP tree
	void get_leaf_counts(int iNode, unordered_map<int, int>& leafCounts);

	// returns all faces marked by the given leaf
	vector<int> get_leaf_faces(int ileaf);

	// replaces all instances of replace leaves with the replaceWith leaf in the bsp tree
	void replace_leaves(int iNode, unordered_set<int>& replace, int replaceWith);

	// get a list of all child nodes. Also includes the start node.
	void get_child_nodes(int iNode, int depth, vector<NodeDepth>& nodes);

	// get parent nodes for the given leaves
	void get_leaf_parents(int iNode, unordered_set<int>& leaves, vector<int>& parents);

	// get parent node for the given child idx
	int get_node_parent(int iNode, int childIdx);

	// get all faces marked by the given node and its children
	void get_node_faces(int iNode, vector<int>& faces);

	// true if this node and all its children reference faces in a consecutive order (no gaps)
	bool node_branch_faces_are_consecutive(int iNode);

	bool is_face_visible(int faceIdx, vec3 pos, vec3 angles);

	int count_visible_polys(vec3 pos, vec3 angles);

	// get leaf index from world position
	int get_leaf(vec3 pos, int hull);

	// get leaf index from face index
	int get_leaf_from_face(int faceIdx);

	// strips a collision hull from the given model index
	// and redirects to the given hull, if redirect>0
	void delete_hull(int hull_number, int modelIdx, int redirect);

	// strips a collision hull from all models
	// and redirects to the given hull, if redirect>0
	void delete_hull(int hull_number, int redirect);

	void dump_lightmap(int faceIdx, string outputPath);
	void dump_lightmap_atlas(string outputPath);

	void write_csg_outputs(string path);

	// write the portal file needed for the VIS compiler
	void write_portal_file(LeafNavMesh* mesh, const char* fname);

	// get the bounding box for the world
	void get_bounding_box(vec3& mins, vec3& maxs);

	// get the bounding box for all vertexes in a BSP tree
	void get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs);

	void get_model_hull_bounds(int modelIdx, int hull, vec3& mins, vec3& maxs);

	// slighty shrunk to allow merging models that touch each other
	void get_model_merge_bounds(int modelIdx, vec3& mins, vec3& maxs);

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
	vector<NodeVolumeCuts> get_model_leaf_volume_cuts(int modelIdx, int hullIdx, int32_t contents);
	void get_clipnode_leaf_cuts(int iNode, vector<BSPPLANE>& clipOrder, vector<NodeVolumeCuts>& output, int32_t contents);
	void get_node_leaf_cuts(int iNode, vector<BSPPLANE>& clipOrder, vector<NodeVolumeCuts>& output, int32_t contents);

	int count_clipnode_solids(int iNode);

	// returns number of planes created
	int expand_clipnode_hull_node(int hull, float vertical, float horizontal);

	// expand/shrink clipnode hull planes
	void expand_clipnode_hull(int hull, float vertical, float horizontal);

	// this a cheat to recalculate plane normals after scaling a solid. Really I should get the plane
	// intersection code working for nonconvex solids, but that's looking like a ton of work.
	// Scaling/stretching really only needs 3 verts _anywhere_ on the plane to calculate new normals/origins.
	vector<ScalableTexinfo> getScalableTexinfos(int modelIdx); // for scaling
	int addTextureInfo(BSPTEXTUREINFO& copy);

	// fixes up the model planes/nodes after vertex posisions have been modified
	// returns false if the model has non-planar faces
	// TODO: split any planes shared with other models
	bool vertex_manipulation_sync(int modelIdx, vector<TransformVert>& hullVerts, bool convexCheckOnly, bool regenClipnodes);

	void load_ents(byte* lump, int lumpLen, vector<Entity*>& entList);

	// call this after editing ents
	byte* create_ent_lump(vector<Entity*>& entList, int& len, bool stripNodes=false);

	// updates using current entity list
	void update_ent_lump(bool stripNodes=false);

	vec3 get_model_center(int modelIdx);

	// returns the number of lightmaps applied to the face, or 0 if it has no lighting
	int lightmap_count(int faceIdx);

	// gets highest value light style in the map
	int lightstyle_count();

	// combines style lightmap to the base lightmap for all faces
	// faceIdx = bake only this face, else all faces
	int bake_lightmap_style(int style, bool deleteNotBake, bool reduceStyles, int faceIdx=-1);

	TextureAtlas* create_lightmap_style_atlas(int style, vector<AtlasLightmap>& lightmaps);

	// export lightmap atlas to a PNG for bulk editing
	void export_lightmap_style(int style, const char* fname);
	void import_lightmap_style(int style, const char* fname);

	// returns the number of lightmaps that were baked into the base lightmap, if no light referenced them
	// also forces toggled light styles to be contiguous and start at the lowest offset (for merging)
	int remove_unused_lightstyles();

	// move lightstyle indexes by the given amount (for merging)
	bool shift_lightstyles(uint32_t shift);

	bool isValid(); // check if any lumps are overflowed
	bool isWritable(); // check if any lumps are overflowed which would corrupt the file

	// delete structures not used by the map (needed after deleting models/hulls)
	STRUCTCOUNT remove_unused_model_structures(bool deleteModels=true);
	void delete_model(int modelIdx);

	// conditionally deletes hulls for entities that aren't using them
	STRUCTCOUNT delete_unused_hulls(bool noProgress=false);

	// deletes data outside the map bounds
	void delete_oob_data(int clipFlags);

	void delete_oob_clipnodes(int iNode, int32_t* parentBranch, vector<BSPPLANE>& clipOrder, 
		int oobFlags, bool* oobHistory, bool isFirstPass, int& removedNodes);
	
	void delete_oob_nodes(int iNode, int32_t* parentBranch, vector<BSPPLANE>& clipOrder, 
		int oobFlags, bool* oobHistory, bool isFirstPass, int& removedNodes);

	// deletes data inside a bounding box
	void delete_box_data(vec3 clipMins, vec3 clipMaxs);
	void delete_box_clipnodes(int iNode, int32_t* parentBranch, vector<BSPPLANE>& clipOrder,
		vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes);
	void delete_box_nodes(int iNode, int32_t* parentBranch, vector<BSPPLANE>& clipOrder,
		vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes);

	// assumes contiguous leaves starting at 0. Only works for worldspawn, which is the only model which
	// should have leaves anyway.
	void count_leaves(int iNode, int& leafCount);

	// searches for entities that have very similar models,
	// then updates the entities to share a single model reference
	// this reduces the precached model count even though the models are still present in the bsp
	int deduplicate_models(bool allowTextureShift, bool dryrun);

	int get_entity_index(Entity* ent);
	
	int count_faces_for_mip(int miptex);

	// scales up texture axes for any face with bad surface extents
	bool fix_bad_surface_extents_with_scale(int faceIdx);
	void fix_bad_surface_extents_with_scale();

	// fix bad extents by downscaling textures and scaling up face coordinates
	void fix_bad_surface_extents_with_downscale(int minTextureDim);

	// count how many face subdivisions would be needed to fix bad surface extents for all faces
	// that use the given texture
	int get_subdivisions_needed_to_fix_mip_extents(int mip);

	// subdivide faces until they have valid surface extents
	void fix_all_bad_surface_extents_with_subdivide(int subdivideLimitPerTexture);
	int fix_bad_surface_extents_with_subdivide(int faceIdx);

	// reduces size of textures that exceed game limits and adjusts face scales accordingly
	int downscale_invalid_textures(vector<Wad*>& wads);

	// downscales a texture to no lower dimension than minDim
	// allowWad:true = texture coordinates will be scaled even if the the texture is from a WAD and must be scaled separately
	// returns true if was downscaled
	bool downscale_texture(int textureId, int minDim, bool allowWad);

	bool downscale_texture(int textureId, int newWidth, int newHeight, int resampleMode);

	bool rename_texture(const char* oldName, const char* newName);

	bool embed_texture(int textureId, vector<Wad*>& wads);

	// return 1 on success, 0 on failure, 2 on success and resize
	int unembed_texture(int textureId, vector<Wad*>& wads, bool force=false, bool quiet=false);

	int embed_all_textures();

	// adds a texture reference to the BSP (does not embed it)
	// returns an iMipTex for use in texture infos
	int add_texture_from_wad(WADTEX* tex);

	// returns the embedded texture data or texture data from WAD, if it exists
	WADTEX load_texture(int textureIdx);

	bool replace_texture(int textureIdx, WADTEX& tex);

	vector<string> get_wad_names();

	// returns the WAD or BSP name the texture is loaded from
	string get_texture_source(string texname, vector<Wad*>& wads);

	void remove_unused_wads(vector<Wad*>& wads);

	// returns data for all embedded textures, ready to be wrtten to a WAD
	vector<WADTEX> get_embedded_textures();

	// gets the ID for a texture, or -1 if not found
	int get_texture_id(string name);

	// updates texture coordinates after a texture has been resized
	void adjust_resized_texture_coordinates(BSPFACE& face, BSPTEXTUREINFO& info, int newWidth, int newHeight, int oldWidth, int oldHeight);
	void adjust_resized_texture_coordinates(int textureId, int oldWidth, int oldHeight);

	// moves entity models to (0,0,0), duplicating the BSP model if necessary
	int zero_entity_origins(string classname);
	
	// set origins to 0,0,0 on all entities that break with non-zero origins, duplicating BSP models if necesasry
	int zero_sensitive_entity_origins();

	// reference vector for computing ut angle
	vec3 get_face_center(int faceIdx);

	// get reference vectors for texture rotations
	vec3 get_face_ut_reference(int faceIdx);

	void get_face_verts(int faceIdx, vector<vec3>& verts);

	void get_face_bounding_box(int faceIdx, vec3& mins, vec3& maxs);

	void get_face_plane(int faceIdx, vec3& v0, vec3& normal);

	int get_default_texture_idx();

	// scales up texture sizes on models that aren't used by visible entities
	int allocblock_reduction();

	// gets estimated number of allocblocks filled
	// actual amount will vary because there is some wasted space when the engine generates lightmap atlases
	float calc_allocblock_usage();

	// will stretch to fit face lightmap dimensions with filtering
	void apply_lightmap(int faceIdx, int layer, COLOR3* data, int srcW, int srcH);

	void get_scaled_texture_dimensions(int textureIdx, float scale, int& newWidth, int& newHeight);

	// true if any face using this texture has bad extents, if texture info is scaled by scale
	bool has_bad_extents(int textureIdx, float scale);

	// true if any face in the map has bad extents
	bool has_bad_extents();

	// returns how much to scale up face textures to fix all bad extents in the map
	float get_scale_to_fix_bad_extents(int textureIdx);

	// subdivides along the axis with the most texture pixels (for biggest surface extent reduction)
	// if dryRun, only update the lumps needed for calculating surface extents
	bool subdivide_face(int faceIdx, bool dryRunForExtents=false);

	void delete_face(int faceId);

	void delete_faces(vector<int>& faceIds);

	// select faces connected to the given one
	// ignoreFaces will not be connected thru
	// planarTextureOnly = only select on the same plane with the same texture
	unordered_set<int> select_connected_faces(vector<int>& srcFaces, unordered_set<int>& ignoreFaces, bool planarOnly, bool textureOnly);

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
	void insert_leaves(int offset, int count); // only insert after all world leaves or else fix VIS yourself
	
	void insert_marksurfs(int offset, int count);
	
	void create_node_box(vec3 mins, vec3 maxs, BSPMODEL* targetModel, int textureIdx);
	void create_nodes(Solid& solid, BSPMODEL* targetModel);
	// returns index of the solid node
	int create_clipnode_box(vec3 mins, vec3 maxs, BSPMODEL* targetModel, int targetHull = 0, bool skipEmpty = false);

	// copies a model from the sourceMap into this one
	void add_model(Bsp* sourceMap, int modelIdx);
	
	// create a new texture from raw RGB data, and embeds into the bsp. 
	// Returns -1 on failure, else the new texture index
	int add_texture(const char* name, byte* data, int width, int height);

	// add the texture only if does not replace an existing texture,
	// otherwise return the existing texture index
	int add_texture(WADTEX texture);

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
	int create_node();

	void insert_nodes(int offset, int count);

	// create a new model using existing faces. Collision will be completely solid.
	int create_model_from_faces(vector<int>& faceIndexes);

	// get all faces marked by the given leaves
	vector<int> get_leaf_faces(vector<int>& ileaves);

	// converts world leaves to a BSP model. This does not affect collision or PVS, but world faces
	// will be invisible. The BSP model is used for rendering instead.
	int convert_leaves_to_model(vector<int>& leafIndexes);

	int duplicate_model(int modelIdx);

	string stringify_model(int modelIdx);

	int add_model(string serialized);

	// for each entity, duplicate its BSP model, remove its origin offset.
	// merge all models together into one, if none of their bounds overlap, even if this means
	// duplicating model data (2 entities share the same model).
	// returns: -1 = merge failed before bsp was modified, or invalid models are selected
	//          -2 = merge aborted because clipnodes overlap
	//          -3 = merge failed after bsp was modified
	//          0+ = the new model index
	int merge_models(vector<Entity*> ents, bool allowClipnodeOverlap);

	// merge 2 models if their bounds don't overlap
	int merge_models(Entity* enta, Entity* entb);

	// returns a plane which bisects the area between the 2 bounding boxes.
	// returns a plane with nType -1 if the boxes overlap
	static BSPPLANE get_separation_plane(vec3 minsA, vec3 maxsA, vec3 minsB, vec3 maxsB);

	// if the face's texinfo is not unique, a new one is created and returned. Otherwise, it's current texinfo is returned
	BSPTEXTUREINFO* get_unique_texinfo(int faceIdx);

	bool is_embedded_rad_texture_name(const char* name);

	// returns original texinfo referenced by an embedded rad texture created by VHLT
	BSPTEXTUREINFO* get_embedded_rad_texinfo(BSPTEXTUREINFO& info);
	BSPTEXTUREINFO* get_embedded_rad_texinfo(const char* texName);

	// generate a combined WAD file for the RAD compiler and save it next to the BSP file
	void generate_wa_file();

	int count_missing_textures();

	// ensures entity that has a texlight model is using a unique model
	int make_unique_texlight_models();

	// true if any entities share a same BSP model
	bool do_entities_share_models();

	// get texlight info from info_texlights entities
	unordered_map<string, string> get_tex_lights();

	// removes texlights for textures not present in the map
	unordered_map<string, string> filter_tex_lights(const unordered_map<string, string>& inputLights);

	// import texlight info to a new or existing info_texlights entity
	// returns true if any changes were made
	unordered_map<string, string> load_texlights_from_file(string fname);

	bool load_texlight_from_string(string line, string& name, string& args);

	// returns true if any changes were made
	bool add_texlights(const unordered_map<string, string>& newLights);

	bool replace_texlights(string texlightString);

	// epsilon = how different texlight pixels can be to still be considered a texlight
	unordered_map<string, string> estimate_texlights(int epsilon=8);

	BSPMIPTEX* get_texture(int iMiptex);

	// returns -1 if invalid RAD textures were detected (will need the original BSP file)
	// else returns number of textures deleted
	int delete_embedded_rad_textures(Bsp* originalMap);

	int get_model_from_face(int faceIdx);

	vector<STRUCTUSAGE*> get_sorted_model_infos(int sortMode);

	// split structures that are shared between the target and other models
	void split_shared_model_structures(int modelIdx);

	// true if the model is sharing planes/clipnodes with other models
	bool does_model_use_shared_structures(int modelIdx);

	// don't allow models to have nodes that interleave with other models
	void make_nodes_contiguous();

	// don't allow models to have clipnodes that interleave with other models
	void make_clipnodes_contiguous();

	// returns the current lump contents
	LumpState duplicate_lumps(int targets);

	void replace_lumps(LumpState& state);

	int delete_embedded_textures();

	int find_texture(const char* name);

	void update_lump_pointers();

	bool did_lumps_change(bool ignoreEntLump);

	int calcMemoryUsage();

	// convert an RGB lightmap pixel to monochrome (for Quake 1)
	static inline uint8_t monochrome_lightmap_pixel(COLOR3& c) { return (uint8_t)((77 * c.r + 150 * c.g + 29 * c.b) >> 8); }

private:
	bool* pvsFaces = NULL; // flags which faces are marked for rendering in the PVS
	int pvsFaceCount = 0;

	int remove_unused_lightmaps(bool* usedFaces);
	int remove_unused_visdata(STRUCTREMAP* remap, BSPLEAF* oldLeaves, int oldLeafCount, int oldWorldspawnLeafCount); // called after removing unused leaves
	int remove_unused_textures(bool* usedTextures, int* remappedIndexes);
	int remove_unused_structs(int lumpIdx, bool* usedStructs, int* remappedIndexes);

	void resize_lightmaps(LIGHTMAP* oldLightmaps, LIGHTMAP* newLightmaps);

	bool load_lumps(string fname, BSPHEADER& head, LumpState& state);

	// convert a file-specific struct to the internal format
	void internalize_face(BSPFACE_29& src, BSPFACE& dst);
	void internalize_leaf(BSPLEAF_29& src, BSPLEAF& dst);
	void internalize_leaf(BSPLEAF_2PSB& src, BSPLEAF& dst);
	void internalize_edge(BSPEDGE_29& src, BSPEDGE& dst);
	void internalize_node(BSPNODE_29& src, BSPNODE& dst);
	void internalize_node(BSPNODE_2PSB& src, BSPNODE& dst);
	void internalize_clip(BSPCLIPNODE_29& src, BSPCLIPNODE& dst);
	void internalize_mark(BSPMARKSURF_29& src, BSPMARKSURF& dst);

	// convert internal struct for a specific file format
	void externalize_face(BSPFACE& src, BSPFACE_29& dst);
	void externalize_leaf(BSPLEAF& src, BSPLEAF_29& dst);
	void externalize_edge(BSPEDGE& src, BSPEDGE_29& dst);
	void externalize_node(BSPNODE& src, BSPNODE_29& dst);
	void externalize_clip(BSPCLIPNODE& src, BSPCLIPNODE_29& dst);
	void externalize_mark(BSPMARKSURF& src, BSPMARKSURF_29& dst);

	// convert file lumps to the internal format
	void internalize_lumps(int fromVersion, LumpState& state);

	// convert internal lumps to a standard file format
	void externalize_lumps(int toVersion, LumpState& state);

	// convert to/from monochrome and RGB lightmaps
	void convert_lightmaps(LumpState& state, bool quakeNotHl);

	// convert to/from quake texture palettes and half-life
	void convert_texture_palettes(LumpState& state, bool monochromeNotRgb);

	// lightmaps that are resized due to precision errors should not be stretched to fit the new canvas.
	// Instead, the texture should be shifted around, depending on which parts of the canvas is "lit" according
	// to the qrad code. Shifts apply to one or both of the lightmaps, depending on which dimension is bigger.
	void get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY);

	void print_model_bsp(int modelIdx);
	void print_leaf(int leafidx);
	void print_node(int nodeidx);
	void print_stat(string name, uint val, uint max, bool isMem);
	void print_model_stat(STRUCTUSAGE* modelInfo, uint val, uint max, bool isMem);

	string get_model_usage(int modelIdx); // description of the entity that uses this model
	vector<Entity*> get_model_ents(int modelIdx);

	void write_csg_polys(int32_t nodeIdx, FILE* fout, int flipPlaneSkip, bool debug);	

	void write_portal_file_leaf_count(int iNode, FILE* fout);

	// create a mapping from bsp leaf index to portal file leaf number
	void get_portal_file_leaf_numbers(int iNode, unordered_map<uint32_t, uint32_t>& leafMap, int &leafCount);
	
	// returns number of portals written, or that would be written if fout is NULL
	int write_portal_file_portals(int iNode, LeafNavMesh* mesh, unordered_set<uint32_t>& visited, 
		unordered_map<uint32_t, uint32_t>& leafMap, FILE* fout);

	// marks all structures that this model uses
	// TODO: don't mark faces in submodel leaves (unused)
	void mark_model_structures(int modelIdx, STRUCTUSAGE* STRUCTUSAGE, bool skipLeaves);
	void mark_face_structures(int iFace, STRUCTUSAGE* usage);
	void mark_node_structures(int iNode, STRUCTUSAGE* usage, bool skipLeaves);
	void mark_clipnode_structures(int iNode, STRUCTUSAGE* usage);
	
	// remove links to faces in leaves used by a submodel.
	// This is a loss of data. The compiler generated leaves for submodels, for some reason.
	// I don't know why. The game doesn't use leaves besides checking contents. This may bite me later.
	// Relinking the faces is hopefully simple if needed. One face per leaf? Taken from the parent node?
	void unlink_model_leaf_faces(int modelIdx);
	void unlink_model_leaf_faces_by_node(int iNode);

	// remaps structure indexes to new locations
	void remap_face_structures(int faceIdx, STRUCTREMAP* remap);
	void remap_model_structures(int modelIdx, STRUCTREMAP* remap);
	void remap_node_structures(int iNode, STRUCTREMAP* remap);
	void remap_clipnode_structures(int iNode, STRUCTREMAP* remap);

};
