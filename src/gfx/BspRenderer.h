#include "Bsp.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Texture.h"
#include "ShaderProgram.h"
#include "LightmapNode.h"
#include "VertexBuffer.h"
#include "primitives.h"

#define LIGHTMAP_ATLAS_SIZE 512

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
	int modelIdx;
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

struct RenderModel {
	RenderGroup* renderGroups;
	int groupCount;
};

struct FaceMath {
	mat4x4 worldToLocal; // transforms world coordiantes to this face's plane's coordinate system
	vec3 normal;
	float fdist;
	vec3* verts; // skips the edge lookups
	int vertCount;
};

class BspRenderer {
public:
	BspRenderer(Bsp* map, ShaderProgram* pipeline);
	~BspRenderer();

	void render();
	void loadTextures();
	void loadLightmaps();

	// calculate vertex positions and uv coordinates once for faster rendering
	// also combines faces that share similar properties into a single buffer
	void preRenderFaces();

	void preRenderEnts();

	void calcFaceMaths();

	void drawModel(int modelIdx, bool transparent);

	float pickPoly(vec3 start, vec3 dir);
	void pickPoly(vec3 start, vec3 dir, vec3 offset, int modelIdx, float& bestDist);

private:
	Bsp* map;
	Texture** glTextures;
	Texture** glLightmapTextures;
	LightmapInfo* lightmaps;
	RenderEnt* renderEnts;
	ShaderProgram* pipeline;
	Texture* whiteTex;
	FaceMath* faceMaths;

	RenderModel* renderModels;
};