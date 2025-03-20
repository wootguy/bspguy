#pragma once
#include <string>
#include "ShaderProgram.h"
#include "Entity.h"

enum render_modes {
	RENDER_MODE_NORMAL,
	RENDER_MODE_COLOR,
	RENDER_MODE_TEXTURE,
	RENDER_MODE_GLOW,
	RENDER_MODE_SOLID,
	RENDER_MODE_ADDITIVE,
};

class BaseRenderer {
public:
	string fpath;
	volatile bool valid;
	volatile int loadState;

	// convenience state for rendering
	float lastDrawCall = 0;
	float drawFrame = 0;

	BaseRenderer() { loadState = 0; valid = false; }
	virtual ~BaseRenderer() {}

	virtual void upload() = 0;

	// get intersection of pick ray and model polygon
	virtual bool pick(vec3 start, vec3 rayDir, Entity* ent, float& bestDist) = 0;
	
	virtual bool isStudioModel() { return false;  }
	virtual bool isSprite() { return false; }
};