#pragma once
#include "bsplimits.h"
#include "mat4x4.h"
#include <vector>
#include "Polygon3D.h"
#include <future>
#include "colors.h"
#include "primitives.h"
#include <unordered_set>
#include "PickInfo.h"

class NavMesh;
class LeafNavMesh;
class PointEntRenderer;
struct EntCube;
class VertexBuffer;
class ShaderProgram;
class Texture;
class TextureArray;
struct TexArrayOffset;
struct lightmapVert;
struct cCube;
class Bsp;
class Entity;
struct LeafNode;
struct WADTEX;
struct Frustum;
struct BSPMODEL;
struct BSPFACE;
struct BSPLEAF;
struct NodeVolumeCuts;
class Wad;

struct LightmapInfo {
	// each face can have 4 lightmaps, and those may be split across multiple atlases
	uint16_t atlasId[MAXLIGHTMAPS];
	uint16_t x[MAXLIGHTMAPS];
	uint16_t y[MAXLIGHTMAPS];

	uint16_t w, h;

	float midTexU, midTexV;
	float midPolyU, midPolyV;
};

// texture within an atlas
struct SubTexture {
	int idx; // texture index
	int atlasId;
	uint16_t x, y, w, h;
	int sz;
};

struct FaceMath {
	mat4x4 worldToLocal; // transforms world coordiantes to this face's plane's coordinate system
	vec3 plane_x;
	vec3 plane_y;
	vec3 plane_z;
	float fdist;
	vector<vec3> verts;
	vector<vec2> localVerts;
	int index; // used to map a face to an element in some other list (e.g. leaf node mesh -> leaf index)

	int calcMemoryUsage();
};

struct RenderEnt {
	mat4x4 modelMat; // model matrix for rendering
	vec3 offset; // vertex transformations for picking
	vec3 angles; // vertex transformations for picking
	int modelIdx; // -1 = point entity
	EntCube* pointEntCube;

	int calcMemoryUsage();
};

struct RenderGroup {
	lightmapVert* verts;
	int vertCount;
	int arrayTextureIdx;
	int atlasTextureIdx;
	Texture* texture;
	Texture* lightmapAtlas[MAXLIGHTMAPS];
	VertexBuffer* buffer;
	bool transparent;

	int calcMemoryUsage();
};

struct RenderFace {
	int group;
	int vertOffset;
	int vertCount;
};

struct RenderModel {
	RenderGroup* renderGroups;
	int groupCount;
	RenderFace* renderFaces;
	int renderFaceCount;

	int calcMemoryUsage();
};

struct RenderClipnodes {
	VertexBuffer* clipnodeBuffer[MAX_MAP_HULLS];
	vector<FaceMath> faceMaths[MAX_MAP_HULLS];
};

struct RenderLeaves {
	VertexBuffer* leafBuffer;
	vector<FaceMath> faceMaths;
	vector<COLOR4> originalColors; // original color values for each vertex
	vector<int> leafRanges[65536]; // maps a leaf index to vertex indexes in the leafBuffer
};

struct RenderPvs {
	VertexBuffer* wireframePvsBuffer;
	vector<int> pvsLeaves;
	vector<int> pvsFaces;
	int leaf;
	int wpoly; // rendered bsp polys
};

struct OrderedEnt {
	Entity* ent;
	int entIdx;
	int modelIdx;
	bool isInMegaRenderGroup; // true if the transformed model for this entity was combined into a mega group and shouldn't be drawn individually
	mat4x4 transform;		// local transform (origin + rotation)
	mat4x4 transformWorld; // local transform + world offset applied
};

// references a render group in a rendermodel referenced by an entity index
struct EntModelGroupIdx {
	int entIdx;
	int groupIdx;
};

struct MegaRenderGroup {
	RenderGroup group;
	vector<EntModelGroupIdx> refs; // which entities to load vertices from
};

struct MegaRenderClipnodes {
	VertexBuffer* buffer[MAX_MAP_HULLS+1];
	int totalVerts[MAX_MAP_HULLS+1]; // extra hull for the automatic hull mode
	vector<int> refs; // which entities to load vertices from
};

// data needed to check if a face is in the PVS
struct PvsPoly {
	vec3 normal;
	vec3 mins, maxs;
	vector<vec3> verts;

	int calcMemoryUsage();
};

class BspRenderer {
	friend class ModelRenderer;
	friend class Editor;
public:
	Bsp* map;
	PointEntRenderer* pointEntRenderer;
	LeafNavMesh* leafNavMesh = NULL; // for leaf selection mode
	RenderPvs* pvsDat = NULL;
	vec3 mapOffset, renderOffset;
	vector<Wad*> wads;
	int lightStyleCount[MAXLIGHTMAPS]; // number of faces that use each light style

	BspRenderer(Bsp* map, PointEntRenderer* fgd);
	~BspRenderer();

	void getRenderEnts(vector<OrderedEnt>& ents); // calc ent data for multipass rendering
	void renderSolids(const vector<OrderedEnt>& orderedEnts, bool highlightAlwaysOnTop, bool transparencyPass);
	void renderClipnodes(const vector<OrderedEnt>& orderedEnts, int clipnodeHull);
	void renderLeaves();

	bool willDrawModel(Entity* ent, int modelIdx, bool transparent);
	void drawModel(Entity* ent, int modelIdx, bool transparent, bool highlight);
	void drawModelRenderGroup(RenderGroup& rgroup, bool highlight, bool useLightmaps);
	void drawModelWireframe(int modelIdx, bool highlight);
	void drawModelClipnodes(int modelIdx, bool highlight, int hullIdx);
	void drawPointEntities();
	void drawSkybox();
	void drawPvs();
	void updatePvs(vec3 viewOrigin);
	void addPvsPoly(int faceIdx, vec3 faceOffset, vec3 viewOrigin, Frustum* frustum, bool makeBuffer, vector<vec3>& allVerts);

	bool pickPoly(vec3 start, vec3 dir, int hullIdx, int& entIdx, int& faceIdx, int& leafIdx, float& bestDist);
	bool pickModelPoly(vec3 start, vec3 dir, vec3 offset, vec3 rot, int modelIdx, int hullIdx, int testEntidx, int& faceIdx, float& bestDist);
	bool pickLeaf(vec3 start, vec3 dir, int& leafIdx, float& bestDist);
	bool pickFaceMath(vec3 start, vec3 dir, FaceMath& faceMath, float& bestDist);
	void pickFrustum(Frustum& frustum, unordered_set<int>& pickEnts, unordered_set<int>& pickFaces, unordered_set<int>& pickLeaves, int hullIdx);
	void pickFrustumFaces(Frustum frustum, unordered_set<int>& pickFaces, vec3 offset, vec3 rot, int modelIdx, int hullIdx, int testEntidx);
	void pickFrustumLeaves(Frustum frustum, unordered_set<int>& pickLeaves);

	void refreshEnt(int entIdx);
	int refreshModel(int modelIdx, bool refreshClipnodes=true);
	bool RenderGroupsAreCombinable(RenderGroup& groupa, RenderGroup& groupb);
	int allocMegaBufferData(vector<OrderedEnt>& ents);
	void refreshMegaBuffers(vector<OrderedEnt>& ents); // update combined render groups for batching solid entity rendering
	bool refreshModelClipnodes(int modelIdx);
	void refreshFace(int faceIdx);
	void refreshPointEnt(int entIdx, bool uploadBuffer=true);

	void reload(); // reloads all geometry, textures, and lightmaps
	void reloadTextures(bool reloadNow=false);
	void reloadLightmaps();
	void reloadClipnodes();
	void reloadLeaves(bool reloadNow=false);
	void delayLoadLeaves(); // load leaf data if not already loaded
	void addClipnodeModel(int modelIdx);
	void updateModelShaders();

	// calculate vertex positions and uv coordinates once for faster rendering
	// also combines faces that share similar properties into a single buffer
	void preRenderFaces();
	void preRenderEnts();
	void calcFaceMaths();

	void preloadTextures(); // sets texture array positions for textures so geometry loader can set uvs
	void loadTextures(); // will reload them if already loaded
	void loadSkyboxTextures();
	void buildTextureAtlases(); // figure out where each texture will go
	void fillTextureAtlases(); // fill in the texture data loaded separately
	void updateLightmapInfos();
	bool isFinishedLoading();

	void highlightPickedFaces(bool highlight);
	void highlightPickedLeaves(bool highlight);
	void hideLeaves(bool hideNotUnhide);
	void hideFaces(bool hideNotUnhide); // must have hiddenFaces populated in app class
	void updateFaceUVs(int faceIdx);
	uint getFaceTextureId(int faceIdx);
	int addTextureToMap(string textureName); // adds a texture reference if found in a loaded WAD
	Texture* uploadTexture(WADTEX* tex);

	void write_obj_file();

	void generateSingleLeafNavMeshBuffer(LeafNode* node);
	EntCube* getEntCube(int idx);
	void delayLoadData();
	void reloadMegaBuffers();

	int calcMemoryUsage();

private:
	ShaderProgram* activeShader;

	LightmapInfo* lightmaps = NULL;
	RenderEnt* renderEnts = NULL;
	RenderModel* renderModels = NULL;
	vector<MegaRenderGroup> megaRenderGroups; // a combination of many bsp models that all share the same
										 // properties. There may be duplicates as a single model can be 
										 // used in many entities.
	MegaRenderClipnodes megaRenderClipnodes; // same as groups but for clipnodes
	int megaGroupUpdateIdx; // no update if this matches the last value, set to -1 to force an update
	int megaGroupUpdateLastPickCount;
	int megaGroupUpdateProgress; // used to limit buffers updated per frame to prevent lag on every click
	float megaGroupUpdateStartTime;
	unordered_set<int> megaGroupEnts; // ent indexes that are part of a mega group
	RenderClipnodes* renderClipnodeDat = NULL;
	RenderLeaves* renderLeafDat = NULL;
	FaceMath* faceMaths = NULL;
	VertexBuffer* pointEnts = NULL;
	VertexBuffer* skyBoxBuffer = NULL;
	vector<PvsPoly> facePolys; // for wpoly calculations

	// textures loaded in a separate thread
	Texture** glTexturesSwap = NULL;
	Texture* skyboxTexturesSwap[6];
	TextureArray* glTextureArray = NULL;
	TexArrayOffset* miptexToTexArray = NULL; // maps iMiptex to a texture layer in an unknown texturearray

	int numLightmapAtlases;
	int numTextureAtlases;
	int numTextureAtlasesSwap;
	int numRenderModels;
	int numRenderClipnodes;
	int numRenderLightmapInfos;
	int numFaceMaths;
	int numPointEnts;
	int numLoadedTextures = 0;
	int lightmapAtlasSz;
	int lightmapAtlasZoneSz;
	int textureAtlasSz;
	int textureAtlasZoneSz;

	vector<Polygon3D> debugFaces;
	NavMesh* debugNavMesh = NULL;


	Texture* skyboxTextures[6];
	Texture** glTextures = NULL;
	Texture** glLightmapTextures = NULL;
	Texture** glTextureAtlases = NULL;
	Texture** glTextureAtlasesSwap = NULL;
	vector<SubTexture> textureAtlasInfos;
	Texture* whiteTex = NULL;
	Texture* whiteTex3D = NULL;
	Texture* redTex = NULL;
	Texture* greyTex = NULL;
	Texture* blackTex = NULL;

	bool lightmapsGenerated = false;
	bool lightmapsUploaded = false;
	future<void> lightmapFuture;

	bool texturesLoaded = false;
	bool textureFacesLoaded = false;
	future<void> texturesFuture;

	bool clipnodesLoaded = false;
	int clipnodeLeafCount = 0;
	future<void> clipnodesFuture;

	bool leavesThreadFinished = false; // true if the loading thread is not running
	bool leavesLoaded = false; // true if leaf data is ready to use
	future<void> leavesFuture;

	bool megaBufferThreadFinished = false; // true if the loading thread is not running
	future<void> megaBufferFuture;

	void loadLightmaps();
	void loadClipnodes();
	void loadLeaves();
	void generateClipnodeBuffer(int modelIdx);
	void generateLeafBuffer();
	void generateNodeMesh(NodeVolumeCuts* volume, COLOR4 color, vector<clipnodeVert>& allVerts,
		vector<FaceMath>& faceMaths, int elementIndex);
	void generateNavMeshBuffer();
	void deleteRenderModel(RenderModel* renderModel);
	void deleteRenderModelClipnodes(RenderClipnodes* renderModel);
	void deleteRenderClipnodes();
	void deleteRenderLeaves();
	void deleteRenderFaces();
	void deleteTextures();
	void deleteLightmapTextures();
	void deleteFaceMaths();
	bool getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup);
	int getBestClipnodeHull(int modelIdx);
	Texture* generateMissingTexture(int width, int height);
};