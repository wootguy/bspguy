#include "BspMerger.h"
#include <algorithm>
#include <map>
#include <set>
#include "vis.h"

BspMerger::BspMerger() {

}

Bsp* BspMerger::merge(vector<Bsp*> maps, vec3 gap, string output_name, bool noripent, bool noscript) {
	if (maps.size() < 1) {
		logf("\nMore than 1 map is required for merging. Aborting merge.\n");
		return NULL;
	}
	vector<vector<vector<MAPBLOCK>>> blocks = separate(maps, gap);


	logf("\nArranging maps so that they don't overlap:\n");

	for (int z = 0; z < blocks.size(); z++) {
		for (int y = 0; y < blocks[z].size(); y++) {
			for (int x = 0; x < blocks[z][y].size(); x++) {
				MAPBLOCK& block = blocks[z][y][x];

				if (block.offset.x != 0 || block.offset.y != 0 || block.offset.z != 0) {
				logf("    Apply offset (%6.0f, %6.0f, %6.0f) to %s\n",
					block.offset.x, block.offset.y, block.offset.z, block.map->name.c_str());
				block.map->move(block.offset);
				}

				if (!noripent) {
					// tag ents with the map they belong to
					for (int i = 0; i < block.map->ents.size(); i++) {
						block.map->ents[i]->addKeyvalue("$s_bspguy_map_source", toLowerCase(block.map->name));
					}
				}
			}
		}
	}


	// Merge order matters. 
	// The bounding box of a merged map is expanded to contain both maps, and bounding boxes cannot overlap.
	// TODO: Don't merge linearly. Merge gradually bigger chunks to minimize BSP tree depth.
	//       Not worth it until more than 27 maps are merged together (merge cube bigger than 3x3x3)

	logf("\nMerging %d maps:\n", maps.size());


	// merge maps along X axis to form rows of maps
	int rowId = 0;
	int mergeCount = 1;
	for (int z = 0; z < blocks.size(); z++) {
		for (int y = 0; y < blocks[z].size(); y++) {
			MAPBLOCK& rowStart = blocks[z][y][0];
			for (int x = 0; x < blocks[z][y].size(); x++) {
				MAPBLOCK& block = blocks[z][y][x];

				if (x != 0) {
					//logf("Merge %d,%d,%d -> %d,%d,%d\n", x, y, z, 0, y, z);
					string merge_name = ++mergeCount < maps.size() ? "row_" + to_string(rowId) : "result";
					merge(rowStart, block, merge_name);
				}
			}
			rowId++;
		}
	}

	// merge the rows along the Y axis to form layers of maps
	int colId = 0;
	for (int z = 0; z < blocks.size(); z++) {
		MAPBLOCK& colStart = blocks[z][0][0];
		for (int y = 0; y < blocks[z].size(); y++) {
			MAPBLOCK& block = blocks[z][y][0];

			if (y != 0) {
				//logf("Merge %d,%d,%d -> %d,%d,%d\n", 0, y, z, 0, 0, z);
				string merge_name = ++mergeCount < maps.size() ? "layer_" + to_string(colId) : "result";
				merge(colStart, block, merge_name);
			}
		}
		colId++;
	}

	// merge the layers to form a cube of maps
	MAPBLOCK& layerStart = blocks[0][0][0];
	for (int z = 0; z < blocks.size(); z++) {
		MAPBLOCK& block = blocks[z][0][0];

		if (z != 0) {
			//logf("Merge %d,%d,%d -> %d,%d,%d\n", 0, 0, z, 0, 0, 0);
			merge(layerStart, block, "result");
		}
	}

	Bsp* output = layerStart.map;

	if (!noripent) {
		vector<MAPBLOCK> flattenedBlocks;
		for (int z = 0; z < blocks.size(); z++)
			for (int y = 0; y < blocks[z].size(); y++)
				for (int x = 0; x < blocks[z][y].size(); x++)
					flattenedBlocks.push_back(blocks[z][y][x]);

		logf("\nUpdating map series entity logic:\n");
		update_map_series_entity_logic(output, flattenedBlocks, maps, output_name, maps[0]->name, noscript);
	}

	return output;
}

void BspMerger::merge(MAPBLOCK& dst, MAPBLOCK& src, string resultType) {
	string thisName = dst.merge_name.size() ? dst.merge_name : dst.map->name;
	string otherName = src.merge_name.size() ? src.merge_name : src.map->name;
	dst.merge_name = resultType;
	logf("    %-8s = %s + %s\n", dst.merge_name.c_str(), thisName.c_str(), otherName.c_str());

	merge(*dst.map, *src.map);
}

vector<vector<vector<MAPBLOCK>>> BspMerger::separate(vector<Bsp*>& maps, vec3 gap) {
	vector<MAPBLOCK> blocks;
	
	vector<vector<vector<MAPBLOCK>>> orderedBlocks;

	vec3 maxDims = vec3(0, 0, 0);
	for (int i = 0; i < maps.size(); i++) {
		MAPBLOCK block;
		maps[i]->get_bounding_box(block.mins, block.maxs);

		block.size = block.maxs - block.mins;
		block.offset = vec3(0, 0, 0);
		block.map = maps[i];


		if (block.size.x > maxDims.x) {
			maxDims.x = block.size.x;
		}
		if (block.size.y > maxDims.y) {
			maxDims.y = block.size.y;
		}
		if (block.size.z > maxDims.z) {
			maxDims.z = block.size.z;
		}

		blocks.push_back(block);
	}

	bool noOverlap = true;
	for (int i = 0; i < blocks.size() && noOverlap; i++) {
		for (int k = i + i; k < blocks.size(); k++) {
			if (blocks[i].intersects(blocks[k])) {
				noOverlap = false;
				break;
			}
		}
	}

	if (noOverlap) {
		logf("Maps do not overlap. They will be merged without moving.");
		return orderedBlocks;
	}

	maxDims += gap;

	int maxMapsPerRow = (MAX_MAP_COORD * 2.0f) / maxDims.x;
	int maxMapsPerCol = (MAX_MAP_COORD * 2.0f) / maxDims.y;
	int maxMapsPerLayer = (MAX_MAP_COORD * 2.0f) / maxDims.z;

	int idealMapsPerAxis = floor(pow(maps.size(), 1 / 3.0f));

	if (idealMapsPerAxis * idealMapsPerAxis * idealMapsPerAxis < maps.size()) {
		idealMapsPerAxis++;
	}

	if (maxMapsPerRow * maxMapsPerCol * maxMapsPerLayer < maps.size()) {
		logf("Not enough space to merge all maps! Try moving them individually before merging.");
		return orderedBlocks;
	}

	vec3 mergedMapSize = maxDims * (float)idealMapsPerAxis;
	vec3 mergedMapMin = mergedMapSize * -0.5f;
	vec3 mergedMapMax = mergedMapMin + mergedMapSize;

	logf("Max map size:      width=%.0f length=%.0f height=%.0f\n", maxDims.x, maxDims.y, maxDims.z);
	logf("Max maps per axis: x=%d y=%d z=%d  (total=%d)\n", maxMapsPerRow, maxMapsPerCol, maxMapsPerLayer, maxMapsPerRow * maxMapsPerCol * maxMapsPerLayer);

	int actualWidth = min(idealMapsPerAxis, (int)maps.size());
	int actualLength = min(idealMapsPerAxis, (int)ceil(maps.size() / (float)(idealMapsPerAxis)));
	int actualHeight = min(idealMapsPerAxis, (int)ceil(maps.size() / (float)(idealMapsPerAxis * idealMapsPerAxis)));
	logf("Merged map size:   %dx%dx%d maps\n", actualWidth, actualLength, actualHeight);

	logf("Merged map bounds: min=(%.0f, %.0f, %.0f)\n"
		"                   max=(%.0f, %.0f, %.0f)\n",
		mergedMapMin.x, mergedMapMin.y, mergedMapMin.z,
		mergedMapMax.x, mergedMapMax.y, mergedMapMax.z);

	vec3 targetMins = mergedMapMin;
	int blockIdx = 0;
	for (int z = 0; z < idealMapsPerAxis && blockIdx < blocks.size(); z++) {
		targetMins.y = mergedMapMin.y;
		vector<vector<MAPBLOCK>> col;
		for (int y = 0; y < idealMapsPerAxis && blockIdx < blocks.size(); y++) {
			targetMins.x = mergedMapMin.x;
			vector<MAPBLOCK> row;
			for (int x = 0; x < idealMapsPerAxis && blockIdx < blocks.size(); x++) {
				MAPBLOCK& block = blocks[blockIdx];

				block.offset = targetMins - block.mins;
				//logf("block %d: %.0f %.0f %.0f\n", blockIdx, targetMins.x, targetMins.y, targetMins.z);
				//logf("%s offset: %.0f %.0f %.0f\n", block.map->name.c_str(), block.offset.x, block.offset.y, block.offset.z);

				row.push_back(block);

				blockIdx++;
				targetMins.x += maxDims.x;
			}
			col.push_back(row);
			targetMins.y += maxDims.y;
		}
		orderedBlocks.push_back(col);
		targetMins.z += maxDims.z;
	}

	return orderedBlocks;
}

typedef map< string, set<string> > mapStringToSet;
typedef map< string, MAPBLOCK > mapStringToMapBlock;

void BspMerger::update_map_series_entity_logic(Bsp* mergedMap, vector<MAPBLOCK>& sourceMaps,
	vector<Bsp*>& mapOrder, string output_name, string firstMapName, bool noscript) {
	int originalEntCount = mergedMap->ents.size();
	int renameCount = force_unique_ent_names_per_map(mergedMap);

	g_progress.update("Processing entities", originalEntCount);

	const string load_section_prefix = "bspguy_setup_";

	// things to trigger when loading a new map
	mapStringToSet load_map_triggers;
	mapStringToMapBlock mapsByName;

	vec3 map_info_origin = vec3(-64, 64, 0);
	vec3 changesky_origin = vec3(64, 64, 0);
	vec3 equip_origin = vec3(64, -64, 0);

	{
		Entity* map_info = new Entity();
		map_info->addKeyvalue("origin", map_info_origin.toKeyvalueString());
		map_info->addKeyvalue("targetname", "bspguy_info");
		map_info->addKeyvalue("$s_noscript", noscript ? "yes" : "no");
		map_info->addKeyvalue("$s_version", g_version_string);
		map_info->addKeyvalue("classname", "info_target");

		for (int i = 0; i < mapOrder.size(); i++) {
			map_info->addKeyvalue("$s_map" + to_string(i), toLowerCase(mapOrder[i]->name));
		}

		mergedMap->ents.push_back(map_info);
		map_info_origin.z += 10;
	}

	for (int i = 0; i < sourceMaps.size(); i++) {
		string sourceMapName = sourceMaps[i].map->name;
		mapsByName[toLowerCase(sourceMapName)] = sourceMaps[i];

		if (!noscript) {
			MAPBLOCK sourceMap = mapsByName[toLowerCase(sourceMapName)];
			vec3 map_min = sourceMap.mins + sourceMap.offset;
			vec3 map_max = sourceMap.maxs + sourceMap.offset;

			Entity* map_info = new Entity();
			map_info->addKeyvalue("origin", map_info_origin.toKeyvalueString());
			map_info->addKeyvalue("targetname", "bspguy_info_" + toLowerCase(sourceMapName));
			map_info->addKeyvalue("$v_min", map_min.toKeyvalueString());
			map_info->addKeyvalue("$v_max", map_max.toKeyvalueString());
			map_info->addKeyvalue("$v_offset", sourceMap.offset.toKeyvalueString());
			map_info->addKeyvalue("classname", "info_target");
			mergedMap->ents.push_back(map_info);
			map_info_origin.z += 10;
		}
	}

	string startingSky = "desert";
	string startingSkyColor = "0 0 0 0";
	for (int k = 0; k < mergedMap->ents.size(); k++) {
		Entity* ent = mergedMap->ents[k];
		if (ent->keyvalues["classname"] == "worldspawn") {
			if (ent->hasKey("skyname")) {
				startingSky = toLowerCase(ent->keyvalues["skyname"]);
			}
		}
		if (ent->keyvalues["classname"] == "light_environment") {
			if (ent->hasKey("_light")) {
				startingSkyColor = ent->keyvalues["_light"];
			}
		}
	}

	string lastSky = startingSky;
	string lastSkyColor = startingSkyColor;
	for (int i = 1; i < mapOrder.size(); i++) {
		string skyname = "desert";
		string skyColor = "0 0 0 0";
		for (int k = 0; k < sourceMaps[i].map->ents.size(); k++) {
			Entity* ent = sourceMaps[i].map->ents[k];
			if (ent->keyvalues["classname"] == "worldspawn") {
				if (ent->hasKey("skyname")) {
					skyname = toLowerCase(ent->keyvalues["skyname"]);
				}
			}
			if (ent->keyvalues["classname"] == "light_environment") {
				if (ent->hasKey("_light")) {
					skyColor = ent->keyvalues["_light"];
				}
			}
		}

		bool skyColorChanged = skyColor != lastSkyColor;

		if (skyname != lastSky || skyColorChanged) {
			Entity* changesky = new Entity();
			changesky->addKeyvalue("origin", changesky_origin.toKeyvalueString());
			changesky->addKeyvalue("targetname", "bspguy_start_" + toLowerCase(mapOrder[i]->name));
			if (skyname != lastSky) {
				changesky->addKeyvalue("skyname", skyname.c_str());
			}
			if (skyColorChanged) {
				changesky->addKeyvalue("color", skyColor.c_str());
			}
			changesky->addKeyvalue("spawnflags", "5"); // all players + update server
			changesky->addKeyvalue("classname", "trigger_changesky");
			mergedMap->ents.push_back(changesky);
			changesky_origin.z += 18;
			lastSky = skyname;
			lastSkyColor = skyColor;
		}
	}

	// add dummy equipment logic, to save some copy-paste work.
	// They'll do nothing until weapons keyvalues are added
	// TODO: parse CFG files and set equipment automatically
	for (int i = 0; i < mapOrder.size(); i++) {
		Entity* equip = new Entity();
		Entity* relay = new Entity();
		string mapname = toLowerCase(mapOrder[i]->name);
		//string equip_name = "equip_" + mapname;

		equip->addKeyvalue("origin", equip_origin.toKeyvalueString());
		equip->addKeyvalue("targetname", "equip_" + mapname);
		equip->addKeyvalue("respawn_equip_mode", "1"); // always equip respawning players
		equip->addKeyvalue("ammo_equip_mode", "1"); // restock ammo up to CFG default
		equip->addKeyvalue("$s_bspguy_map_source", mapname);

		// 1 = equip all on trigger to get new weapons for new sections
		// 2 = force all flag. Set for first map so that you spawn with the most powerful weapon equipped
		//     when starting a listen server (needed because ent is activated after the host spawns).
		equip->addKeyvalue("spawnflags", i == 0 ? "3" : "1");

		equip->addKeyvalue("classname", "bspguy_equip");

		relay->addKeyvalue("origin", (equip_origin + vec3(0, 18, 0)).toKeyvalueString());
		relay->addKeyvalue("targetname", "bspguy_start_" + mapname);
		relay->addKeyvalue("target", "equip_" + mapname); // add new weapons when the map starts
		relay->addKeyvalue("triggerstate", "1");
		relay->addKeyvalue("classname", "trigger_relay");

		mergedMap->ents.push_back(equip);
		mergedMap->ents.push_back(relay);

		equip_origin.z += 18;
	}

	int replaced_changelevels = 0;
	int updated_spawns = 0;
	int updated_monsters = 0;

	for (int i = 0; i < originalEntCount; i++) {
		Entity* ent = mergedMap->ents[i];
		string cname = ent->keyvalues["classname"];
		string tname = ent->keyvalues["targetname"];
		string source_map = ent->keyvalues["$s_bspguy_map_source"];
		int spawnflags = atoi(ent->keyvalues["spawnflags"].c_str());
		bool isInFirstMap = toLowerCase(source_map) == toLowerCase(firstMapName);
		vec3 origin;

		if (cname == "worldspawn") {
			ent->removeKeyvalue("$s_bspguy_map_source");
			continue;
		}

		if (ent->hasKey("origin")) {
			origin = Keyvalue("origin", ent->keyvalues["origin"]).getVector();
		}
		if (ent->isBspModel()) {
			origin = mergedMap->get_model_center(ent->getBspModelIdx());
		}

		if (noscript && (cname == "info_player_start" || cname == "info_player_coop" || cname == "info_player_dm2")) {
			// info_player_start ents are ignored if there is any active info_player_deathmatch,
			// so this may break spawns if there are a mix of spawn types
			cname = ent->keyvalues["classname"] = "info_player_deathmatch";
		}

		if (noscript && !isInFirstMap) {
			if (cname == "info_player_deathmatch" && !(spawnflags & 2)) { // not start off
				// disable spawns in all but the first map
				ent->setOrAddKeyvalue("spawnflags", to_string(spawnflags | 2));

				if (tname.empty()) {
					tname = "bspguy_spawns_" + source_map;
					ent->setOrAddKeyvalue("targetname", tname);
				}

				// re-enable when map is loading
				if (load_map_triggers[source_map].find(tname) == load_map_triggers[source_map].end()) {
					load_map_triggers[source_map].insert(tname);
					//logf << "-   Disabling spawn points in " << source_map << endl;
				}

				updated_spawns++;
			}
			if (cname == "trigger_auto") {
				ent->addKeyvalue("targetname", "bspguy_autos_" + source_map);
				ent->keyvalues["classname"] = "trigger_relay";
			}
			if (cname.find("monster_") == 0 && cname.rfind("_dead") != cname.size() - 5) {
				// replace with a squadmaker and spawn when this map section starts

				updated_monsters++;
				hashmap oldKeys = ent->keyvalues;

				string spawn_name = "bspguy_npcs_" + source_map;

				int newFlags = 4; // cyclic
				if (spawnflags & 4) newFlags = newFlags | 8; // MonsterClip
				if (spawnflags & 16) newFlags = newFlags | 16; // prisoner
				if (spawnflags & 127) newFlags = newFlags | 128; // wait for script

				// TODO: abort if any of these are set?
				// - sqaud leader, pre-disaster, wait till seen, don't fade corpse
				// - apache/osprey targets, and any other monster-specific keys

				ent->clearAllKeyvalues();
				ent->addKeyvalue("origin", oldKeys["origin"]);
				ent->addKeyvalue("angles", oldKeys["angles"]);
				ent->addKeyvalue("targetname", spawn_name);
				ent->addKeyvalue("netname", oldKeys["targetname"]);
				//ent->addKeyvalue("target", "bspguy_npc_spawn_" + toLowerCase(source_map));
				if (oldKeys["rendermode"] != "0") {
					ent->addKeyvalue("renderfx", oldKeys["renderfx"]);
					ent->addKeyvalue("rendermode", oldKeys["rendermode"]);
					ent->addKeyvalue("renderamt", oldKeys["renderamt"]);
					ent->addKeyvalue("rendercolor", oldKeys["rendercolor"]);
					ent->addKeyvalue("change_rendermode", "1");
				}
				ent->addKeyvalue("classify", oldKeys["classify"]);
				ent->addKeyvalue("is_not_revivable", oldKeys["is_not_revivable"]);
				ent->addKeyvalue("bloodcolor", oldKeys["bloodcolor"]);
				ent->addKeyvalue("health", oldKeys["health"]);
				ent->addKeyvalue("minhullsize", oldKeys["minhullsize"]);
				ent->addKeyvalue("maxhullsize", oldKeys["maxhullsize"]);
				ent->addKeyvalue("freeroam", oldKeys["freeroam"]);
				ent->addKeyvalue("monstercount", "1");
				ent->addKeyvalue("delay", "0");
				ent->addKeyvalue("m_imaxlivechildren", "1");
				ent->addKeyvalue("spawn_mode", "2"); // force spawn, never block
				ent->addKeyvalue("dmg", "0"); // telefrag damage
				ent->addKeyvalue("trigger_condition", oldKeys["TriggerCondition"]);
				ent->addKeyvalue("trigger_target", oldKeys["TriggerTarget"]);
				ent->addKeyvalue("trigger_target", oldKeys["TriggerTarget"]);
				ent->addKeyvalue("notsolid", "-1");
				ent->addKeyvalue("gag", (spawnflags & 2) ? "1" : "0");
				ent->addKeyvalue("weapons", oldKeys["weapons"]);
				ent->addKeyvalue("new_body", oldKeys["body"]);
				ent->addKeyvalue("respawn_as_playerally", oldKeys["is_player_ally"]);
				ent->addKeyvalue("monstertype", oldKeys["classname"]);
				ent->addKeyvalue("displayname", oldKeys["displayname"]);
				ent->addKeyvalue("squadname", oldKeys["netname"]);
				ent->addKeyvalue("new_model", oldKeys["model"]);
				ent->addKeyvalue("soundlist", oldKeys["soundlist"]);
				ent->addKeyvalue("path_name", oldKeys["path_name"]);
				ent->addKeyvalue("guard_ent", oldKeys["guard_ent"]);
				ent->addKeyvalue("$s_bspguy_map_source", oldKeys["$s_bspguy_map_source"]);
				ent->addKeyvalue("spawnflags", to_string(newFlags));
				ent->addKeyvalue("classname", "squadmaker");
				ent->clearEmptyKeyvalues(); // things like the model keyvalue will break the monster if it's set but empty

				// re-enable when map is loading
				if (load_map_triggers[source_map].find(spawn_name) == load_map_triggers[source_map].end()) {
					load_map_triggers[source_map].insert(spawn_name);
					//logf << "-   Disabling monster_* in " << source_map << endl;
				}
			}

			g_progress.tick();
		}

		if (cname == "trigger_changelevel") {
			replaced_changelevels++;

			string map = toLowerCase(ent->keyvalues["map"]);
			bool isMergedMap = false;
			for (int i = 0; i < sourceMaps.size(); i++) {
				if (map == toLowerCase(sourceMaps[i].map->name)) {
					isMergedMap = true;
				}
			}
			if (!isMergedMap) {
				continue; // probably the last map in the merge set
			}

			string newTriggerTarget = noscript ? load_section_prefix + map : "bspguy_mapchange";

			// TODO: keep_inventory flag?

			if (spawnflags & 2 && tname.empty())
				logf("\nWarning: use-only trigger_changelevel has no targetname\n");

			if (!(spawnflags & 2)) {
				string model = ent->keyvalues["model"];

				string oldOrigin = ent->keyvalues["origin"];
				ent->clearAllKeyvalues();
				ent->addKeyvalue("origin", oldOrigin);
				ent->addKeyvalue("model", model);
				ent->addKeyvalue("target", newTriggerTarget);
				ent->addKeyvalue("$s_next_map", map);
				ent->addKeyvalue("$s_bspguy_map_source", source_map);
				ent->addKeyvalue("classname", "trigger_once");
			}
			if (!tname.empty()) { // USE Only
				Entity* relay = ent;

				if (!(spawnflags & 2)) {
					relay = new Entity();
					mergedMap->ents.push_back(relay);
				}

				relay->clearAllKeyvalues();
				relay->addKeyvalue("origin", origin.toKeyvalueString());
				relay->addKeyvalue("targetname", tname);
				relay->addKeyvalue("target", newTriggerTarget);
				relay->addKeyvalue("spawnflags", "1"); // remove on fire
				relay->addKeyvalue("triggerstate", "0");
				relay->addKeyvalue("delay", "0");
				relay->addKeyvalue("$s_next_map", map);
				relay->addKeyvalue("$s_bspguy_map_source", source_map);
				relay->addKeyvalue("classname", "trigger_relay");
			}

			if (noscript) {
				string cleanup_iter = "bspguy_clean_" + source_map;
				string cleanup_check1 = "bspguy_clean2_" + source_map;
				string cleanup_check2 = "bspguy_clean3_" + source_map;
				string cleanup_check3 = "bspguy_clean4_" + source_map;
				string cleanup_check4 = "bspguy_clean5_" + source_map;
				string cleanup_check5 = "bspguy_clean6_" + source_map;
				string cleanup_check6 = "bspguy_clean7_" + source_map;
				string cleanup_setval = "bspguy_clean8_" + source_map;
				// ".ent_create trigger_changevalue "targetname:kill_me:target:!activator:m_iszValueName:targetname:m_iszNewValue:bee_gun:message:kill_me2"

				if (load_map_triggers[map].find(cleanup_iter) == load_map_triggers[map].end()) {
					load_map_triggers[map].insert(cleanup_iter);

					MAPBLOCK sourceMap = mapsByName[toLowerCase(source_map)];

					vec3 map_min = sourceMap.mins + sourceMap.offset;
					vec3 map_max = sourceMap.maxs + sourceMap.offset;

					string cond_use_x = to_string(96 + 4 + 8 + 16);
					string cond_use_y = to_string(96 + 2 + 8 + 16);
					string cond_use_z = to_string(96 + 2 + 4 + 16);
					vec3 entOrigin = origin;

					// delete entities in this map
					{	// kill spawn points ASAP so everyone can respawn in the new map right away
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_iter);
						cleanup_ent->addKeyvalue("classname_filter", "info_player_*");
						cleanup_ent->addKeyvalue("target", cleanup_check1);
						cleanup_ent->addKeyvalue("triggerstate", "2"); // toggle
						cleanup_ent->addKeyvalue("delay_between_triggers", "0.0");
						cleanup_ent->addKeyvalue("trigger_after_run", "bspguy_finish_clean");
						cleanup_ent->addKeyvalue("classname", "trigger_entity_iterator");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 28;
					}
					{	// kill monster entities in the map slower to reduce lag
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_iter);
						cleanup_ent->addKeyvalue("classname_filter", "monster_*");
						cleanup_ent->addKeyvalue("target", cleanup_check1);
						cleanup_ent->addKeyvalue("triggerstate", "2"); // toggle
						cleanup_ent->addKeyvalue("delay_between_triggers", "0.0");
						cleanup_ent->addKeyvalue("trigger_after_run", "bspguy_finish_clean");
						cleanup_ent->addKeyvalue("classname", "trigger_entity_iterator");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 24;
					}
					{	// check if entity is within bounds (min x)
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_check1);
						cleanup_ent->addKeyvalue("target", "!activator");
						cleanup_ent->addKeyvalue("m_iszValueName", "origin");
						cleanup_ent->addKeyvalue("m_iszCheckValue", to_string((int)map_min.x));
						cleanup_ent->addKeyvalue("m_iCheckType", "3"); // greater
						cleanup_ent->addKeyvalue("netname", cleanup_check2); // true case
						cleanup_ent->addKeyvalue("spawnflags", cond_use_x); // cyclic + keep !activator
						cleanup_ent->addKeyvalue("classname", "trigger_condition");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 18;
					}
					{	// check if entity is within bounds (min y)
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_check2);
						cleanup_ent->addKeyvalue("target", "!activator");
						cleanup_ent->addKeyvalue("m_iszValueName", "origin");
						cleanup_ent->addKeyvalue("m_iszCheckValue", to_string((int)map_min.y));
						cleanup_ent->addKeyvalue("m_iCheckType", "3"); // greater
						cleanup_ent->addKeyvalue("netname", cleanup_check3); // true case
						cleanup_ent->addKeyvalue("spawnflags", cond_use_y); // cyclic + keep !activator
						cleanup_ent->addKeyvalue("classname", "trigger_condition");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 18;
					}
					{	// check if entity is within bounds (min z)
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_check3);
						cleanup_ent->addKeyvalue("target", "!activator");
						cleanup_ent->addKeyvalue("m_iszValueName", "origin");
						cleanup_ent->addKeyvalue("m_iszCheckValue", to_string((int)map_min.z));
						cleanup_ent->addKeyvalue("m_iCheckType", "3"); // greater
						cleanup_ent->addKeyvalue("netname", cleanup_check4); // true case
						cleanup_ent->addKeyvalue("spawnflags", cond_use_z); // cyclic + keep !activator
						cleanup_ent->addKeyvalue("classname", "trigger_condition");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 18;
					}
					{	// check if entity is within bounds (max x)
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_check4);
						cleanup_ent->addKeyvalue("target", "!activator");
						cleanup_ent->addKeyvalue("m_iszValueName", "origin");
						cleanup_ent->addKeyvalue("m_iszCheckValue", to_string((int)map_max.x));
						cleanup_ent->addKeyvalue("m_iCheckType", "2"); // less
						cleanup_ent->addKeyvalue("netname", cleanup_check5); // true case
						cleanup_ent->addKeyvalue("spawnflags", cond_use_x); // cyclic + keep !activator
						cleanup_ent->addKeyvalue("classname", "trigger_condition");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 18;
					}
					{	// check if entity is within bounds (max y)
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_check5);
						cleanup_ent->addKeyvalue("target", "!activator");
						cleanup_ent->addKeyvalue("m_iszValueName", "origin");
						cleanup_ent->addKeyvalue("m_iszCheckValue", to_string((int)map_max.y));
						cleanup_ent->addKeyvalue("m_iCheckType", "2"); // less
						cleanup_ent->addKeyvalue("netname", cleanup_check6); // true case
						cleanup_ent->addKeyvalue("spawnflags", cond_use_y); // cyclic + keep !activator
						cleanup_ent->addKeyvalue("classname", "trigger_condition");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 18;
					}
					{	// check if entity is within bounds (max z)
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_check6);
						cleanup_ent->addKeyvalue("target", "!activator");
						cleanup_ent->addKeyvalue("m_iszValueName", "origin");
						cleanup_ent->addKeyvalue("m_iszCheckValue", to_string((int)map_max.z));
						cleanup_ent->addKeyvalue("m_iCheckType", "2"); // less
						cleanup_ent->addKeyvalue("netname", cleanup_setval); // true case
						cleanup_ent->addKeyvalue("spawnflags", cond_use_z); // cyclic + keep !activator
						cleanup_ent->addKeyvalue("classname", "trigger_condition");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 18;
					}
					{	// mark the entity for killing
						Entity* cleanup_ent = new Entity();
						cleanup_ent->addKeyvalue("origin", entOrigin.toKeyvalueString());
						cleanup_ent->addKeyvalue("targetname", cleanup_setval);
						cleanup_ent->addKeyvalue("target", "!activator");
						cleanup_ent->addKeyvalue("m_iszValueName", "targetname");
						cleanup_ent->addKeyvalue("m_iszNewValue", "bspguy_kill_me");
						//cleanup_ent->addKeyvalue("message", "bspguy_test");
						cleanup_ent->addKeyvalue("classname", "trigger_changevalue");
						mergedMap->ents.push_back(cleanup_ent);
						entOrigin.z += 18;
					}
				}
			}
		}
	}

	if (noscript) {
		Entity* respawn_all_ent = new Entity();
		respawn_all_ent->addKeyvalue("targetname", "bspguy_respawn_everyone");
		respawn_all_ent->addKeyvalue("classname", "trigger_respawn");
		respawn_all_ent->addKeyvalue("origin", "64 64 0");
		mergedMap->ents.push_back(respawn_all_ent);

		Entity* finish_clean_ent = new Entity();
		finish_clean_ent->addKeyvalue("targetname", "bspguy_finish_clean");
		//finish_clean_ent->addKeyvalue("bspguy_test", "0");
		finish_clean_ent->addKeyvalue("bspguy_kill_me", "0#2"); // kill ents in previous map
		finish_clean_ent->addKeyvalue("classname", "multi_manager");
		finish_clean_ent->addKeyvalue("origin", "64 64 32");
		mergedMap->ents.push_back(finish_clean_ent);

		/*
		{
			Entity* cleanup_ent = new Entity();
			cleanup_ent->addKeyvalue("targetname", "bspguy_test");
			cleanup_ent->addKeyvalue("message", "OMG BSPGUY TEST");
			cleanup_ent->addKeyvalue("spawnflags", "1");
			cleanup_ent->addKeyvalue("classname", "game_text");
			mergedMap->ents.push_back(cleanup_ent);
		}
		*/

		vec3 map_setup_origin = vec3(64, -64, 0);
		for (auto it = load_map_triggers.begin(); it != load_map_triggers.end(); ++it) {
			Entity* map_setup = new Entity();

			map_setup->addKeyvalue("origin", map_setup_origin.toKeyvalueString());

			int triggerCount = 0;
			for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
				map_setup->addKeyvalue(*it2, "0");
				triggerCount++;
			}

			map_setup->addKeyvalue("bspguy_respawn_everyone", "1"); // respawn in new spots
			map_setup->addKeyvalue("bspguy_autos_" + it->first, "1"); // fire what used to be trigger_auto
			map_setup->addKeyvalue("targetname", load_section_prefix + it->first);
			map_setup->addKeyvalue("classname", "multi_manager");


			mergedMap->ents.push_back(map_setup);

			map_setup_origin.z += 18;
		}
	}

	g_progress.clear();

	logf("    Replaced %d level transitions\n", replaced_changelevels);
	logf("    Updated %d spawn points\n", updated_spawns);
	logf("    Replaced %d monster_* ents with squadmakers\n", updated_monsters);
	logf("    Renamed %d entities to prevent conflicts between map sections\n", renameCount);

	mergedMap->update_ent_lump();

	if (!noscript) {
		ofstream entFile(output_name + ".ent", ios::trunc);
		entFile.write((const char*)mergedMap->lumps[LUMP_ENTITIES], mergedMap->header.lump[LUMP_ENTITIES].nLength - 1);
	}
}

int BspMerger::force_unique_ent_names_per_map(Bsp* mergedMap) {
	mapStringToSet mapEntNames;
	mapStringToSet entsToRename;

	for (int i = 0; i < mergedMap->ents.size(); i++) {
		Entity* ent = mergedMap->ents[i];
		string tname = ent->keyvalues["targetname"];
		string source_map = ent->keyvalues["$s_bspguy_map_source"];

		if (tname.empty())
			continue;

		bool isUnique = true;
		for (auto it = mapEntNames.begin(); it != mapEntNames.end(); ++it) {
			if (it->first != source_map && it->second.find(tname) != it->second.end()) {
				entsToRename[source_map].insert(tname);
				isUnique = false;
				break;
			}
		}

		if (isUnique)
			mapEntNames[source_map].insert(tname);
	}

	int renameCount = 0;
	for (auto it = entsToRename.begin(); it != entsToRename.end(); ++it)
		renameCount += it->second.size();

	g_progress.update("Renaming entities", renameCount);

	int renameSuffix = 2;
	for (auto it = entsToRename.begin(); it != entsToRename.end(); ++it) {
		for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
			string oldName = *it2;
			string newName = oldName + "_" + to_string(renameSuffix++);

			//logf << "\nRenaming " << *it2 << " to " << newName << endl;

			for (int i = 0; i < mergedMap->ents.size(); i++) {
				Entity* ent = mergedMap->ents[i];
				if (ent->keyvalues["$s_bspguy_map_source"] != it->first)
					continue;

				ent->renameTargetnameValues(oldName, newName);
			}

			g_progress.tick();
		}
	}

	return renameCount;
}

bool BspMerger::merge(Bsp& mapA, Bsp& mapB) {
	// TODO: Create a new map and store result there. Don't break mapA.

	BSPPLANE separationPlane = separate(mapA, mapB);
	if (separationPlane.nType == -1) {
		logf("No separating axis found. The maps overlap and can't be merged.\n");
		return false;
	}
	thisWorldLeafCount = mapA.models[0].nVisLeafs; // excludes solid leaf 0
	otherWorldLeafCount = mapB.models[0].nVisLeafs; // excluding solid leaf 0

	texRemap.clear();
	texInfoRemap.clear();
	planeRemap.clear();
	leavesRemap.clear();
	modelLeafRemap.clear();

	bool shouldMerge[HEADER_LUMPS] = { false };

	for (int i = 0; i < HEADER_LUMPS; i++) {

		if (i == LUMP_VISIBILITY || i == LUMP_LIGHTING) {
			continue; // always merge
		}

		if (!mapA.lumps[i] && !mapB.lumps[i]) {
			//logf << "Skipping " << g_lump_names[i] << " lump (missing from both maps)\n";
		}
		else if (!mapA.lumps[i]) {
			logf("Replacing %s lump\n", g_lump_names[i]);
			mapA.header.lump[i].nLength = mapB.header.lump[i].nLength;
			mapA.lumps[i] = new byte[mapB.header.lump[i].nLength];
			memcpy(mapA.lumps[i], mapB.lumps[i], mapB.header.lump[i].nLength);

			// process the lump here (TODO: faster to just copy wtv needs copying)
			switch (i) {
			case LUMP_ENTITIES:
				mapA.load_ents(); break;
			}
		}
		else if (!mapB.lumps[i]) {
			logf("Keeping %s lump\n", g_lump_names[i]);
		}
		else {
			//logf << "Merging " << g_lump_names[i] << " lump\n";

			shouldMerge[i] = true;
		}
	}

	// base structures (they don't reference any other structures)
	if (shouldMerge[LUMP_ENTITIES])
		merge_ents(mapA, mapB);
	if (shouldMerge[LUMP_PLANES])
		merge_planes(mapA, mapB);
	if (shouldMerge[LUMP_TEXTURES])
		merge_textures(mapA, mapB);
	if (shouldMerge[LUMP_VERTICES])
		merge_vertices(mapA, mapB);

	if (shouldMerge[LUMP_EDGES])
		merge_edges(mapA, mapB); // references verts

	if (shouldMerge[LUMP_SURFEDGES])
		merge_surfedges(mapA, mapB); // references edges

	if (shouldMerge[LUMP_TEXINFO])
		merge_texinfo(mapA, mapB); // references textures

	if (shouldMerge[LUMP_FACES])
		merge_faces(mapA, mapB); // references planes, surfedges, and texinfo

	if (shouldMerge[LUMP_MARKSURFACES])
		merge_marksurfs(mapA, mapB); // references faces

	if (shouldMerge[LUMP_LEAVES])
		merge_leaves(mapA, mapB); // references vis data, and marksurfs

	if (shouldMerge[LUMP_NODES]) {
		create_merge_headnodes(mapA, mapB, separationPlane);
		merge_nodes(mapA, mapB);
		merge_clipnodes(mapA, mapB);
	}

	if (shouldMerge[LUMP_MODELS])
		merge_models(mapA, mapB);

	merge_lighting(mapA, mapB);

	// doing this last because it takes way longer than anything else, and limit overflows should fail the
	// merge as soon as possible. // TODO: fail fast if overflow detected in other merges? Kind ni
	merge_vis(mapA, mapB);

	g_progress.clear();

	return true;
}

BSPPLANE BspMerger::separate(Bsp& mapA, Bsp& mapB) {
	BSPMODEL& thisWorld = mapA.models[0];
	BSPMODEL& otherWorld = mapB.models[0];

	vec3 amin = thisWorld.nMins;
	vec3 amax = thisWorld.nMaxs;
	vec3 bmin = otherWorld.nMins;
	vec3 bmax = otherWorld.nMaxs;

	BSPPLANE separationPlane;
	memset(&separationPlane, 0, sizeof(BSPPLANE));

	// separating plane points toward the other map (b)
	if (bmin.x >= amax.x) {
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = { 1, 0, 0 };
		separationPlane.fDist = amax.x + (bmin.x - amax.x) * 0.5f;
	}
	else if (bmax.x <= amin.x) {
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = { -1, 0, 0 };
		separationPlane.fDist = bmax.x + (amin.x - bmax.x) * 0.5f;
	}
	else if (bmin.y >= amax.y) {
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, 1, 0 };
		separationPlane.fDist = bmin.y;
	}
	else if (bmax.y <= amin.y) {
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, -1, 0 };
		separationPlane.fDist = bmax.y;
	}
	else if (bmin.z >= amax.z) {
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, 1 };
		separationPlane.fDist = bmin.z;
	}
	else if (bmax.z <= amin.z) {
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, -1 };
		separationPlane.fDist = bmax.z;
	}
	else {
		separationPlane.nType = -1; // no simple separating axis

		logf("Bounding boxes for each map:\n");
		logf("(%6.0f, %6.0f, %6.0f)", amin.x, amin.y, amin.z);
		logf(" - (%6.0f, %6.0f, %6.0f) %s\n", amax.x, amax.y, amax.z, mapA.name.c_str());

		logf("(%6.0f, %6.0f, %6.0f)", bmin.x, bmin.y, bmin.z);
		logf(" - (%6.0f, %6.0f, %6.0f) %s\n", bmax.x, bmax.y, bmax.z, mapB.name.c_str());
	}

	return separationPlane;
}

void BspMerger::merge_ents(Bsp& mapA, Bsp& mapB)
{
	g_progress.update("Merging entities", mapA.ents.size() + mapB.ents.size());

	int oldEntCount = mapA.ents.size();

	// update model indexes since this map's models will be appended after the other map's models
	int otherModelCount = (mapB.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) - 1;
	for (int i = 0; i < mapA.ents.size(); i++) {
		if (!mapA.ents[i]->hasKey("model") || mapA.ents[i]->keyvalues["model"][0] != '*') {
			continue;
		}
		string modelIdxStr = mapA.ents[i]->keyvalues["model"].substr(1);

		if (!isNumeric(modelIdxStr)) {
			continue;
		}

		int newModelIdx = atoi(modelIdxStr.c_str()) + otherModelCount;
		mapA.ents[i]->keyvalues["model"] = "*" + to_string(newModelIdx);

		g_progress.tick();
	}

	for (int i = 0; i < mapB.ents.size(); i++) {
		if (mapB.ents[i]->keyvalues["classname"] == "worldspawn") {
			Entity* otherWorldspawn = mapB.ents[i];

			vector<string> otherWads = splitString(otherWorldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < otherWads.size(); j++) {
				otherWads[j] = basename(otherWads[j]);
			}

			Entity* worldspawn = NULL;
			for (int k = 0; k < mapA.ents.size(); k++) {
				if (mapA.ents[k]->keyvalues["classname"] == "worldspawn") {
					worldspawn = mapA.ents[k];
					break;
				}
			}

			// merge wad list
			vector<string> thisWads = splitString(worldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < thisWads.size(); j++) {
				thisWads[j] = basename(thisWads[j]);
			}

			// add unique wads to this map
			for (int j = 0; j < otherWads.size(); j++) {
				if (std::find(thisWads.begin(), thisWads.end(), otherWads[j]) == thisWads.end()) {
					thisWads.push_back(otherWads[j]);
				}
			}

			worldspawn->keyvalues["wad"] = "";
			for (int j = 0; j < thisWads.size(); j++) {
				worldspawn->keyvalues["wad"] += thisWads[j] + ";";
			}

			// include prefixed version of the other maps keyvalues
			for (auto it = otherWorldspawn->keyvalues.begin(); it != otherWorldspawn->keyvalues.end(); it++) {
				if (it->first == "classname" || it->first == "wad") {
					continue;
				}
				// TODO: unknown keyvalues crash the game? Try something else.
				//worldspawn->addKeyvalue(Keyvalue(mapB.name + "_" + it->first, it->second));
			}
		}
		else {
			Entity* copy = new Entity();
			copy->keyvalues = mapB.ents[i]->keyvalues;
			copy->keyOrder = mapB.ents[i]->keyOrder;
			mapA.ents.push_back(copy);
		}

		g_progress.tick();
	}

	mapA.update_ent_lump();
}

void BspMerger::merge_planes(Bsp& mapA, Bsp& mapB) {
	g_progress.update("Merging planes", mapA.planeCount + mapB.planeCount);

	vector<BSPPLANE> mergedPlanes;
	mergedPlanes.reserve(mapA.planeCount + mapB.planeCount);

	for (int i = 0; i < mapA.planeCount; i++) {
		mergedPlanes.push_back(mapA.planes[i]);
		g_progress.tick();
	}
	for (int i = 0; i < mapB.planeCount; i++) {
		bool isUnique = true;
		for (int k = 0; k < mapA.planeCount; k++) {
			if (memcmp(&mapB.planes[i], &mapA.planes[k], sizeof(BSPPLANE)) == 0) {
				isUnique = false;
				planeRemap.push_back(k);
				break;
			}
		}
		if (isUnique) {
			planeRemap.push_back(mergedPlanes.size());
			mergedPlanes.push_back(mapB.planes[i]);
		}

		g_progress.tick();
	}

	int newLen = mergedPlanes.size() * sizeof(BSPPLANE);
	int duplicates = (mapA.planeCount + mapB.planeCount) - mergedPlanes.size();

	//logf("\nRemoved %d duplicate planes\n", duplicates);

	byte* newPlanes = new byte[newLen];
	memcpy(newPlanes, &mergedPlanes[0], newLen);

	mapA.replace_lump(LUMP_PLANES, newPlanes, newLen);
}

void BspMerger::merge_textures(Bsp& mapA, Bsp& mapB) {
	uint32_t newTexCount = 0;

	// temporary buffer for holding miptex + embedded textures (too big but doesn't matter)
	uint maxMipTexDataSize = mapA.header.lump[LUMP_TEXTURES].nLength + mapB.header.lump[LUMP_TEXTURES].nLength;
	byte* newMipTexData = new byte[maxMipTexDataSize];

	byte* mipTexWritePtr = newMipTexData;

	// offsets relative to the start of the mipmap data, not the lump
	uint32_t* mipTexOffsets = new uint32_t[mapA.textureCount + mapB.textureCount];

	g_progress.update("Merging textures", mapA.textureCount + mapB.textureCount);

	uint thisMergeSz = (mapA.textureCount + 1) * sizeof(int32_t);
	for (int i = 0; i < mapA.textureCount; i++) {
		int32_t offset = ((int32_t*)mapA.textures)[i + 1];

		if (offset == -1) {
			mipTexOffsets[newTexCount] = -1;
		}
		else {
			BSPMIPTEX* tex = (BSPMIPTEX*)(mapA.textures + offset);
			int sz = getBspTextureSize(tex);
			//memset(tex->nOffsets, 0, sizeof(uint32) * 4);

			mipTexOffsets[newTexCount] = (mipTexWritePtr - newMipTexData);
			memcpy(mipTexWritePtr, tex, sz);
			mipTexWritePtr += sz;
			thisMergeSz += sz;
		}
		newTexCount++;

		g_progress.tick();
	}

	uint otherMergeSz = (mapB.textureCount + 1) * sizeof(int32_t);
	for (int i = 0; i < mapB.textureCount; i++) {
		int32_t offset = ((int32_t*)mapB.textures)[i + 1];

		if (offset != -1) {
			bool isUnique = true;
			BSPMIPTEX* tex = (BSPMIPTEX*)(mapB.textures + offset);
			int sz = getBspTextureSize(tex);

			for (int k = 0; k < mapA.textureCount; k++) {
				if (mipTexOffsets[k] == -1) {
					continue;
				}
				BSPMIPTEX* thisTex = (BSPMIPTEX*)(newMipTexData + mipTexOffsets[k]);
				if (memcmp(tex, thisTex, sz) == 0) {
					isUnique = false;
					texRemap.push_back(k);
					break;
				}
			}

			if (isUnique) {
				mipTexOffsets[newTexCount] = (mipTexWritePtr - newMipTexData);
				texRemap.push_back(newTexCount);
				memcpy(mipTexWritePtr, tex, sz); // Note: won't work if pixel data isn't immediately after struct
				mipTexWritePtr += sz;
				newTexCount++;
				otherMergeSz += sz;
			}
		}
		else {
			mipTexOffsets[newTexCount] = -1;
			texRemap.push_back(newTexCount);
			newTexCount++;
		}

		g_progress.tick();
	}

	int duplicates = newTexCount - (mapA.textureCount + mapB.textureCount);

	uint texHeaderSize = (newTexCount + 1) * sizeof(int32_t);
	uint newLen = (mipTexWritePtr - newMipTexData) + texHeaderSize;
	byte* newTextureData = new byte[newLen];

	// write texture lump header
	uint32_t* texHeader = (uint32_t*)(newTextureData);
	texHeader[0] = newTexCount;
	for (int i = 0; i < newTexCount; i++) {
		texHeader[i + 1] = (mipTexOffsets[i] == -1) ? -1 : mipTexOffsets[i] + texHeaderSize;
	}

	memcpy(newTextureData + texHeaderSize, newMipTexData, mipTexWritePtr - newMipTexData);

	delete[] mipTexOffsets;
	mapA.replace_lump(LUMP_TEXTURES, newTextureData, newLen);
}

void BspMerger::merge_vertices(Bsp& mapA, Bsp& mapB) {
	thisVertCount = mapA.vertCount;
	int totalVertCount = thisVertCount + mapB.vertCount;

	g_progress.update("Merging verticies", 3);
	g_progress.tick();

	vec3* newVerts = new vec3[totalVertCount];
	memcpy(newVerts, mapA.verts, thisVertCount * sizeof(vec3));
	g_progress.tick();
	memcpy(newVerts + thisVertCount, mapB.verts, mapB.vertCount * sizeof(vec3));
	g_progress.tick();

	mapA.replace_lump(LUMP_VERTICES, newVerts, totalVertCount * sizeof(vec3));
}

void BspMerger::merge_texinfo(Bsp& mapA, Bsp& mapB) {
	g_progress.update("Merging texinfos", mapA.texinfoCount + mapB.texinfoCount);

	vector<BSPTEXTUREINFO> mergedInfo;
	mergedInfo.reserve(mapA.texinfoCount + mapB.texinfoCount);

	for (int i = 0; i < mapA.texinfoCount; i++) {
		mergedInfo.push_back(mapA.texinfos[i]);
		g_progress.tick();
	}

	for (int i = 0; i < mapB.texinfoCount; i++) {
		BSPTEXTUREINFO info = mapB.texinfos[i];
		info.iMiptex = texRemap[info.iMiptex];

		bool isUnique = true;
		for (int k = 0; k < mapA.texinfoCount; k++) {
			if (memcmp(&info, &mapA.texinfos[k], sizeof(BSPTEXTUREINFO)) == 0) {
				texInfoRemap.push_back(k);
				isUnique = false;
				break;
			}
		}

		if (isUnique) {
			texInfoRemap.push_back(mergedInfo.size());
			mergedInfo.push_back(info);
		}
		g_progress.tick();
	}

	int newLen = mergedInfo.size() * sizeof(BSPTEXTUREINFO);
	int duplicates = mergedInfo.size() - (mapA.texinfoCount + mapB.texinfoCount);

	byte* newTexinfoData = new byte[newLen];
	memcpy(newTexinfoData, &mergedInfo[0], newLen);

	mapA.replace_lump(LUMP_TEXINFO, newTexinfoData, newLen);
}

void BspMerger::merge_faces(Bsp& mapA, Bsp& mapB) {
	thisFaceCount = mapA.faceCount;
	otherFaceCount = mapB.faceCount;
	thisWorldFaceCount = mapA.models[0].nFaces;
	int totalFaceCount = thisFaceCount + mapB.faceCount;

	g_progress.update("Merging faces", mapB.faceCount + 1);
	g_progress.tick();

	BSPFACE* newFaces = new BSPFACE[totalFaceCount];

	// world model faces come first so they can be merged into one group (model.nFaces is used to render models)
	// assumes world model faces always come first
	int appendOffset = 0;
	// copy world faces
	int worldFaceCountA = thisWorldFaceCount;
	int worldFaceCountB = mapB.models[0].nFaces;
	memcpy(newFaces + appendOffset, mapA.faces, worldFaceCountA * sizeof(BSPFACE));
	appendOffset += worldFaceCountA;
	memcpy(newFaces + appendOffset, mapB.faces, worldFaceCountB * sizeof(BSPFACE));
	appendOffset += worldFaceCountB;

	// copy B's submodel faces followed by A's
	int submodelFaceCountA = mapA.faceCount - worldFaceCountA;
	int submodelFaceCountB = mapB.faceCount - worldFaceCountB;
	memcpy(newFaces + appendOffset, mapB.faces + worldFaceCountB, submodelFaceCountB * sizeof(BSPFACE));
	appendOffset += submodelFaceCountB;
	memcpy(newFaces + appendOffset, mapA.faces + worldFaceCountA, submodelFaceCountA * sizeof(BSPFACE));

	for (int i = 0; i < totalFaceCount; i++) {
		// only update B's faces
		if (i < worldFaceCountA || i >= worldFaceCountA + mapB.faceCount)
			continue;

		BSPFACE& face = newFaces[i];
		face.iPlane = planeRemap[face.iPlane];
		face.iFirstEdge = face.iFirstEdge + thisSurfEdgeCount;
		face.iTextureInfo = texInfoRemap[face.iTextureInfo];
		g_progress.tick();
	}

	mapA.replace_lump(LUMP_FACES, newFaces, totalFaceCount * sizeof(BSPFACE));
}

void BspMerger::merge_leaves(Bsp& mapA, Bsp& mapB) {
	thisLeafCount = mapA.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	otherLeafCount = mapB.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);

	int thisWorldLeafCount = ((BSPMODEL*)mapA.lumps[LUMP_MODELS])->nVisLeafs + 1; // include solid leaf

	g_progress.update("Merging leaves", thisLeafCount + otherLeafCount);

	vector<BSPLEAF> mergedLeaves;
	mergedLeaves.reserve(thisWorldLeafCount + otherLeafCount);
	modelLeafRemap.reserve(thisWorldLeafCount + otherLeafCount);

	for (int i = 0; i < thisWorldLeafCount; i++) {
		modelLeafRemap.push_back(i);
		mergedLeaves.push_back(mapA.leaves[i]);
		g_progress.tick();
	}

	for (int i = 0; i < otherLeafCount; i++) {
		BSPLEAF& leaf = mapB.leaves[i];
		if (leaf.nMarkSurfaces) {
			leaf.iFirstMarkSurface = leaf.iFirstMarkSurface + thisMarkSurfCount;
		}

		bool isSharedSolidLeaf = i == 0;
		if (!isSharedSolidLeaf) {
			leavesRemap.push_back(mergedLeaves.size());
			mergedLeaves.push_back(leaf);
		}
		else {
			// always exclude the first solid leaf since there can only be one per map, at index 0
			leavesRemap.push_back(0);
		}
		g_progress.tick();
	}

	// append A's submodel leaves after B's world leaves
	// Order will be: A's world leaves -> B's world leaves -> B's submodel leaves -> A's submodel leaves
	for (int i = thisWorldLeafCount; i < thisLeafCount; i++) {
		modelLeafRemap.push_back(mergedLeaves.size());
		mergedLeaves.push_back(mapA.leaves[i]);
	}

	otherLeafCount -= 1; // solid leaf removed

	int newLen = mergedLeaves.size() * sizeof(BSPLEAF);

	byte* newLeavesData = new byte[newLen];
	memcpy(newLeavesData, &mergedLeaves[0], newLen);

	mapA.replace_lump(LUMP_LEAVES, newLeavesData, newLen);
}

void BspMerger::merge_marksurfs(Bsp& mapA, Bsp& mapB) {
	thisMarkSurfCount = mapA.marksurfCount;
	int totalSurfCount = thisMarkSurfCount + mapB.marksurfCount;

	g_progress.update("Merging marksurfaces", totalSurfCount + 1);
	g_progress.tick();

	uint16* newSurfs = new uint16[totalSurfCount];
	memcpy(newSurfs, mapA.marksurfs, thisMarkSurfCount * sizeof(uint16));
	memcpy(newSurfs + thisMarkSurfCount, mapB.marksurfs, mapB.marksurfCount * sizeof(uint16));

	for (int i = 0; i < thisMarkSurfCount; i++) {
		uint16& mark = newSurfs[i];
		if (mark >= thisWorldFaceCount) {
			mark = mark + otherFaceCount;
		}
		g_progress.tick();
	}

	for (int i = thisMarkSurfCount; i < totalSurfCount; i++) {
		uint16& mark = newSurfs[i];
		mark = mark + thisWorldFaceCount;
		g_progress.tick();
	}

	mapA.replace_lump(LUMP_MARKSURFACES, newSurfs, totalSurfCount * sizeof(uint16));
}

void BspMerger::merge_edges(Bsp& mapA, Bsp& mapB) {
	thisEdgeCount = mapA.header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int totalEdgeCount = thisEdgeCount + mapB.edgeCount;

	g_progress.update("Merging edges", mapB.edgeCount + 1);
	g_progress.tick();

	BSPEDGE* newEdges = new BSPEDGE[totalEdgeCount];
	memcpy(newEdges, mapA.edges, thisEdgeCount * sizeof(BSPEDGE));
	memcpy(newEdges + thisEdgeCount, mapB.edges, mapB.edgeCount * sizeof(BSPEDGE));

	for (int i = thisEdgeCount; i < totalEdgeCount; i++) {
		BSPEDGE& edge = newEdges[i];
		edge.iVertex[0] = edge.iVertex[0] + thisVertCount;
		edge.iVertex[1] = edge.iVertex[1] + thisVertCount;
		g_progress.tick();
	}

	mapA.replace_lump(LUMP_EDGES, newEdges, totalEdgeCount * sizeof(BSPEDGE));
}

void BspMerger::merge_surfedges(Bsp& mapA, Bsp& mapB) {
	thisSurfEdgeCount = mapA.surfedgeCount;
	int totalSurfCount = thisSurfEdgeCount + mapB.surfedgeCount;

	g_progress.update("Merging surfedges", mapB.edgeCount + 1);
	g_progress.tick();

	int32_t* newSurfs = new int32_t[totalSurfCount];
	memcpy(newSurfs, mapA.surfedges, thisSurfEdgeCount * sizeof(int32_t));
	memcpy(newSurfs + thisSurfEdgeCount, mapB.surfedges, mapB.surfedgeCount * sizeof(int32_t));

	for (int i = thisSurfEdgeCount; i < totalSurfCount; i++) {
		int32_t& surfEdge = newSurfs[i];
		surfEdge = surfEdge < 0 ? surfEdge - thisEdgeCount : surfEdge + thisEdgeCount;
		g_progress.tick();
	}

	mapA.replace_lump(LUMP_SURFEDGES, newSurfs, totalSurfCount * sizeof(int32_t));
}

void BspMerger::merge_nodes(Bsp& mapA, Bsp& mapB) {
	thisNodeCount = mapA.nodeCount;

	g_progress.update("Merging nodes", thisNodeCount + mapB.nodeCount);

	vector<BSPNODE> mergedNodes;
	mergedNodes.reserve(thisNodeCount + mapB.nodeCount);

	for (int i = 0; i < thisNodeCount; i++) {
		BSPNODE node = mapA.nodes[i];

		if (i > 0) { // new headnode should already be correct
			for (int k = 0; k < 2; k++) {
				if (node.iChildren[k] >= 0) {
					node.iChildren[k] += 1; // shifted from new head node
				}
				else {
					node.iChildren[k] = ~((int16_t)modelLeafRemap[~node.iChildren[k]]);
				}
			}
		}
		if (node.nFaces && node.firstFace >= thisWorldFaceCount) {
			node.firstFace += otherFaceCount;
		}

		mergedNodes.push_back(node);
		g_progress.tick();
	}

	for (int i = 0; i < mapB.nodeCount; i++) {
		BSPNODE node = mapB.nodes[i];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisNodeCount;
			}
			else {
				node.iChildren[k] = ~((int16_t)leavesRemap[~node.iChildren[k]]);
			}
		}
		node.iPlane = planeRemap[node.iPlane];
		if (node.nFaces) {
			node.firstFace += thisWorldFaceCount;
		}

		mergedNodes.push_back(node);
		g_progress.tick();
	}

	int newLen = mergedNodes.size() * sizeof(BSPNODE);

	byte* newNodeData = new byte[newLen];
	memcpy(newNodeData, &mergedNodes[0], newLen);

	mapA.replace_lump(LUMP_NODES, newNodeData, newLen);
}

void BspMerger::merge_clipnodes(Bsp& mapA, Bsp& mapB) {
	thisClipnodeCount = mapA.clipnodeCount;

	g_progress.update("Merging clipnodes", thisClipnodeCount + mapB.clipnodeCount);

	vector<BSPCLIPNODE> mergedNodes;
	mergedNodes.reserve(thisClipnodeCount + mapB.clipnodeCount);

	for (int i = 0; i < thisClipnodeCount; i++) {
		BSPCLIPNODE node = mapA.clipnodes[i];
		if (i > 2) { // new headnodes should already be correct
			for (int k = 0; k < 2; k++) {
				if (node.iChildren[k] >= 0) {
					node.iChildren[k] += MAX_MAP_HULLS - 1; // offset from new headnodes being added
				}
			}
		}
		mergedNodes.push_back(node);
		g_progress.tick();
	}

	for (int i = 0; i < mapB.clipnodeCount; i++) {
		BSPCLIPNODE node = mapB.clipnodes[i];
		node.iPlane = planeRemap[node.iPlane];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisClipnodeCount;
			}
		}
		mergedNodes.push_back(node);
		g_progress.tick();
	}

	int newLen = mergedNodes.size() * sizeof(BSPCLIPNODE);

	byte* newClipnodeData = new byte[newLen];
	memcpy(newClipnodeData, &mergedNodes[0], newLen);

	mapA.replace_lump(LUMP_CLIPNODES, newClipnodeData, newLen);
}

void BspMerger::merge_models(Bsp& mapA, Bsp& mapB) {
	g_progress.update("Merging models", mapA.modelCount + mapB.modelCount);

	vector<BSPMODEL> mergedModels;
	mergedModels.reserve(mapA.modelCount + mapB.modelCount);

	// merged world model
	mergedModels.push_back(mapA.models[0]);

	// other map's submodels
	for (int i = 1; i < mapB.modelCount; i++) {
		BSPMODEL model = mapB.models[i];
		if (model.iHeadnodes[0] >= 0)
			model.iHeadnodes[0] += thisNodeCount; // already includes new head nodes (merge_nodes comes after create_merge_headnodes)
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (model.iHeadnodes[k] >= 0)
				model.iHeadnodes[k] += thisClipnodeCount;
		}
		model.iFirstFace = model.iFirstFace + thisWorldFaceCount;
		mergedModels.push_back(model);
		g_progress.tick();
	}

	// this map's submodels
	for (int i = 1; i < mapA.modelCount; i++) {
		BSPMODEL model = mapA.models[i];
		if (model.iHeadnodes[0] >= 0)
			model.iHeadnodes[0] += 1; // adjust for new head node
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (model.iHeadnodes[k] >= 0)
				model.iHeadnodes[k] += (MAX_MAP_HULLS - 1); // adjust for new head nodes
		}
		if (model.iFirstFace >= thisWorldFaceCount) {
			model.iFirstFace += otherFaceCount;
		}
		mergedModels.push_back(model);
		g_progress.tick();
	}

	// update world head nodes
	mergedModels[0].iHeadnodes[0] = 0;
	mergedModels[0].iHeadnodes[1] = 0;
	mergedModels[0].iHeadnodes[2] = 1;
	mergedModels[0].iHeadnodes[3] = 2;
	mergedModels[0].nVisLeafs = mapA.models[0].nVisLeafs + mapB.models[0].nVisLeafs;
	mergedModels[0].nFaces = mapA.models[0].nFaces + mapB.models[0].nFaces;
	
	vec3 amin = mapA.models[0].nMins;
	vec3 bmin = mapB.models[0].nMins;
	vec3 amax = mapA.models[0].nMaxs;
	vec3 bmax = mapB.models[0].nMaxs;
	mergedModels[0].nMins = { min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z) };
	mergedModels[0].nMaxs = { max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z) };

	int newLen = mergedModels.size() * sizeof(BSPMODEL);

	byte* newModelData = new byte[newLen];
	memcpy(newModelData, &mergedModels[0], newLen);

	mapA.replace_lump(LUMP_MODELS, newModelData, newLen);
}

void BspMerger::merge_vis(Bsp& mapA, Bsp& mapB) {
	BSPLEAF* allLeaves = mapA.leaves; // combined with mapB's leaves earlier in merge_leaves

	int thisVisLeaves = thisLeafCount - 1; // VIS ignores the shared solid leaf 0
	int otherVisLeaves = otherLeafCount; // already does not include the solid leaf (see merge_leaves)
	int totalVisLeaves = thisVisLeaves + otherVisLeaves;

	int mergedWorldLeafCount = thisWorldLeafCount + otherWorldLeafCount;

	uint newVisRowSize = ((totalVisLeaves + 63) & ~63) >> 3;
	int decompressedVisSize = totalVisLeaves * newVisRowSize;

	g_progress.update("Merging visibility", thisWorldLeafCount + otherWorldLeafCount * 2 + mergedWorldLeafCount);
	g_progress.tick();

	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);

	// decompress this map's world leaves
	// model leaves don't need to be decompressed because the game ignores VIS for them.
	decompress_vis_lump(allLeaves, mapA.visdata, decompressedVis,
		thisWorldLeafCount, thisVisLeaves, totalVisLeaves);

	// decompress other map's world-leaf vis data (skip empty first leaf, which now only the first map should have)
	byte* decompressedOtherVis = decompressedVis + thisWorldLeafCount * newVisRowSize;
	decompress_vis_lump(allLeaves + thisWorldLeafCount, mapB.visdata, decompressedOtherVis,
		otherWorldLeafCount, otherLeafCount, totalVisLeaves);

	// shift mapB's world leaves after mapA's world leaves


	for (int i = 0; i < otherWorldLeafCount; i++) {
		shiftVis(decompressedOtherVis + i * newVisRowSize, newVisRowSize, 0, thisWorldLeafCount);
		g_progress.tick();
	}

	// recompress the combined vis data
	byte* compressedVis = new byte[decompressedVisSize];
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(allLeaves, decompressedVis, compressedVis, totalVisLeaves, mergedWorldLeafCount, decompressedVisSize);
	int oldLen = mapA.header.lump[LUMP_VISIBILITY].nLength;

	byte* compressedVisResize = new byte[newVisLen];
	memcpy(compressedVisResize, compressedVis, newVisLen);

	mapA.replace_lump(LUMP_VISIBILITY, compressedVisResize, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;
}

void BspMerger::merge_lighting(Bsp& mapA, Bsp& mapB) {
	COLOR3* thisRad = (COLOR3*)mapA.lightdata;
	COLOR3* otherRad = (COLOR3*)mapB.lightdata;
	bool freemem = false;

	int thisColorCount = mapA.header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int otherColorCount = mapB.header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int totalColorCount = thisColorCount + otherColorCount;
	int totalFaceCount = mapA.header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	g_progress.update("Merging lightmaps", 4 + totalFaceCount);

	// create a single full-bright lightmap to use for all faces, if one map has lighting but the other doesn't
	if (thisColorCount == 0 && otherColorCount != 0) {
		thisColorCount = MAX_SURFACE_EXTENT * MAX_SURFACE_EXTENT;
		totalColorCount += thisColorCount;
		int sz = thisColorCount * sizeof(COLOR3);
		mapA.lumps[LUMP_LIGHTING] = new byte[sz];
		mapA.header.lump[LUMP_LIGHTING].nLength = sz;
		thisRad = (COLOR3*)mapA.lumps[LUMP_LIGHTING];

		memset(thisRad, 255, sz);

		for (int i = 0; i < thisWorldFaceCount; i++) {
			mapA.faces[i].nLightmapOffset = 0;
		}
		for (int i = thisWorldFaceCount + otherFaceCount; i < totalFaceCount; i++) {
			mapA.faces[i].nLightmapOffset = 0;
		}
	}
	else if (thisColorCount != 0 && otherColorCount == 0) {
		otherColorCount = MAX_SURFACE_EXTENT * MAX_SURFACE_EXTENT;
		totalColorCount += otherColorCount;
		otherRad = new COLOR3[otherColorCount];
		freemem = true;
		memset(otherRad, 255, otherColorCount * sizeof(COLOR3));

		for (int i = thisWorldFaceCount; i < thisWorldFaceCount + otherFaceCount; i++) {
			mapA.faces[i].nLightmapOffset = 0;
		}
	}

	g_progress.tick();
	COLOR3* newRad = new COLOR3[totalColorCount];

	g_progress.tick();
	memcpy(newRad, thisRad, thisColorCount * sizeof(COLOR3));

	g_progress.tick();
	memcpy((byte*)newRad + thisColorCount * sizeof(COLOR3), otherRad, otherColorCount * sizeof(COLOR3));
	if (freemem)
	{
		delete[] otherRad;
	}
	g_progress.tick();
	mapA.replace_lump(LUMP_LIGHTING, newRad, totalColorCount * sizeof(COLOR3));

	for (int i = thisWorldFaceCount; i < thisWorldFaceCount + otherFaceCount; i++) {
		mapA.faces[i].nLightmapOffset += thisColorCount * sizeof(COLOR3);
		g_progress.tick();
	}
}

void BspMerger::create_merge_headnodes(Bsp& mapA, Bsp& mapB, BSPPLANE separationPlane) {
	BSPMODEL& thisWorld = mapA.models[0];
	BSPMODEL& otherWorld = mapB.models[0];

	vec3 amin = thisWorld.nMins;
	vec3 amax = thisWorld.nMaxs;
	vec3 bmin = otherWorld.nMins;
	vec3 bmax = otherWorld.nMaxs;

	// planes with negative normals mess up VIS and lighting stuff, so swap children instead
	bool swapNodeChildren = separationPlane.vNormal.x < 0 || separationPlane.vNormal.y < 0 || separationPlane.vNormal.z < 0;
	if (swapNodeChildren)
		separationPlane.vNormal = separationPlane.vNormal.invert();

	//logf("Separating plane: (%.0f, %.0f, %.0f) %.0f\n", separationPlane.vNormal.x, separationPlane.vNormal.y, separationPlane.vNormal.z, separationPlane.fDist);

	// write separating plane

	BSPPLANE* newThisPlanes = new BSPPLANE[mapA.planeCount + 1];
	memcpy(newThisPlanes, mapA.planes, mapA.planeCount * sizeof(BSPPLANE));
	newThisPlanes[mapA.planeCount] = separationPlane;
	mapA.replace_lump(LUMP_PLANES, newThisPlanes, (mapA.planeCount + 1) * sizeof(BSPPLANE));

	int separationPlaneIdx = mapA.planeCount - 1;


	// write new head node (visible BSP)
	{
		BSPNODE headNode = {
			separationPlaneIdx,			// plane idx
			{mapA.nodeCount + 1, 1},		// child nodes
			{ bmin.x, bmin.y, bmin.z },	// mins
			{ bmax.x, bmax.y, bmax.z },	// maxs
			0, // first face
			0  // n faces (none since this plane is in the void)
		};

		if (swapNodeChildren) {
			int16_t temp = headNode.iChildren[0];
			headNode.iChildren[0] = headNode.iChildren[1];
			headNode.iChildren[1] = temp;
		}

		BSPNODE* newThisNodes = new BSPNODE[mapA.nodeCount + 1];
		memcpy(newThisNodes + 1, mapA.nodes, mapA.nodeCount * sizeof(BSPNODE));
		newThisNodes[0] = headNode;

		mapA.replace_lump(LUMP_NODES, newThisNodes, (mapA.nodeCount + 1) * sizeof(BSPNODE));
	}


	// write new head node (clipnode BSP)
	{
		const int NEW_NODE_COUNT = MAX_MAP_HULLS - 1;

		BSPCLIPNODE newHeadNodes[NEW_NODE_COUNT];
		for (int i = 0; i < NEW_NODE_COUNT; i++) {
			//logf("HULL %d starts at %d\n", i+1, thisWorld.iHeadnodes[i+1]);
			newHeadNodes[i] = {
				separationPlaneIdx,	// plane idx
				{	// child nodes
					(int16_t)(otherWorld.iHeadnodes[i + 1] + mapA.clipnodeCount + NEW_NODE_COUNT),
					(int16_t)(thisWorld.iHeadnodes[i + 1] + NEW_NODE_COUNT)
				},
			};

			if (otherWorld.iHeadnodes[i + 1] < 0) {
				newHeadNodes[i].iChildren[0] = CONTENTS_EMPTY;
			}
			if (thisWorld.iHeadnodes[i + 1] < 0) {
				newHeadNodes[i].iChildren[1] = CONTENTS_EMPTY;
			}


			if (swapNodeChildren) {
				int16_t temp = newHeadNodes[i].iChildren[0];
				newHeadNodes[i].iChildren[0] = newHeadNodes[i].iChildren[1];
				newHeadNodes[i].iChildren[1] = temp;
			}
		}

		BSPCLIPNODE* newThisNodes = new BSPCLIPNODE[mapA.clipnodeCount + NEW_NODE_COUNT];
		memcpy(newThisNodes, newHeadNodes, NEW_NODE_COUNT * sizeof(BSPCLIPNODE));
		memcpy(newThisNodes + NEW_NODE_COUNT, mapA.clipnodes, mapA.clipnodeCount * sizeof(BSPCLIPNODE));

		mapA.replace_lump(LUMP_CLIPNODES, newThisNodes, (mapA.clipnodeCount + NEW_NODE_COUNT) * sizeof(BSPCLIPNODE));
	}
}
