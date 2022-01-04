#include "Entity.h"
#include <string>
#include "util.h"
#include <algorithm>

Entity::Entity(const std::string& classname)
{
	addKeyvalue("classname", classname);
}

void Entity::addKeyvalue(const std::string& key, const std::string& value)
{
	int dup = 1;
	if (keyvalues.find(key) == keyvalues.end()) {
		keyvalues[key] = value;
		keyOrder.push_back(key);
	}
	else
	{
		while (true)
		{
			std::string newKey = key + '#' + std::to_string((long long)dup);
			if (keyvalues.find(newKey) == keyvalues.end())
			{
				//println("wrote dup key " + newKey);
				keyvalues[newKey] = value;
				keyOrder.push_back(newKey);
				break;
			}
			dup++;
		}
	}

	cachedModelIdx = -2;
	targetsCached = false;
}

void Entity::setOrAddKeyvalue(const std::string& key, const std::string& value) {
	cachedModelIdx = -2;
	targetsCached = false;

	if (hasKey(key)) {
		keyvalues[key] = value;
		return;
	}
	addKeyvalue(key, value);
}

void Entity::removeKeyvalue(const std::string& key) {
	if (!hasKey(key))
		return;
	keyOrder.erase(find(keyOrder.begin(), keyOrder.end(), key));
	keyvalues.erase(key);
	cachedModelIdx = -2;
	targetsCached = false;
}

bool Entity::renameKey(int idx, std::string newName) {
	if (idx < 0 || idx >= keyOrder.size() || newName.empty()) {
		return false;
	}
	for (int i = 0; i < keyOrder.size(); i++) {
		if (keyOrder[i] == newName) {
			return false;
		}
	}

	keyvalues[newName] = keyvalues[keyOrder[idx]];
	keyvalues.erase(keyOrder[idx]);
	keyOrder[idx] = newName;
	cachedModelIdx = -2;
	targetsCached = false;
	return true;
}

void Entity::clearAllKeyvalues() {
	keyOrder.clear();
	keyvalues.clear();
	cachedModelIdx = -2;
}

void Entity::clearEmptyKeyvalues() {
	std::vector<std::string> newKeyOrder;
	for (int i = 0; i < keyOrder.size(); i++) {
		if (!keyvalues[keyOrder[i]].empty()) {
			newKeyOrder.push_back(keyOrder[i]);
		}
	}
	keyOrder = std::move(newKeyOrder);
	cachedModelIdx = -2;
	targetsCached = false;
}

bool Entity::hasKey(const std::string& key)
{
	return keyvalues.find(key) != keyvalues.end() && find(keyOrder.begin(), keyOrder.end(), key) != keyOrder.end();
}

int Entity::getBspModelIdx() {
	if (cachedModelIdx != -2) {
		return cachedModelIdx;
	}

	if (!hasKey("model")) {
		cachedModelIdx = -1;
		return -1;
	}

	std::string model = keyvalues["model"];
	if (model.size() <= 1 || model[0] != '*') {
		cachedModelIdx = -1;
		return -1;
	}

	std::string modelIdxStr = model.substr(1);
	if (!isNumeric(modelIdxStr)) {
		cachedModelIdx = -1;
		return -1;
	}
	cachedModelIdx = atoi(modelIdxStr.c_str());
	return cachedModelIdx;
}

int Entity::getBspModelIdxForce() {
	if (!hasKey("model")) {
		return -1;
	}

	std::string model = keyvalues["model"];
	if (model.size() <= 1 || model[0] != '*') {
		return -1;
	}

	std::string modelIdxStr = model.substr(1);
	if (!isNumeric(modelIdxStr)) {
		return -1;
	}
	return atoi(modelIdxStr.c_str());
}

bool Entity::isBspModel() {
	return getBspModelIdx() >= 0;
}

vec3 Entity::getOrigin() {
	return hasKey("origin") ? parseVector(keyvalues["origin"]) : vec3(0, 0, 0);
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

std::vector<std::string> Entity::getTargets() {
	if (targetsCached) {
		return cachedTargets;
	}

	std::vector<std::string> targets;

	for (int i = 1; i < TOTAL_TARGETNAME_KEYS; i++) { // skip targetname
		const char* key = potential_tergetname_keys[i];
		if (hasKey(key)) {
			targets.push_back(keyvalues[key]);
		}
	}

	if (keyvalues["classname"] == "multi_manager") {
		// multi_manager is a special case where the targets are in the key names
		for (int i = 0; i < keyOrder.size(); i++) {
			std::string tname = keyOrder[i];
			size_t hashPos = tname.find('#');
			// std::string suffix;

			// duplicate targetnames have a #X suffix to differentiate them
			if (hashPos != std::string::npos) {
				tname = tname.substr(0, hashPos);
			}
			targets.push_back(tname);
		}
	}

	cachedTargets.clear();
	cachedTargets.reserve(targets.size());
	for (int i = 0; i < targets.size(); i++) {
		cachedTargets.push_back(targets[i]);
	}
	targetsCached = true;

	return targets;
}

bool Entity::hasTarget(std::string checkTarget) {
	std::vector<std::string> targets = getTargets();
	for (int i = 0; i < targets.size(); i++) {
		if (targets[i] == checkTarget) {
			return true;
		}
	}

	return false;
}

void Entity::renameTargetnameValues(std::string oldTargetname, std::string newTargetname) {
	for (int i = 0; i < TOTAL_TARGETNAME_KEYS; i++) {
		const char* key = potential_tergetname_keys[i];
		if (keyvalues.find(key) != keyvalues.end() && keyvalues[key] == oldTargetname) {
			keyvalues[key] = newTargetname;
		}
	}

	if (keyvalues["classname"] == "multi_manager") {
		// multi_manager is a special case where the targets are in the key names
		for (int i = 0; i < keyOrder.size(); i++) {
			std::string tname = keyOrder[i];
			size_t hashPos = tname.find("#");
			std::string suffix;

			// duplicate targetnames have a #X suffix to differentiate them
			if (hashPos != std::string::npos) {
				tname = keyOrder[i].substr(0, hashPos);
				suffix = keyOrder[i].substr(hashPos);
			}

			if (tname == oldTargetname) {
				std::string newKey = newTargetname + suffix;
				keyvalues[newKey] = keyvalues[keyOrder[i]];
				keyOrder[i] = newKey;
			}
		}
	}
}

int Entity::getMemoryUsage() {
	int size = sizeof(Entity);

	for (int i = 0; i < cachedTargets.size(); i++) {
		size += cachedTargets[i].size();
	}
	for (int i = 0; i < keyOrder.size(); i++) {
		size += keyOrder[i].size();
	}
	for (const auto& entry : keyvalues) {
		size += entry.first.size() + entry.second.size();
	}

	return size;
}