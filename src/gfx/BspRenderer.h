#pragma once
#include "Bsp.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Texture.h"
#include "ShaderProgram.h"
#include "LightmapNode.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "PointEntRenderer.h"

#define LIGHTMAP_ATLAS_SIZE 512

enum RenderFlags {
	RENDER_TEXTURES = 1,
	RENDER_LIGHTMAPS = 2,
	RENDER_WIREFRAME = 4,
	RENDER_ENTS = 8,
	RENDER_SPECIAL = 16,
	RENDER_SPECIAL_ENTS = 32,
	RENDER_POINT_ENTS = 64,

	RENDER_ORIGIN = 128
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

struct RenderEnt {
	mat4x4 modelMat; // model matrix for rendering
	vec3 offset; // vertex transformations for picking
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

struct FaceMath {
	mat4x4 worldToLocal; // transforms world coordiantes to this face's plane's coordinate system
	vec3 normal;
	float fdist;
	vec3* verts = NULL; // skips the edge lookups
	int vertCount;
};

struct PickInfo {
	int mapIdx;
	int entIdx;
	int modelIdx;
	int faceIdx;
	float bestDist;
	bool valid;
	Bsp* map = NULL;
	Entity* ent = NULL;
};

class BspRenderer {
public:
	Bsp* map;
	PointEntRenderer* pointEntRenderer;

	BspRenderer(Bsp* map, ShaderProgram* bspShader, ShaderProgram* fullBrightBspShader, ShaderProgram* colorShader, PointEntRenderer* fgd);
	~BspRenderer();

	void render(int highlightEnt, bool highlightAlwaysOnTop);

	void drawModel(int modelIdx, bool transparent, bool highlight, bool edgesOnly);
	void drawPointEntities(int highlightEnt);

	bool pickPoly(vec3 start, vec3 dir, PickInfo& pickInfo);
	bool pickPoly(vec3 start, vec3 dir, vec3 offset, int modelIdx, PickInfo& pickInfo);

	void refreshEnt(int entIdx);
	int refreshModel(int modelIdx, RenderModel* renderModel=NULL);
	void refreshFace(int faceIdx);
	void refreshPointEnt(int entIdx);

	void reload(); // reloads all geometry, textures, and lightmaps
	void reloadTextures();
	void reloadLightmaps();
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

private:
	ShaderProgram* bspShader;
	ShaderProgram* fullBrightBspShader;
	ShaderProgram* colorShader;

	LightmapInfo* lightmaps = NULL;
	RenderEnt* renderEnts = NULL;
	RenderModel* renderModels = NULL;
	FaceMath* faceMaths = NULL;
	VertexBuffer* pointEnts = NULL;

	// textures loaded in a separate thread
	Texture** glTexturesSwap;

	int numLightmapAtlases;
	int numRenderModels;
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
	future<void> lightmapFuture;

	bool texturesLoaded = false;
	future<void> texturesFuture;

	void loadLightmaps();
	RenderModel* genRenderFaces(int& renderModelCount);
	void deleteRenderModel(RenderModel* renderModel);
	void deleteRenderFaces();
	void deleteTextures();
	void deleteLightmapTextures();
	void deleteFaceMaths();
	void delayLoadData();
	bool getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup);
};