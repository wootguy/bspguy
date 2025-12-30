#pragma once
#include <string>
#include "ShaderProgram.h"
#include "Entity.h"
#include "ThreadSafeInt.h"
#include "util.h"

enum render_modes {
	RENDER_MODE_NORMAL,
	RENDER_MODE_COLOR,
	RENDER_MODE_TEXTURE,
	RENDER_MODE_GLOW,
	RENDER_MODE_SOLID,
	RENDER_MODE_ADDITIVE,
};

enum MODEL_LOAD_STATE {
	MODEL_LOAD_WAITING,
	MODEL_LOAD_INITIAL,
	MODEL_LOAD_UPLOAD,
	MODEL_LOAD_DONE
};

class BaseRenderer {
public:
	string fpath;
	volatile bool valid;
	volatile int loadState;

	BaseRenderer() { loadState = 0; valid = false; }
	virtual ~BaseRenderer() {}

	virtual void upload() = 0;

	// get intersection of pick ray and model polygon
	virtual bool pick(vec3 start, vec3 rayDir, Entity* ent, float& bestDist) = 0;

	virtual bool pick(Frustum& frustum, Entity* ent) = 0;
	
	virtual bool isStudioModel() { return false;  }
	virtual bool isSprite() { return false; }
	virtual void loadData() = 0;
};

// used to prevent too many models loading at once and freezing the program
extern ThreadSafeInt g_loading_models;