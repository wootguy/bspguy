#pragma once
#include "Keyvalue.h"
#include "types.h"
#include <unordered_map>
#include <vector>
#include "mat4x4.h"

class Bsp;
class MdlRenderer;

enum anglesKeyBehavior {
	ANGLES_ROTATE,
	ANGLES_DIRECTIONAL,
	ANGLES_AMBIGUOUS
};

class Entity
{
public:
	unordered_map<string, string> keyvalues;
	vector<string> keyOrder;

	int cachedModelIdx = -2; // -2 = not cached
	vector<string> cachedTargets;
	bool targetsCached = false;
	
	// model rendering state updated whenever drawCached is false
	bool drawCached; // origin, angles, sequence, and model are cached?
	MdlRenderer* cachedMdl = NULL;
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

	vec3 getOrigin();

	vec3 getAngles();

	mat4x4 getRotationMatrix(bool flipped);

	// true if this type of entity can be rotated by its angles keyvalue
	bool canRotate();

	vec3 getHullOrigin(Bsp* map);

	bool hasKey(const std::string& key);

	vector<string> getTargets();

	bool hasTarget(string tname);

	void renameTargetnameValues(string oldTargetname, string newTargetname);

	int getMemoryUsage(); // aproximate

	bool isEverVisible();

	string serialize();

	void clearCache();
};

