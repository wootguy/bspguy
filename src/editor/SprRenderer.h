#pragma once
#include <string>
#include "util.h"
#include "mstream.h"
#include <stdint.h>
#include "Texture.h"
#include "VertexBuffer.h"
#include "BaseRenderer.h"
#include "Entity.h"
#include "colors.h"

enum SPR_LOAD_STATE {
	SPR_LOAD_INITIAL,
	SPR_LOAD_UPLOAD,
	SPR_LOAD_DONE
};

enum sprite_formats
{
	SPR_NORMAL,
	SPR_ADDITIVE,
	SPR_INDEXALPHA,
	SPR_ALPHATEST
};

enum sprite_modes
{
	VP_PARALLEL_UPRIGHT,
	FACING_UPRIGHT,
	VP_PARALLEL,
	ORIENTED,
	VP_PARALLEL_ORIENTED
};

#pragma pack(push, 1)
struct SpriteHeader
{
	char ident[4];		// should always be "IDSP"
	uint32_t version;		// should be 2
	uint32_t mode;		// see sprite_modes enum
	uint32_t format;		// see sprite_formats enum
	float radius;		// ???
	uint32_t width;
	uint32_t height;
	uint32_t frames;
	float beamLength;	// for beam effects?
	uint32_t syncType;	// 0 = synchronized, 1 = random
	uint16_t paletteSz;	// always 256??
};

struct FrameHeader
{
	uint32_t group;
	int32_t x;		// sprite offset?
	int32_t y;
	uint32_t width;
	uint32_t height;
};

// Tip of the day: if you mistype this as "#pragma pop" or something you will get
// stack corruption which is very confusing to debug.
#pragma pack(pop) 

class SprRenderer : public BaseRenderer {
public:
	SprRenderer(ShaderProgram* frameShader, ShaderProgram* outlineShader, string sprPath);
	~SprRenderer();

	void upload() override;
	void draw(vec3 ori, vec3 angles, EntRenderOpts opts, bool selected);
	void getBoundingBox(vec3& mins, vec3& maxs, float scale);
	bool pick(vec3 start, vec3 rayDir, Entity* ent, float& bestDist) override;

	bool isSprite() override { return true; };

private:
	SpriteHeader* header;

	bool validate();
	void loadData();

	ShaderProgram* frameShader;
	ShaderProgram* outlineShader;
	Texture** glTextures;
	VertexBuffer* frameBuffer;
	VertexBuffer* outlineBuffer;

	// shader uniforms
	uint u_color_frame;
	uint u_color_outline;

	mstream data;

	float maxCoord; // max distance a vertex is from its origin
};