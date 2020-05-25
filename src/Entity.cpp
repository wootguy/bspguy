#include "Entity.h"
#include <string>
#include "util.h"
#include <algorithm>

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
}

void Entity::addKeyvalue(const std::string& key, const std::string& value)
{
	keyvalues[key] = value;

	keyOrder.push_back(key);
}

void Entity::setOrAddKeyvalue(const std::string& key, const std::string& value) {
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
}

bool Entity::renameKey(int idx, string newName) {
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
	return true;
}

void Entity::clearAllKeyvalues() {
	keyOrder.clear();
	keyvalues.clear();
}

void Entity::clearEmptyKeyvalues() {
	vector<string> newKeyOrder;
	for (int i = 0; i < keyOrder.size(); i++) {
		if (!keyvalues[keyOrder[i]].empty()) {
			newKeyOrder.push_back(keyOrder[i]);
		}
	}
	keyOrder = newKeyOrder;
}

bool Entity::hasKey(const std::string& key)
{
	return keyvalues.find(key) != keyvalues.end() && find(keyOrder.begin(), keyOrder.end(), key) != keyOrder.end();
}

int Entity::getBspModelIdx() {
	if (!hasKey("model"))
		return -1;

	string model = keyvalues["model"];
	if (model.size() <= 1 || model[0] != '*') {
		return -1;
	}

	string modelIdxStr = model.substr(1);
	if (!isNumeric(modelIdxStr)) {
		return -1;
	}

	return atoi(modelIdxStr.c_str());
}

bool Entity::isBspModel() {
	return getBspModelIdx() >= 0;
}

// TODO: maybe store this in a text file or something
#define TOTAL_TARGETNAME_KEYS 134
const char* potential_tergetname_keys[TOTAL_TARGETNAME_KEYS] = {
	// common target-related keys
	"target",
	"targetname",
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

void Entity::renameTargetnameValues(string oldTargetname, string newTargetname) {
	for (int i = 0; i < TOTAL_TARGETNAME_KEYS; i++) {
		const char* key = potential_tergetname_keys[i];
		if (keyvalues.find(key) != keyvalues.end() && keyvalues[key] == oldTargetname) {
			keyvalues[key] = newTargetname;
		}
	}
}