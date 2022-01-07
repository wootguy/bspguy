#pragma once
#include "Bsp.h"
#include "Texture.h"
#include "ShaderProgram.h"
#include "LightmapNode.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "PointEntRenderer.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

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
	RENDER_ENT_CONNECTIONS = 1024
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
	vec3 normal;
	float fdist;
	std::vector<vec2> localVerts;
};

struct RenderEnt {
	mat4x4 modelMat; // model matrix for rendering with angles
	mat4x4 modelMatOrigin; // model matrix for render origin
	vec3 offset; // vertex transformations for picking
	vec3 angles; // support angles
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
	int groupCount;
	int renderFaceCount;
	RenderFace* renderFaces;
	RenderGroup* renderGroups;
};

struct RenderClipnodes {
	VertexBuffer* clipnodeBuffer[MAX_MAP_HULLS];
	VertexBuffer* wireframeClipnodeBuffer[MAX_MAP_HULLS];
	std::vector<FaceMath> faceMaths[MAX_MAP_HULLS];
};

struct PickInfo {
	int entIdx;
	int modelIdx;
	int faceIdx;
	float bestDist;
	Entity* ent;
	Bsp* map;
	PickInfo()
	{
		bestDist = 0.0f;
		entIdx = modelIdx = faceIdx = -1;
		ent = NULL;
		map = NULL;
	}
};

class BspRenderer {
public:
	Bsp* map;
	PointEntRenderer* pointEntRenderer;
	vec3 mapOffset;
	int showLightFlag = -1;
	std::vector<Wad*> wads;
	bool texturesLoaded = false;


	BspRenderer(Bsp* map, ShaderProgram* bspShader, ShaderProgram* fullBrightBspShader, ShaderProgram* colorShader, PointEntRenderer* pointEntRenderer);
	~BspRenderer();

	void render(int highlightEnt, bool highlightAlwaysOnTop, int clipnodeHull);

	void drawModel(RenderEnt* ent, bool transparent, bool highlight, bool edgesOnly);
	void drawModelClipnodes(int modelIdx, bool highlight, int hullIdx);
	void drawPointEntities(int highlightEnt);

	bool pickPoly(vec3 start, vec3 dir, int hullIdx, PickInfo& pickInfo);
	bool pickModelPoly(vec3 start, vec3 dir, vec3 offset, int modelIdx, int hullIdx, PickInfo& pickInfo);
	bool pickFaceMath(vec3 start, vec3 dir, FaceMath& faceMath, float& bestDist);

	void refreshEnt(int entIdx);
	int refreshModel(int modelIdx, bool refreshClipnodes = true);
	bool refreshModelClipnodes(int modelIdx);
	void refreshFace(int faceIdx);
	void refreshPointEnt(int entIdx);
	void updateClipnodeOpacity(unsigned char newValue);

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
	unsigned int getFaceTextureId(int faceIdx);

	bool getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup);

	void ReuploadTextures();
private:
	ShaderProgram* bspShader;
	ShaderProgram* fullBrightBspShader;
	ShaderProgram* colorShader;
	unsigned int colorShaderMultId;

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
	std::future<void> lightmapFuture;

	std::future<void> texturesFuture;

	bool clipnodesLoaded = false;
	int clipnodeLeafCount = 0;
	std::future<void> clipnodesFuture;

	void loadLightmaps();
	void genRenderFaces(int& renderModelCount);
	void loadClipnodes();
	void generateClipnodeBuffer(int modelIdx);
	void deleteRenderModel(RenderModel* renderModel);
	void deleteRenderModelClipnodes(RenderClipnodes* renderClip);
	void deleteRenderClipnodes();
	void deleteRenderFaces();
	void deleteTextures();
	void deleteLightmapTextures();
	void deleteFaceMaths();
	void delayLoadData();
	int getBestClipnodeHull(int modelIdx);
};