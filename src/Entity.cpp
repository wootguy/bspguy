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

// This needs to be kept in sync with the FGD
// TODO: maybe store this in a text file or something
vector<string> Entity::getEntityRelatedKeys() {
	string cname = keyvalues["classname"];

	vector<string> potentialKeys;
	potentialKeys.reserve(128);

	// common target-related keys
	potentialKeys.push_back("target");
	potentialKeys.push_back("targetname");
	potentialKeys.push_back("killtarget");
	potentialKeys.push_back("master");
	if (cname != "trigger_numericdisplay")
		potentialKeys.push_back("netname");
	potentialKeys.push_back("message"); // not always an entity, but unlikely a .wav file or something will match an entity name

	// monster_* and monster spawners
	potentialKeys.push_back("TriggerTarget");
	potentialKeys.push_back("path_name");
	potentialKeys.push_back("guard_ent");
	potentialKeys.push_back("trigger_target");
	potentialKeys.push_back("xenmaker");
	potentialKeys.push_back("squadname");

	// OpenClosable
	potentialKeys.push_back("fireonopening");
	potentialKeys.push_back("fireonclosing");
	potentialKeys.push_back("fireonopened");
	potentialKeys.push_back("fireonclosed");

	// breakables
	potentialKeys.push_back("fireonbreak");

	// Trackchange
	potentialKeys.push_back("train");
	potentialKeys.push_back("toptrack");
	potentialKeys.push_back("bottomtrack");

	// scripted sequences
	potentialKeys.push_back("m_iszEntity");
	potentialKeys.push_back("entity");
	//potentialKeys.push_back("listener"); // TODO: what is this?

	// BaseCharger
	potentialKeys.push_back("TriggerOnEmpty");
	potentialKeys.push_back("TriggerOnRecharged");

	// Beams
	potentialKeys.push_back("LightningStart");
	potentialKeys.push_back("LightningEnd");
	potentialKeys.push_back("LaserTarget");
	potentialKeys.push_back("laserentity");

	// func_rot_button
	potentialKeys.push_back("changetarget");

	// game_zone_player
	potentialKeys.push_back("intarget");
	potentialKeys.push_back("outtarget");

	// info_bigmomma
	potentialKeys.push_back("reachtarget");
	potentialKeys.push_back("presequence");

	// info_monster_goal
	potentialKeys.push_back("enemy");

	// path_condition_controller
	potentialKeys.push_back("conditions_reference");
	potentialKeys.push_back("starttrigger");
	// TODO: support lists of targetnames
	//potentialKeys.push_back("pathcondition_list");
	//potentialKeys.push_back("waypoint_list");
	//potentialKeys.push_back("m_szASConditionsName"); // TODO: what is this?
	
	// path_waypoint
	potentialKeys.push_back("alternate_target");
	potentialKeys.push_back("trigger_on_arrival");
	potentialKeys.push_back("trigger_after_arrival");
	potentialKeys.push_back("wait_master");
	potentialKeys.push_back("trigger_on_departure");
	potentialKeys.push_back("overflow_waypoint");
	potentialKeys.push_back("stop_trigger");

	// path_track
	potentialKeys.push_back("altpath");

	// trigger_camera + trigger_cameratarget
	potentialKeys.push_back("moveto");
	// TODO: parameters are not always entities(?)
	potentialKeys.push_back("mouse_param_0_0");
	potentialKeys.push_back("mouse_param_0_1");
	potentialKeys.push_back("mouse_param_1_0");
	potentialKeys.push_back("mouse_param_1_1");
	potentialKeys.push_back("mouse_param_2_0");
	potentialKeys.push_back("mouse_param_2_1");
	potentialKeys.push_back("m_iszOverridePlayerTargetname");
	potentialKeys.push_back("m_iszTargetWhenPlayerStartsUsing");
	potentialKeys.push_back("m_iszTargetWhenPlayerStopsUsing");
	potentialKeys.push_back("m_iszTurnedOffTarget");
	potentialKeys.push_back("max_player_target");
	potentialKeys.push_back("mouse_target_0_0");
	potentialKeys.push_back("mouse_target_0_1");
	potentialKeys.push_back("mouse_target_1_0");
	potentialKeys.push_back("mouse_target_1_1");
	potentialKeys.push_back("mouse_target_2_0");
	potentialKeys.push_back("mouse_target_2_1");

	// trigger_changelevel
	potentialKeys.push_back("changetarget");

	// trigger_changetarget
	potentialKeys.push_back("m_iszNewTarget");

	// trigger_condition
	potentialKeys.push_back("m_iszSourceName");

	// trigger_createentity
	potentialKeys.push_back("m_iszCrtEntChildName");
	potentialKeys.push_back("m_iszTriggerAfter"); // commented out in FGD for some reason? Think I've used it before.
	
	// trigger_endsection
	potentialKeys.push_back("section"); // TODO: what is this?

	// trigger_entity_iterator
	potentialKeys.push_back("name_filter");
	potentialKeys.push_back("trigger_after_run");

	// trigger_load/save
	potentialKeys.push_back("m_iszTrigger");

	// BaseRandom
	potentialKeys.push_back("target1");
	potentialKeys.push_back("target2");
	potentialKeys.push_back("target3");
	potentialKeys.push_back("target4");
	potentialKeys.push_back("target5");
	potentialKeys.push_back("target6");
	potentialKeys.push_back("target7");
	potentialKeys.push_back("target8");
	potentialKeys.push_back("target9");
	potentialKeys.push_back("target10");
	potentialKeys.push_back("target11");
	potentialKeys.push_back("target12");
	potentialKeys.push_back("target13");
	potentialKeys.push_back("target14");
	potentialKeys.push_back("target15");
	potentialKeys.push_back("target16");

	// trigger_setorigin
	potentialKeys.push_back("copypointer");

	if (cname == "trigger_vote") {
		potentialKeys.push_back("noise");
	}
	
	// weapon_displacer
	potentialKeys.push_back("m_iszTeleportDestination");

	if (cname == "item_inventory") {
		potentialKeys.push_back("item_name");
		potentialKeys.push_back("item_group");
		potentialKeys.push_back("filter_targetnames");
		potentialKeys.push_back("item_name_moved");
		potentialKeys.push_back("item_name_not_moved");
		potentialKeys.push_back("target_on_collect");
		potentialKeys.push_back("target_on_collect_team");
		potentialKeys.push_back("target_on_collect_other");
		potentialKeys.push_back("target_cant_collect");
		potentialKeys.push_back("target_cant_collect_team");
		potentialKeys.push_back("target_cant_collect_other");
		potentialKeys.push_back("target_on_drop");
		potentialKeys.push_back("target_on_drop_team");
		potentialKeys.push_back("target_on_drop_other");
		potentialKeys.push_back("target_cant_drop");
		potentialKeys.push_back("target_cant_drop_team");
		potentialKeys.push_back("target_cant_drop_other");
		potentialKeys.push_back("target_on_activate");
		potentialKeys.push_back("target_on_activate_team");
		potentialKeys.push_back("target_on_activate_other");
		potentialKeys.push_back("target_cant_activate");
		potentialKeys.push_back("target_cant_activate_team");
		potentialKeys.push_back("target_cant_activate_other");
		potentialKeys.push_back("target_on_use");
		potentialKeys.push_back("target_on_use_team");
		potentialKeys.push_back("target_on_use_other");
		potentialKeys.push_back("target_on_wearing_out");
		potentialKeys.push_back("target_on_wearing_out_team");
		potentialKeys.push_back("target_on_wearing_out_other");
		potentialKeys.push_back("target_on_return");
		potentialKeys.push_back("target_on_return_team");
		potentialKeys.push_back("target_on_return_other");
		potentialKeys.push_back("target_on_materialise");
		potentialKeys.push_back("target_on_destroy");
	}

	// inventory rules
	potentialKeys.push_back("item_name_required");
	potentialKeys.push_back("item_group_required");
	potentialKeys.push_back("item_name_canthave");
	potentialKeys.push_back("item_group_canthave");
	potentialKeys.push_back("pass_drop_item_name");
	potentialKeys.push_back("pass_drop_item_group");
	potentialKeys.push_back("pass_return_item_name");
	potentialKeys.push_back("pass_return_item_group");
	potentialKeys.push_back("pass_destroy_item_name");
	potentialKeys.push_back("pass_destroy_item_group");
	

	vector<string> keys;
	for (int i = 0; i < potentialKeys.size(); i++) {
		if (hasKey(potentialKeys[i]))
			keys.push_back(potentialKeys[i]);
	}
	return keys;
}