#pragma once
#include "Keyvalue.h"
#include "types.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "mat4x4.h"
#include "colors.h"

class Bsp;
class BaseRenderer;

enum anglesKeyBehavior {
	ANGLES_ROTATE,
	ANGLES_DIRECTIONAL,
	ANGLES_AMBIGUOUS
};

struct EntRenderOpts {
	uint8_t rendermode;
	uint8_t renderamt;
	COLOR3 rendercolor;
	float framerate;
	float scale;
	int vp_type;
};

class Entity
{
public:
	vector<string> keyOrder;
	
	// model rendering state updated whenever drawCached is false
	bool drawCached; // origin, angles, sequence, and model are cached?
	BaseRenderer* cachedMdl = NULL;
	string cachedMdlCname; // classname that was used to load the model
	bool hasCachedMdl = false;
	bool didStudioDraw = false;
	vec3 drawAngles;
	vec3 drawOrigin;
	vec3 drawMin, drawMax; // model bounding box
	int drawSequence;
	float drawFrame;

	Entity(void);
	Entity(const std::string& classname);
	~Entity(void);

	string getKeyvalue(string key);
	unordered_map<string, string> getAllKeyvalues();
	void addKeyvalue(Keyvalue& k);
	void addKeyvalue(const std::string& key, const std::string& value);
	void removeKeyvalue(const std::string& key);
	bool renameKey(string oldName, string newName);
	void clearAllKeyvalues();
	void clearEmptyKeyvalues();

	void setOrAddKeyvalue(const std::string& key, const std::string& value);

	// returns -1 for invalid idx
	int getBspModelIdx();

	bool isBspModel();

	string getTargetname();
	string getClassname();

	vec3 getOrigin();

	vec3 getAngles();

	EntRenderOpts getRenderOpts();

	mat4x4 getRotationMatrix(bool flipped);

	// true if this type of entity can be rotated by its angles keyvalue
	bool canRotate();

	vec3 getHullOrigin(Bsp* map);

	bool hasKey(const std::string& key);

	unordered_set<string> getTargets();

	bool hasTarget(string tname);

	void renameTargetnameValues(string oldTargetname, string newTargetname);

	int getMemoryUsage(); // aproximate

	bool isEverVisible();

	string serialize();

	void clearCache();

private:
	unordered_map<string, string> keyvalues;

	int cachedModelIdx = -2; // -2 = not cached
	unordered_set<string> cachedTargets;
	bool targetsCached = false;
	bool hasCachedTargetname = false;
	bool hasCachedClassname = false;
	bool hasCachedOrigin = false;
	bool hasCachedAngles = false;
	bool hasCachedRenderOpts = false;
	string cachedTargetname;
	string cachedClassname;
	vec3 cachedOrigin;
	vec3 cachedAngles;
	EntRenderOpts cachedRenderOpts;
};

