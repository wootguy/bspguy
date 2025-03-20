#include "Entity.h"
#include "util.h"
#include <set>
#include <algorithm>
#include <sstream>
#include "Bsp.h"
#include "globals.h"
#include "Renderer.h"
#include <unordered_set>

using namespace std;

Entity::Entity(void)
{
}

Entity::Entity(const string& classname)
{
	addKeyvalue("classname", classname);
}

Entity::~Entity(void)
{
}

void Entity::addKeyvalue( Keyvalue& k )
{
	int dup = 1;
	if (keyvalues.find(k.key) == keyvalues.end()) {
		keyvalues[k.key] = k.value;
		keyOrder.push_back(k.key);
	}
	else
	{
		while (true)
		{
			string newKey = k.key + '#' + to_string((long long)dup);
			if (keyvalues.find(newKey) == keyvalues.end())
			{
				//println("wrote dup key " + newKey);
				keyvalues[newKey] = k.value;
				keyOrder.push_back(newKey);
				break;
			}
			dup++;
		}
	}

	clearCache();
}

string Entity::getKeyvalue(string key) {
	auto found = keyvalues.find(key);
	if (found == keyvalues.end()) {
		return "";
	}
	return found->second;
}

unordered_map<string, string> Entity::getAllKeyvalues() {
	return keyvalues;
}

void Entity::addKeyvalue(const std::string& key, const std::string& value)
{
	keyvalues[key] = value;

	keyOrder.push_back(key);
	clearCache();
}

void Entity::setOrAddKeyvalue(const std::string& key, const std::string& value) {
	clearCache();

	auto existing = keyvalues.find(key);
	if (existing != keyvalues.end()) {
		existing->second = value;
		return;
	}
	addKeyvalue(key, value);
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
	keyvalues.erase(key);
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
	
	keyOrder[hasKey] = newName;
	keyvalues[newName] = keyvalues[oldName];
	keyvalues.erase(oldName);
	clearCache();
	return true;
}

void Entity::clearAllKeyvalues() {
	keyOrder.clear();
	keyvalues.clear();
	cachedModelIdx = -2;
}

void Entity::clearEmptyKeyvalues() {
	vector<string> newKeyOrder;
	unordered_map<string, string> newKeyvalues;
	for (int i = 0; i < keyOrder.size(); i++) {
		if (!keyvalues[keyOrder[i]].empty()) {
			newKeyOrder.push_back(keyOrder[i]);
			newKeyvalues[keyOrder[i]] = keyvalues[keyOrder[i]];
		}
	}
	keyOrder = newKeyOrder;
	keyvalues = newKeyvalues;
	clearCache();
}

bool Entity::hasKey(const std::string& key)
{
	return keyvalues.find(key) != keyvalues.end();
}

int Entity::getBspModelIdx() {
	if (cachedModelIdx != -2) {
		return cachedModelIdx;
	}

	if (!hasKey("model")) {
		cachedModelIdx = -1;
		return -1;
	}

	string model = keyvalues["model"];
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

string Entity::getTargetname() {
	if (hasCachedTargetname) {
		return cachedTargetname;
	}

	auto kv = keyvalues.find("targetname");
	if (kv == keyvalues.end()) {
		return "";
	}

	cachedTargetname = kv->second;
	hasCachedTargetname = true;

	return cachedTargetname;
}

string Entity::getClassname() {
	if (hasCachedClassname) {
		return cachedClassname;
	}

	auto kv = keyvalues.find("classname");
	if (kv == keyvalues.end()) {
		return "";
	}

	cachedClassname = kv->second;
	hasCachedClassname = true;

	return cachedClassname;
}

vec3 Entity::getOrigin() {
	if (hasCachedOrigin) {
		return cachedOrigin;
	}

	auto kv = keyvalues.find("origin");
	if (kv == keyvalues.end()) {
		cachedOrigin = vec3();
	}
	else {
		cachedOrigin = parseVector(kv->second);
	}

	hasCachedOrigin = true;
	return cachedOrigin;
}

vec3 Entity::getAngles() {
	if (hasCachedAngles) {
		return cachedAngles;
	}

	auto kv = keyvalues.find("angles");
	if (kv == keyvalues.end()) {
		cachedAngles = vec3();
	}
	else {
		cachedAngles = parseVector(kv->second);
	}

	hasCachedAngles = true;
	return cachedAngles;
}

mat4x4 Entity::getRotationMatrix(bool flipped) {
	mat4x4 angleTransform;
	angleTransform.loadIdentity();

	if (canRotate()) {
		vec3 angles = getAngles() * (PI / 180.0f);
		
		if (angles != vec3()) {
			if (flipped) {
				// well this makes no sense but it's required for object picking
				// but not for rendering. I guess it's a combination of flips or undoing them, idk
				angleTransform.rotateY(angles.x);
				angleTransform.rotateZ(-angles.y);
				angleTransform.rotateX(-angles.z);
			}
			else {
				angleTransform.rotateX(angles.z);
				angleTransform.rotateY(angles.y);
				angleTransform.rotateZ(angles.x);
			}
		}
	}

	return angleTransform;
}

bool Entity::canRotate() {
	if (g_app->forceAngleRotation) {
		return true;
	}

	if (getBspModelIdx() == -1) {
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

// TODO: maybe store this in a text file or something
#define TOTAL_TARGETNAME_KEYS 134
const char* potential_tergetname_keys[TOTAL_TARGETNAME_KEYS] = {
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

unordered_set<string> Entity::getTargets() {
	if (targetsCached) {
		return cachedTargets;
	}

	unordered_set<string> targets;

	for (int i = 1; i < TOTAL_TARGETNAME_KEYS; i++) { // skip targetname
		const char* key = potential_tergetname_keys[i];
		if (hasKey(key)) {
			targets.insert(keyvalues[key]);
		}
	}

	if (keyvalues["classname"] == "multi_manager") {
		// multi_manager is a special case where the targets are in the key names
		for (int i = 0; i < keyOrder.size(); i++) {
			string tname = keyOrder[i];
			size_t hashPos = tname.find("#");
			string suffix;

			// duplicate targetnames have a #X suffix to differentiate them
			if (hashPos != string::npos) {
				tname = tname.substr(0, hashPos);
			}
			targets.insert(tname);
		}
	}

	cachedTargets.clear();
	cachedTargets.reserve(targets.size());
	for (string tar : targets) {
		cachedTargets.insert(tar);
	}
	targetsCached = true;

	return targets;
}

bool Entity::hasTarget(string checkTarget) {
	if (!targetsCached) {
		getTargets();
	}

	return cachedTargets.find(checkTarget) != cachedTargets.end();
}

void Entity::renameTargetnameValues(string oldTargetname, string newTargetname) {
	for (int i = 0; i < TOTAL_TARGETNAME_KEYS; i++) {
		const char* key = potential_tergetname_keys[i];
		auto entkey = keyvalues.find(key);
		if (entkey != keyvalues.end() && entkey->second == oldTargetname) {
			keyvalues[key] = newTargetname;
		}
	}

	if (keyvalues["classname"] == "multi_manager") {
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
				keyvalues[newKey] = keyvalues[keyOrder[i]];
				keyvalues.erase(keyOrder[i]);
				keyOrder[i] = newKey;
			}
		}
	}
}

int Entity::getMemoryUsage() {
	int size = sizeof(Entity);

	for (string tar: cachedTargets) {
		size += tar.size();
	}
	for (int i = 0; i < keyOrder.size(); i++) {
		size += keyOrder[i].size();
	}
	for (const auto& entry : keyvalues) {
		size += entry.first.size() + entry.second.size();
	}

	return size;
}

bool Entity::isEverVisible() {
	string cname = keyvalues["classname"];
	string tname = hasKey("targetname") ? keyvalues["targetname"] : "";

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

	if (!tname.length() && hasKey("rendermode") && atoi(keyvalues["rendermode"].c_str()) != 0) {
		if (!hasKey("renderamt") || atoi(keyvalues["renderamt"].c_str()) == 0) {
			// starts invisible and likely nothing will change that because it has no targetname
			return false;
		}
	}

	return true;
}

string Entity::serialize() {
	stringstream ent_data;

	ent_data << "{\n";

	for (int k = 0; k < keyOrder.size(); k++) {
		string key = keyOrder[k];
		ent_data << "\"" << key << "\" \"" << keyvalues[key] << "\"\n";
	}

	ent_data << "}\n";

	return ent_data.str();
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
	cachedMdl = NULL;
	cachedTargets.clear();
}
