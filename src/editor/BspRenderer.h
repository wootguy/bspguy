#pragma once
#include "bsplimits.h"
#include "mat4x4.h"
#include <vector>
#include "Polygon3D.h"
#include <future>

class NavMesh;
class PointEntRenderer;
struct EntCube;
class VertexBuffer;
class ShaderProgram;
class Texture;
struct lightmapVert;
struct cCube;
class Bsp;
class Entity;
struct LeafNode;
struct WADTEX;

#define LIGHTMAP_ATLAS_SIZE 512

enum RenderFlags {
	RENDER_TEXTURES = 1,
	RENDER_LIGHTMAPS = 2,
	RENDER_WIREFRAME = 4,
	RENDER_ENTS = 8,
	RENDER_SPECIAL = 16,
	RENDER_SPECIAL_ENTS = 32,
	RENDER_POINT_ENTS = 64,
	RENDER_ORIGIN = 128,
	RENDER_WORLD_CLIPNODES = 256,
	RENDER_ENT_CLIPNODES = 512,
	RENDER_ENT_CONNECTIONS = 1024,
	RENDER_MAP_BOUNDARY = 2048,
	RENDER_STUDIO_MDL = 4096,
	RENDER_SPRITES = 8192,
	RENDER_ENT_DIRECTIONS = 16384,
};

struct LightmapInfo {
	// each face can have 4 lightmaps, and those may be split across multiple atlases
	int atlasId[MAXLIGHTMAPS];
	int x[MAXLIGHTMAPS];
	int y[MAXLIGHTMAPS];

	int w, h;

	float midTexU, midTexV;
	float midPolyU, midPolyV;
};

struct FaceMath {
	mat4x4 worldToLocal; // transforms world coordiantes to this face's plane's coordinate system
	vec3 plane_x;
	vec3 plane_y;
	vec3 plane_z;
	float fdist;
	vector<vec3> verts;
	vector<vec2> localVerts;
};

struct RenderEnt {
	mat4x4 modelMat; // model matrix for rendering
	vec3 offset; // vertex transformations for picking
	vec3 angles; // vertex transformations for picking
	int modelIdx; // -1 = point entity
	EntCube* pointEntCube;
};

struct RenderGroup {
	lightmapVert* wireframeVerts; // verts for rendering wireframe
	lightmapVert* verts;
	int vertCount;
	int wireframeVertCount;
	Texture* texture;
	Texture* lightmapAtlas[MAXLIGHTMAPS];
	VertexBuffer* buffer;
	VertexBuffer* wireframeBuffer;
	bool transparent;
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
};

struct RenderClipnodes {
	VertexBuffer* clipnodeBuffer[MAX_MAP_HULLS];
	VertexBuffer* wireframeClipnodeBuffer[MAX_MAP_HULLS];
	vector<FaceMath> faceMaths[MAX_MAP_HULLS];
};

struct BSPMODEL;
struct BSPFACE;

struct EntityState {
	int index;
	Entity* ent;
};

class PickInfo {
public:
	vector<int> ents; // selected entity indexes
	vector<int> faces; // selected face indexes

	PickInfo() {}

	Bsp* getMap();
	void selectEnt(int entIdx);
	void selectFace(int faceIdx);
	void deselect();
	void deselectEnt(int entIdx);
	void deselectFace(int faceIdx);
	Entity* getEnt();
	int getEntIndex();
	int getModelIndex();
	BSPMODEL* getModel();
	BSPFACE* getFace();
	int getFaceIndex();
	vec3 getOrigin(); // origin of the selected entity
	bool isFaceSelected(int faceIdx);
	bool isEntSelected(int entIdx);
	vector<Entity*> getEnts();
	vector<BSPFACE*> getFaces();
	bool shouldHideSelection();
};

class Wad;

class BspRenderer {
	friend class Renderer;
public:
	Bsp* map;
	PointEntRenderer* pointEntRenderer;
	vec3 mapOffset;
	int showLightFlag = -1;
	vector<Wad*> wads;

	BspRenderer(Bsp* map, ShaderProgram* bspShader, ShaderProgram* fullBrightBspShader, ShaderProgram* colorShader, PointEntRenderer* fgd);
	~BspRenderer();

	void render(const vector<int>& highlightedEnts, bool highlightAlwaysOnTop, int clipnodeHull, bool transparencyPass);

	void drawModel(int modelIdx, bool transparent, bool highlight, bool edgesOnly);
	void drawModelClipnodes(int modelIdx, bool highlight, int hullIdx);
	void drawPointEntities(const vector<int>& highlightedEnts);

	bool pickPoly(vec3 start, vec3 dir, int hullIdx, int& entIdx, int& faceIdx);
	bool pickModelPoly(vec3 start, vec3 dir, vec3 offset, vec3 rot, int modelIdx, int hullIdx, int testEntidx, int& faceIdx, float& bestDist);
	bool pickFaceMath(vec3 start, vec3 dir, FaceMath& faceMath, float& bestDist);

	void refreshEnt(int entIdx);
	int refreshModel(int modelIdx, bool refreshClipnodes=true);
	bool refreshModelClipnodes(int modelIdx);
	void refreshFace(int faceIdx);
	void refreshPointEnt(int entIdx);
	void updateClipnodeOpacity(byte newValue);

	void reload(); // reloads all geometry, textures, and lightmaps
	void reloadTextures();
	void reloadLightmaps();
	void reloadClipnodes();
	void addClipnodeModel(int modelIdx);
	void updateModelShaders();

	// calculate vertex positions and uv coordinates once for faster rendering
	// also combines faces that share similar properties into a single buffer
	void preRenderFaces();
	void preRenderEnts();
	void calcFaceMaths();

	void loadTextures(); // will reload them if already loaded
	void updateLightmapInfos();
	bool isFinishedLoading();

	void highlightFace(int faceIdx, bool highlight);
	void updateFaceUVs(int faceIdx);
	uint getFaceTextureId(int faceIdx);
	void loadTexture(WADTEX* tex);
	Texture* uploadTexture(WADTEX* tex);

	void write_obj_file();

	void generateSingleLeafNavMeshBuffer(LeafNode* node);

private:
	ShaderProgram* bspShader;
	ShaderProgram* fullBrightBspShader;
	ShaderProgram* colorShader;
	uint colorShaderMultId;

	LightmapInfo* lightmaps = NULL;
	RenderEnt* renderEnts = NULL;
	RenderModel* renderModels = NULL;
	RenderClipnodes* renderClipnodes = NULL;
	FaceMath* faceMaths = NULL;
	VertexBuffer* pointEnts = NULL;

	// textures loaded in a separate thread
	Texture** glTexturesSwap;

	int numLightmapAtlases;
	int numRenderModels;
	int numRenderClipnodes;
	int numRenderLightmapInfos;
	int numFaceMaths;
	int numPointEnts;
	int numLoadedTextures = 0;

	vector<Polygon3D> debugFaces;
	NavMesh* debugNavMesh;


	Texture** glTextures = NULL;
	Texture** glLightmapTextures = NULL;
	Texture* whiteTex = NULL;
	Texture* redTex = NULL;
	Texture* yellowTex = NULL;
	Texture* greyTex = NULL;
	Texture* blackTex = NULL;
	Texture* blueTex = NULL;
	Texture* missingTex = NULL;

	bool lightmapsGenerated = false;
	bool lightmapsUploaded = false;
	future<void> lightmapFuture;

	bool texturesLoaded = false;
	bool textureFacesLoaded = false;
	future<void> texturesFuture;

	bool clipnodesLoaded = false;
	int clipnodeLeafCount = 0;
	future<void> clipnodesFuture;

	void loadLightmaps();
	void genRenderFaces(int& renderModelCount);
	void loadClipnodes();
	void generateClipnodeBuffer(int modelIdx);
	void generateNavMeshBuffer();
	void deleteRenderModel(RenderModel* renderModel);
	void deleteRenderModelClipnodes(RenderClipnodes* renderModel);
	void deleteRenderClipnodes();
	void deleteRenderFaces();
	void deleteTextures();
	void deleteLightmapTextures();
	void deleteFaceMaths();
	void delayLoadData();
	bool getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup);
	int getBestClipnodeHull(int modelIdx);
};