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
	mat4x4 modelMat;
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

	void drawModel(int modelIdx, bool transparent);

private:
	Bsp* map;
	Texture** glTextures;
	Texture** glLightmapTextures;
	LightmapInfo* lightmaps;
	RenderEnt* renderEnts;
	ShaderProgram* pipeline;
	Texture* whiteTex;

	RenderModel* renderModels;
};