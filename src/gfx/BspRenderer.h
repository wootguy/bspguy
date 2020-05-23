#include "Bsp.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Texture.h"
#include "ShaderProgram.h"
#include "LightmapNode.h"
#include "VertexBuffer.h"

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

class BspRenderer {
public:
	BspRenderer(Bsp* map, ShaderProgram* pipeline);
	~BspRenderer();

	void render();
	void renderLightmapFace(Bsp* map, int faceIdx);

private:
	Bsp* map;
	Texture** glTextures;
	Texture** glLightmapTextures;
	LightmapInfo* lightmaps;
	ShaderProgram* pipeline;
	Texture* whiteTex;

	VertexBuffer* faceBuffer;
	int sTexId;
	int sLightmapTexIds[MAXLIGHTMAPS];
	int lightmapScaleIds[MAXLIGHTMAPS];
};