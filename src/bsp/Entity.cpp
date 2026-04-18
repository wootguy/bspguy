#include "Entity.h"
#include "util.h"
#include <set>
#include <algorithm>
#include <sstream>
#include "Bsp.h"
#include "globals.h"
#include "Editor.h"
#include <unordered_set>
#include "Fgd.h"
#include "BspRenderer.h"

using namespace std;

unordered_map<string, uint16_t> g_stringMap;
vector<string> g_stringMapInv;
int g_stringId = 1;

/*
string_t make_string_t(string s) {
	string_t& item = g_stringMap[s];

	if (!item) {
		item = g_stringId++;
		g_stringMapInv.push_back(s);

		if (g_stringId >= 65535) {
			logf("Too many strings in string map! Entity connections will be broken\n");
		}
	}

	return item;
}

string_t to_string_t(string s) {
	auto item = g_stringMap.find(s);
	if (item != g_stringMap.end())
		return item->second;
	return 0;
}

string get_string_t(string_t id) {
	if (id-1 < g_stringMapInv.size())
		return g_stringMapInv[id];
	return "";
}

void clearStringMap() {
	g_stringMap.clear();
	g_stringMapInv.clear();
	g_stringId = 1;

	// invalidate connections that use these ids
	for (Entity* ent : g_app->mapRenderer->map->ents) {
		ent->clearCache();
	}
}
*/

void clearStringMap() {}

Entity::Entity(void)
{
}

Entity::Entity(const string& classname)
{
	setOrAddKeyvalue("classname", classname);
}

Entity::~Entity(void)
{
}

const string Entity::getKeyvalue(string key) {
	const char* found = keyvalues.get(key.c_str());
	return found ? found : "";
}

StringMap Entity::getAllKeyvalues() {
	return keyvalues;
}

void Entity::setOrAddKeyvalue(const std::string& key, const std::string& value) {
	clearCache();

	const char* found = keyvalues.get(key.c_str());
	if (found) {
		keyvalues.put(key.c_str(), value.c_str());
		return;
	}

	keyOrder.push_back(key);
	keyvalues.put(key.c_str(), value.c_str());
}

void Entity::removeKeyvalue(const std::string& key) {
	if (!hasKey(key))
		return;
	auto it = find(keyOrder.begin(), keyOrder.end(), key);
	if (it != keyOrder.end())
		keyOrder.erase(it);
	else {
		logf("Desync between keyorder and keyvalues!\n");
	}
	keyvalues.del(key.c_str());
	clearCache();
}

bool Entity::renameKey(string oldName, string newName) {
	int hasKey = -1;
	for (int i = 0; i < keyOrder.size(); i++) {
		if (keyOrder[i] == oldName) {
			hasKey = i;
		}
		if (keyOrder[i] == newName) {
			return false;
		}
	}
	if (hasKey == -1 || newName.empty() || newName == oldName) {
		return false;
	}
	
	string oldVal = keyvalues.get(oldName.c_str());
	keyOrder[hasKey] = newName;
	keyvalues.del(oldName.c_str());
	keyvalues.put(newName.c_str(), oldVal.size() ? oldVal.c_str() : "");
	clearCache();
	return true;
}

void Entity::clearAllKeyvalues() {
	keyOrder.clear();
	keyvalues.clear();
	cachedAllKvStr = "";
	cachedModelIdx = -2;
}

void Entity::clearEmptyKeyvalues() {
	vector<string> newKeyOrder;
	StringMap newKeyvalues;
	for (int i = 0; i < keyOrder.size(); i++) {
		const char* val = keyvalues.get(keyOrder[i].c_str());
		if (val && val[0]) {
			newKeyOrder.push_back(keyOrder[i]);
			newKeyvalues.put(keyOrder[i].c_str(), val);
		}
	}
	keyOrder = newKeyOrder;
	keyvalues = newKeyvalues;
	clearCache();
}

bool Entity::hasKey(const std::string& key)
{
	return keyvalues.get(key.c_str());
}

int Entity::getBspModelIdx() {
	if (cachedModelIdx != -2) {
		return cachedModelIdx;
	}

	if (!hasKey("model")) {
		cachedModelIdx = -1;
		return -1;
	}

	string model = getKeyvalue("model");
	if (model.size() <= 1 || model[0] != '*') {
		cachedModelIdx = -1;
		return -1;
	}

	string modelIdxStr = model.substr(1);
	if (!isNumeric(modelIdxStr)) {
		cachedModelIdx = -1;
		return -1;
	}
	cachedModelIdx = atoi(modelIdxStr.c_str());
	return cachedModelIdx;
}

bool Entity::isBspModel() {
	return getBspModelIdx() >= 0;
}

bool Entity::isSprite() {
	string model = getKeyvalue("model");
	int ext = model.find(".spr");
	return ext != -1 && ext == model.size() - 4;
}

string Entity::getTargetname() {
	if (hasCachedTargetname) {
		return cachedTargetname;
	}

	const char* kv = keyvalues.get("targetname");
	if (!kv) {
		return "";
	}

	cachedTargetname = kv;
	hasCachedTargetname = true;

	return cachedTargetname;
}

const StringSet& Entity::getAllTargetnames() {
	if (hasCachedTargetnames) {
		return cachedTargetnames;
	}

	StringSet tnameKeys = { "targetname" };
	cachedTargetnames.clear();

	FgdClass* fgd = g_app->mergedFgd ? g_app->mergedFgd->getFgdClass(getClassname()) : NULL;
	if (fgd) {
		for (KeyvalueDef& def : fgd->keyvalues) {
			if (def.iType == FGD_KEY_TARGET_SRC) {
				tnameKeys.put(def.name.c_str());
			}
		}
	}

	StringSet::iterator_t iter;
	while (tnameKeys.iterate(iter)) {
		string val = getKeyvalue(iter.key);
		if (val.size()) {
			cachedTargetnames.put(val.c_str());
		}
	}

	hasCachedTargetnames = true;

	return cachedTargetnames;
}

string Entity::getClassname() {
	if (hasCachedClassname) {
		return cachedClassname;
	}

	const char* kv = keyvalues.get("classname");
	if (!kv) {
		return "";
	}

	cachedClassname = kv;
	hasCachedClassname = true;

	return cachedClassname;
}

string Entity::getFullKvString() {
	if (cachedAllKvStr.size()) {
		return cachedAllKvStr;
	}

	for (int k = 0; k < keyOrder.size(); k++) {
		const char* val = keyvalues.get(keyOrder[k].c_str());
		cachedAllKvStr += "\"" + keyOrder[k] + "\" \"" + (val ? val : "") + "\"\n";
	}

	return cachedAllKvStr;
}

vec3 Entity::getOrigin() {
	if (hasCachedOrigin) {
		return cachedOrigin;
	}

	const char* kv = keyvalues.get("origin");
	if (!kv) {
		cachedOrigin = vec3();
	}
	else {
		cachedOrigin = parseVector(kv);
	}

	hasCachedOrigin = true;
	return cachedOrigin;
}

vec3 Entity::getAngles() {
	if (hasCachedAngles) {
		return cachedAngles;
	}

	const char* kv = keyvalues.get("angles");
	cachedAngles = kv ? parseVector(kv) : vec3();

	kv = keyvalues.get("angle");
	if (kv) {
		float angle = atof(kv);

		if (angle >= 0) {
			cachedAngles.y = angle;
		}
		else if ((int)angle == -1) {
			cachedAngles = vec3(-90, 0, 0);
		}
		else {
			cachedAngles = vec3(90, 0, 0);
		}
	}

	hasCachedAngles = true;
	return cachedAngles;
}

vec3 Entity::getVisualAngles() {
	vec3 angles = getAngles();

	string cname = getClassname();

	if (getBspModelIdx() != -1 || cname.find("info_player_") == 0) {
		angles.x *= -1;
	}

	static unordered_set<string> pitch_angle_classnames = {
		"light_environment",
		"light_spot",
		"info_sunlight"
	};

	if (pitch_angle_classnames.count(cname)) {
		string pitchkey = getKeyvalue("pitch");
		if (pitchkey.size())
			angles.x += atof(pitchkey.c_str());
	}

	return angles;
}

const EntRenderOpts& Entity::getRenderOpts() {
	if (hasCachedRenderOpts) {
		return cachedRenderOpts;
	}

	const char* kv = keyvalues.get("rendermode");
	cachedRenderOpts.rendermode = !kv ? 0 : atoi(kv);

	kv = keyvalues.get("renderamt");
	cachedRenderOpts.renderamt = !kv ? 0 : atoi(kv);

	kv = keyvalues.get("rendercolor");
	cachedRenderOpts.rendercolor = !kv ? COLOR3(0,0,0) : parseColor(kv);

	kv = keyvalues.get("framerate");
	cachedRenderOpts.framerate = !kv ? 0.0f : atof(kv);

	kv = keyvalues.get("scale");
	cachedRenderOpts.scale = !kv ? 1.0f : atof(kv);

	kv = keyvalues.get("vp_type");
	cachedRenderOpts.vp_type = !kv ? 0 : atoi(kv);

	kv = keyvalues.get("new_body");
	cachedRenderOpts.body = !kv ? 0 : atoi(kv);
	kv = keyvalues.get("body");
	cachedRenderOpts.body = !kv ? cachedRenderOpts.body : atoi(kv);

	kv = keyvalues.get("new_skin");
	cachedRenderOpts.skin = !kv ? 0 : atoi(kv);
	kv = keyvalues.get("skin");
	cachedRenderOpts.skin = !kv ? cachedRenderOpts.skin : atoi(kv);

	kv = keyvalues.get("sequence");
	cachedRenderOpts.sequence = !kv ? 0 : atoi(kv);

	hasCachedRenderOpts = true;
	return cachedRenderOpts;
}

const mat4x4& Entity::getRotationMatrix(bool flipped) {
	if (hasCachedRotMatrixes) { // TODO: force rotate messing this up i think
		return flipped ? cachedRotationMatrixFlipped : cachedRotationMatrix;
	}

	cachedRotationMatrix.loadIdentity();
	cachedRotationMatrixFlipped.loadIdentity();

	if (canRotate()) {
		vec3 angles = getVisualAngles() * (PI / 180.0f);
		bool isBspModel = getBspModelIdx() != -1;

		if (angles != vec3()) {

			// well this makes no sense but it's required for object picking
			// but not for rendering. I guess it's a combination of flips or undoing them, idk
			if (isBspModel) {
				cachedRotationMatrixFlipped.rotateX(-angles.z);
				cachedRotationMatrixFlipped.rotateY(angles.x);
				cachedRotationMatrixFlipped.rotateZ(-angles.y);
			}
			else {
				cachedRotationMatrixFlipped.rotateY(angles.x);
				cachedRotationMatrixFlipped.rotateZ(-angles.y);
				cachedRotationMatrixFlipped.rotateX(-angles.z);
			}
			if (isBspModel) {
				cachedRotationMatrix.rotateY(angles.y);
				cachedRotationMatrix.rotateZ(angles.x);
				cachedRotationMatrix.rotateX(angles.z);
			}
			else {
				cachedRotationMatrix.rotateX(angles.z);
				cachedRotationMatrix.rotateY(angles.y);
				cachedRotationMatrix.rotateZ(angles.x);
			}
		}
	}

	hasCachedRotMatrixes = true;
	return flipped ? cachedRotationMatrixFlipped : cachedRotationMatrix;
}

COLOR3 Entity::getFgdTint() {
	if (hasCachedFgdTint) {
		return cachedFgdTint;
	}

	if (!g_app->mergedFgd) {
		return COLOR3(255, 255, 255);
	}

	FgdClass* fgd = g_app->mergedFgd->getFgdClass(getClassname());

	if (fgd->iconColorKey.empty()) {
		return COLOR3(255, 255, 255);
	}

	string val = getKeyvalue(fgd->iconColorKey);
	if (val.empty()) {
		return COLOR3(255, 255, 255);
	}

	cachedFgdTint = parseColor(val);
	hasCachedFgdTint = true;
	return cachedFgdTint;
}

bool Entity::canRotate() {
	if (g_app->forceAngleRotation) {
		return true;
	}

	static unordered_set<string> rotatable_classnames = {
		"func_rotating",
		"func_rot_button",
		"func_door_rotating",
		"func_pendulum",
		"func_tank",
		"func_tanklaser",
		"func_tankmortar",
		"func_tankrocket",
		"func_train",
		"func_tracktrain",
		"momentary_rot_button",
	};

	if (!isIconSprite && getBspModelIdx() == -1) {
		return true;
	}

	string cname = getClassname();

	if (cname.empty())
		return false;

	if (rotatable_classnames.count(cname)) {
		return true;
	}

	if (cname == "func_wall" || cname == "func_illusionary") {
		int spawnflags = atoi(getKeyvalue("spawnflags").c_str());
		return spawnflags & 2; // "Use angles" key
	}

	return false;
}

bool Entity::shouldDisplayDirectionVector() {
	if (!(g_settings.render_flags & RENDER_ENT_DIRECTIONS)) {
		return false;
	}

	// don't show vectors for point entities or solids that can rotate normally
	// don't show for sprites either unless force rotation is on (the vector makes no sense)
	if ((!isBspModel() || !canRotate()) && (!isSprite() || g_app->forceAngleRotation)) {
		string cname = getClassname();
		FgdClass* clazz = g_app->mergedFgd ? g_app->mergedFgd->getFgdClass(cname) : NULL;
		// show if the FGD says the ent uses angles, or if the fgd is missing and the ent has angles,
		// or if force angles are on
		bool classUsesAngle = clazz ? (clazz->hasKey("angles") || clazz->hasKey("angle")) : false;
		if (classUsesAngle || (!clazz && (hasKey("angles") || hasKey("angle"))) || g_app->forceAngleRotation) {
			return true;
		}
	}

	return false;
}

vec3 Entity::getHullOrigin(Bsp* map) {
	vec3 ori = getOrigin();
	int modelIdx = getBspModelIdx();

	if (modelIdx != -1) {
		BSPMODEL& model = map->models[modelIdx];

		vec3 mins, maxs;
		map->get_model_vertex_bounds(modelIdx, mins, maxs);
		vec3 modelCenter = (maxs + mins) * 0.5f;

		ori += modelCenter;
	}

	return ori;
}

// this is here because the command-line merger doesn't load any FGDs
// and also because the sven co-op FGD doesn't mark target keys properly.
// This should be removed once the sven co-op FGD keys have the proper types.
#define TOTAL_TARGETNAME_KEYS 134
const char* potential_targetname_keys[TOTAL_TARGETNAME_KEYS] = {
	// common target-related keys
	"targetname",
	"target",
	"killtarget",
	"master",
	"netname",
	"message", // not always an entity, but unlikely a .wav file or something will match an entity name

	// monster_* and monster spawners
	"TriggerTarget",
	"path_name",
	"guard_ent",
	"trigger_target",
	"xenmaker",
	"squadname",

	// OpenClosable
	"fireonopening",
	"fireonclosing",
	"fireonopened",
	"fireonclosed",

	// breakables
	"fireonbreak",

	// Trackchange
	"train",
	"toptrack",
	"bottomtrack",

	// scripted sequences
	"m_iszEntity",
	"entity",
	//"listener", // TODO: what is this?

	// BaseCharger
	"TriggerOnEmpty",
	"TriggerOnRecharged",

	// Beams
	"LightningStart",
	"LightningEnd",
	"LaserTarget",
	"laserentity",

	// func_rot_button
	"changetarget",

	// game_zone_player
	"intarget",
	"outtarget",

	// info_bigmomma
	"reachtarget",
	"presequence",

	// info_monster_goal
	"enemy",

	// path_condition_controller
	"conditions_reference",
	"starttrigger",
	// TODO: support lists of targetnames
	//"pathcondition_list",
	//"waypoint_list",
	//"m_szASConditionsName", // TODO: what is this?

	// path_waypoint
	"alternate_target",
	"trigger_on_arrival",
	"trigger_after_arrival",
	"wait_master",
	"trigger_on_departure",
	"overflow_waypoint",
	"stop_trigger",

	// path_track
	"altpath",

	// trigger_camera + trigger_cameratarget
	"moveto",
	// TODO: parameters are not always entities(?)
	"mouse_param_0_0",
	"mouse_param_0_1",
	"mouse_param_1_0",
	"mouse_param_1_1",
	"mouse_param_2_0",
	"mouse_param_2_1",
	"m_iszOverridePlayerTargetname",
	"m_iszTargetWhenPlayerStartsUsing",
	"m_iszTargetWhenPlayerStopsUsing",
	"m_iszTurnedOffTarget",
	"max_player_target",
	"mouse_target_0_0",
	"mouse_target_0_1",
	"mouse_target_1_0",
	"mouse_target_1_1",
	"mouse_target_2_0",
	"mouse_target_2_1",

	// trigger_changelevel
	"changetarget",

	// trigger_changetarget
	"m_iszNewTarget",

	// trigger_condition
	"m_iszSourceName",

	// trigger_createentity
	"m_iszCrtEntChildName",
	"m_iszTriggerAfter", // commented out in FGD for some reason? Think I've used it before.

	// trigger_endsection
	"section", // TODO: what is this?

	// trigger_entity_iterator
	"name_filter",
	"trigger_after_run",

	// trigger_load/save
	"m_iszTrigger",

	// BaseRandom
	"target1",
	"target2",
	"target3",
	"target4",
	"target5",
	"target6",
	"target7",
	"target8",
	"target9",
	"target10",
	"target11",
	"target12",
	"target13",
	"target14",
	"target15",
	"target16",

	// trigger_setorigin
	"copypointer",

	"noise",

	// weapon_displacer
	"m_iszTeleportDestination",

	// item_inventory
	"item_name",
	"item_group",
	"filter_targetnames",
	"item_name_moved",
	"item_name_not_moved",
	"target_on_collect",
	"target_on_collect_team",
	"target_on_collect_other",
	"target_cant_collect",
	"target_cant_collect_team",
	"target_cant_collect_other",
	"target_on_drop",
	"target_on_drop_team",
	"target_on_drop_other",
	"target_cant_drop",
	"target_cant_drop_team",
	"target_cant_drop_other",
	"target_on_activate",
	"target_on_activate_team",
	"target_on_activate_other",
	"target_cant_activate",
	"target_cant_activate_team",
	"target_cant_activate_other",
	"target_on_use",
	"target_on_use_team",
	"target_on_use_other",
	"target_on_wearing_out",
	"target_on_wearing_out_team",
	"target_on_wearing_out_other",
	"target_on_return",
	"target_on_return_team",
	"target_on_return_other",
	"target_on_materialise",
	"target_on_destroy",

	// inventory rules
	"item_name_required",
	"item_group_required",
	"item_name_canthave",
	"item_group_canthave",
	"pass_drop_item_name",
	"pass_drop_item_group",
	"pass_return_item_name",
	"pass_return_item_group",
	"pass_destroy_item_name",
	"pass_destroy_item_group"
};

// This needs to be kept in sync with the FGD

unordered_map<string, StringSet> g_cached_target_keys;

const StringSet& Entity::getTargets() {
	if (targetsCached) {
		return cachedTargets;
	}

	cachedTargets.eraseAll();

	StringSet* targetKeys;
	string cname = getClassname();

	auto cachedKeys = g_cached_target_keys.find(cname);
	if (cachedKeys != g_cached_target_keys.end()) {
		targetKeys = &cachedKeys->second;
	}
	else {
		StringSet targetKeysCache;

		for (int i = 1; i < TOTAL_TARGETNAME_KEYS; i++) { // skip targetname
			targetKeysCache.put(potential_targetname_keys[i]);
		}

		FgdClass* fgd = g_app->mergedFgd ? g_app->mergedFgd->getFgdClass(getClassname()) : NULL;
		if (fgd) {
			for (KeyvalueDef& def : fgd->keyvalues) {
				if (def.iType == FGD_KEY_TARGET_DST) {
					targetKeysCache.put(def.name.c_str());
				}
			}
		}

		g_cached_target_keys[cname] = targetKeysCache;
		targetKeys = &g_cached_target_keys[cname];
	}

	StringSet::iterator_t iter;
	while (targetKeys->iterate(iter)) {
		string val = getKeyvalue(iter.key);
		if (val.size()) {
			cachedTargets.put(val.c_str());
		}
	}

	if (getKeyvalue("classname") == "multi_manager") {
		// multi_manager is a special case where the targets are in the key names
		for (int i = 0; i < keyOrder.size(); i++) {
			string tname = keyOrder[i];
			size_t hashPos = tname.find("#");
			string suffix;

			// duplicate targetnames have a #X suffix to differentiate them
			if (hashPos != string::npos) {
				tname = tname.substr(0, hashPos);
			}
			cachedTargets.put(tname.c_str());
		}
	}


	targetsCached = true;
	return cachedTargets;
}

bool Entity::hasTarget(string checkTarget) {
	if (!targetsCached) {
		getTargets();
	}

	return cachedTargets.hasKey(checkTarget.c_str());
}

bool Entity::hasTarget(const StringSet& checkNames) {
	if (!targetsCached) {
		getTargets();
	}

	StringSet::iterator_t iter;
	while (checkNames.iterate(iter)) {
		if (cachedTargets.hasKey(iter.key)) {
			return true;
		}
	}

	return false;
}

void Entity::renameTargetnameValues(string oldTargetname, string newTargetname) {
	for (int i = 0; i < TOTAL_TARGETNAME_KEYS; i++) {
		const char* key = potential_targetname_keys[i];
		const char* entkey = keyvalues.get(key);
		if (entkey && entkey == oldTargetname) {
			keyvalues.put(key, newTargetname.c_str());
		}
	}

	if (getKeyvalue("classname") == "multi_manager") {
		// multi_manager is a special case where the targets are in the key names
		for (int i = 0; i < keyOrder.size(); i++) {
			string tname = keyOrder[i];
			size_t hashPos = tname.find("#");
			string suffix;

			// duplicate targetnames have a #X suffix to differentiate them
			if (hashPos != string::npos) {
				tname = keyOrder[i].substr(0, hashPos);
				suffix = keyOrder[i].substr(hashPos);
			}

			if (tname == oldTargetname) {
				string newKey = newTargetname + suffix;
				const char* val = keyvalues.get(keyOrder[i].c_str());
				keyvalues.del(keyOrder[i].c_str());
				keyvalues.put(newKey.c_str(), val ? val : "");
				keyOrder[i] = newKey;
			}
		}
	}

	clearCache();
}

int Entity::getMemoryUsage() {
	int size = sizeof(Entity);

	size += (cachedTargets.size() + cachedTargetnames.size()) * sizeof(string_t);

	for (int i = 0; i < keyOrder.size(); i++) {
		size += keyOrder[i].size();
	}
	size += keyvalues.calcMemUsage();
	size += cachedAllKvStr.size();
	size += cachedMdlCname.size();
	size += cachedClassname.size();
	size += cachedTargetname.size();

	return size;
}

bool Entity::isEverVisible() {
	string cname = getKeyvalue("classname");
	string tname = getKeyvalue("targetname");

	static set<string> invisibleEnts = {
		"env_bubbles",
		"func_clip",
		"func_friction",
		"func_ladder",
		"func_monsterclip",
		"func_mortar_field",
		"func_op4mortarcontroller",
		"func_tankcontrols",
		"func_traincontrols",
		"trigger_autosave",
		"trigger_cameratarget",
		"trigger_cdaudio",
		"trigger_changelevel",
		"trigger_counter",
		"trigger_endsection",
		"trigger_gravity",
		"trigger_hurt",
		"trigger_monsterjump",
		"trigger_multiple",
		"trigger_once",
		"trigger_push",
		"trigger_teleport",
		"trigger_transition",
		"game_zone_player",
		"info_hullshape",
		"player_respawn_zone",
	};

	if (invisibleEnts.count(cname)) {
		return false;
	}

	if (!tname.length() && atoi(getKeyvalue("rendermode").c_str()) != 0) {
		if (atoi(getKeyvalue("renderamt").c_str()) == 0) {
			// starts invisible and likely nothing will change that because it has no targetname
			return false;
		}
	}

	return true;
}

string Entity::serialize(bool serializeBspModel) {
	stringstream ent_data;

	ent_data << "{\n";

	int bspModel = getBspModelIdx();

	for (int k = 0; k < keyOrder.size(); k++) {
		string key = keyOrder[k];
		string value = getKeyvalue(key);

		ent_data << "\"" << key << "\" \"" << value << "\"\n";
	}

	if (serializeBspModel && bspModel >= 0) {
		string value = g_app->mapRenderer->map->stringify_model(bspModel);
		ent_data << "\"bspguy_binary_data\" \"" << value << "\"\n";
	}

	ent_data << "}\n";

	return ent_data.str();
}

bool Entity::deserialize() {
	if (hasKey("bspguy_binary_data")) {
		int modelIdx = g_app->mapRenderer->map->add_model(getKeyvalue("bspguy_binary_data"));
		if (modelIdx != -1)
			setOrAddKeyvalue("model", "*" + to_string(modelIdx));
		else
			removeKeyvalue("model");
		removeKeyvalue("bspguy_binary_data");
		return true;
	}

	return false;
}

void Entity::clearCache() {
	cachedModelIdx = -2;
	targetsCached = false;
	drawCached = false;
	hasCachedMdl = false;
	hasCachedTargetname = false;
	hasCachedClassname = false;
	hasCachedOrigin = false;
	hasCachedAngles = false;
	hasCachedRenderOpts = false;
	hasCachedRotMatrixes = false;
	hasCachedFgdTint = false;
	hasCachedTargetnames = false;
	didStudioDraw = false;
	cachedMdlCname = "";
	cachedAllKvStr = "";
	cachedFgdTint = COLOR3(0, 0, 0);
	cachedMdl = NULL;
	lastDrawCall = 0;
	drawFrame = 0;
	cachedTargets.clear();
}
