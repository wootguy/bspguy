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

struct RenderFace {
	lightmapVert* verts;
	int vertCount;
	float lightmapScales[MAXLIGHTMAPS];
	Texture* texture;
	Texture* lightmapAtlas[MAXLIGHTMAPS];
};

class BspRenderer {
public:
	BspRenderer(Bsp* map, ShaderProgram* pipeline);
	~BspRenderer();

	void render();
	void renderLightmapFace(int faceIdx);
	void loadTextures();
	void loadLightmaps();

	// calculate vertex positions and uv coordinates once for faster rendering
	void preRenderFaces();

private:
	Bsp* map;
	Texture** glTextures;
	Texture** glLightmapTextures;
	LightmapInfo* lightmaps;
	RenderFace* renderFaces;
	ShaderProgram* pipeline;
	Texture* whiteTex;

	VertexBuffer* faceBuffer;
	int sTexId;
	int sLightmapTexIds[MAXLIGHTMAPS];
	int lightmapScaleIds[MAXLIGHTMAPS];
};