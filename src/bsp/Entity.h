#pragma once
#include "Keyvalue.h"
#include "types.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "mat4x4.h"
#include "colors.h"
#include "HashMap.h"

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
	int body;
	int skin;
	int sequence;
};

typedef uint16_t string_t;
extern unordered_map<string, uint16_t> g_stringMap; // maps a string to an integer for smaller connection graphs
void clearStringMap();

class Entity
{
public:
	vector<string> keyOrder;
	string cachedAllKvStr; // full list of keys and values for updating the entity lump faster
	bool hidden = false; // hidden in the 3d view
	bool highlighted = false; // temporary within a single render call only

	// model rendering state updated whenever drawCached is false
	bool drawCached; // origin, angles, sequence, and model are cached?
	BaseRenderer* cachedMdl = NULL;
	string cachedMdlCname; // classname that was used to load the model
	COLOR3 cachedFgdTint;
	bool hasCachedMdl = false;
	bool didStudioDraw = false;
	bool isIconSprite = false;
	vec3 drawAngles;
	vec3 drawOrigin;
	vec3 drawMin, drawMax; // model bounding box
	float drawFrame;
	float lastDrawCall;

	Entity(void);
	Entity(const std::string& classname);
	~Entity(void);

	const string getKeyvalue(string key);
	StringMap getAllKeyvalues();
	void removeKeyvalue(const std::string& key);
	bool renameKey(string oldName, string newName);
	void clearAllKeyvalues();
	void clearEmptyKeyvalues();

	void setOrAddKeyvalue(const std::string& key, const std::string& value);

	// returns -1 for invalid idx
	int getBspModelIdx();

	bool isBspModel();

	bool isSprite();

	string getTargetname();
	const StringSet& getAllTargetnames(); // includes target_source/target_name_or_class keys in the FGD
	string getClassname();

	string getFullKvString();

	vec3 getOrigin();

	vec3 getAngles();

	vec3 getVisualAngles(); // angles for displaying models

	EntRenderOpts getRenderOpts();

	const mat4x4& getRotationMatrix(bool flipped);

	// color assigned by color255 key in the FGD (lights)
	COLOR3 getFgdTint();

	// true if this type of entity can be rotated by its angles keyvalue
	bool canRotate();

	bool shouldDisplayDirectionVector();

	vec3 getHullOrigin(Bsp* map);

	bool hasKey(const std::string& key);

	const StringSet& getTargets();

	bool hasTarget(string tname);

	bool hasTarget(const StringSet& checkNames);

	void renameTargetnameValues(string oldTargetname, string newTargetname);

	int getMemoryUsage(); // aproximate

	bool isEverVisible();

	string serialize(bool serializeBspModel=false);

	bool deserialize();

	void clearCache();

private:
	StringMap keyvalues;

	int cachedModelIdx = -2; // -2 = not cached
	StringSet cachedTargets;
	StringSet cachedTargetnames;
	bool targetsCached = false;
	bool hasCachedTargetname = false;
	bool hasCachedClassname = false;
	bool hasCachedOrigin = false;
	bool hasCachedAngles = false;
	bool hasCachedRenderOpts = false;
	bool hasCachedRotMatrixes = false;
	bool hasCachedFgdTint = false;
	bool hasCachedTargetnames = false;
	string cachedTargetname;
	string cachedClassname;
	vec3 cachedOrigin;
	vec3 cachedAngles;
	mat4x4 cachedRotationMatrix;
	mat4x4 cachedRotationMatrixFlipped;
	EntRenderOpts cachedRenderOpts;
};

