#include "bspguy_equip"

namespace bspguy {
	array<dictionary> g_ent_defs;
	bool noscript = false; // true if this script shouldn't be used but is loaded anyway
	int survival_is_restarting = 0;
	
	dictionary no_delete_ents; // entity classes that don't work right if spawned late
	dictionary map_loaded;
	dictionary map_cleaned;
	array<string> map_order;
	int current_map_idx;

	void print(string text) { g_Game.AlertMessage( at_console, text); }
	void println(string text) { print(text + "\n"); }
	
	void delay_respawn() {
		println("[bspguy] Respawning everyone");
		g_PlayerFuncs.RespawnAllPlayers(true, true);
		survival_is_restarting = 0;
	}
	
	void delay_trigger(EHandle h_ent) {
		CBaseEntity@ ent = h_ent;
		
		if (ent is null)
			return;

		ent.Use(null, null, USE_TOGGLE);
	}
	
	void delay_fire_targets(string target) {
		println("[bspguy] Triggering: " + target);
		g_EntityFuncs.FireTargets(target, null, null, USE_TOGGLE);
	}
	
	void resetMapCleanStates() {
		println("[bspguy] Reset map clean states");
		map_cleaned.clear();
	}
	
	void mapchange_internal(string thisMap, string nextMap, bool fastchange=false) {
		for (uint i = 0; i < map_order.size(); i++) {
			if (map_order[i] == nextMap) {
				current_map_idx = i;
				break;
			}
		}
		
		g_PlayerFuncs.ClientPrintAll(HUD_PRINTCENTER, "Entering " + nextMap + "\n");
		
		float extraDelay = fastchange ? 0.0f : 1.5f;
		
		if (thisMap != nextMap) {
			array<string>@ levelKeys = map_loaded.getKeys();
			for (uint i = 0; i < levelKeys.length(); i++)
			{
				if (map_cleaned.exists(levelKeys[i])) {
					println("[bspguy] Map " + levelKeys[i] + " has already been cleaned. Skipping mapchange clean.");
					continue;
				}
		
				deleteMapEnts(levelKeys[i], false, true); // delete spawns immediately, in all previous levels
				g_Scheduler.SetTimeout("clean_map_no_repeat", 1.0f + extraDelay, levelKeys[i]); // everything else
			}
		
			spawnMapEnts(nextMap);
			g_Scheduler.SetTimeout("delay_fire_targets", 0.5f + extraDelay, "bspguy_start_" + nextMap);
			g_Scheduler.SetTimeout("delay_respawn", 0.5f + extraDelay);
			g_Scheduler.SetTimeout("resetMapCleanStates", 1.1f + extraDelay);
			
		} else {
			// restarting the same level
			deleteMapEnts(thisMap, false, false);
			spawnMapEnts(nextMap);
			g_Scheduler.SetTimeout("delay_respawn", 0.5f + extraDelay);
		}
	}

	void load_map_no_repeat(string map) {
		if (map_loaded.exists(map)) {
			println("[bspguy] Map " + map + " has already loaded. Ignoring mapload trigger.");
			return;
		}
		
		g_Scheduler.SetTimeout("delay_fire_targets", 0.0f, "bspguy_start_" + map);
		
		spawnMapEnts(map);
		
		for (uint i = 0; i < map_order.size(); i++) {
			if (map_order[i] == map) {
				current_map_idx = i;
				break;
			}
		}
	}
	
	void clean_map_no_repeat(string map) {
		if (map_cleaned.exists(map)) {
			println("[bspguy] Map " + map + " has already been cleaned.");
			return;
		}
		
		deleteMapEnts(map, false, false); // delete everything
	}
	
	void mapchange(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{
		string thisMap = getCustomStringKeyvalue(pCaller, "$s_bspguy_map_source").ToLowercase();
		string nextMap = getCustomStringKeyvalue(pCaller, "$s_next_map").ToLowercase();
		
		if (thisMap == "" or nextMap == "") {
			println("[bspguy] ERROR: bspguy_mapchange called by " + pCaller.pev.classname + " which is missing $s_bspguy_map_source or $s_next_map");
			return;
		}
		
		if (map_cleaned.exists(thisMap)) {
			println("[bspguy] Map " + thisMap + " has already been cleaned. Ignoring mapchange trigger.");
			return;
		}
		if (map_loaded.exists(nextMap)) {
			println("[bspguy] Map " + nextMap + " has already loaded. Ignoring mapchange trigger.");
			return;
		}
		
		mapchange_internal(thisMap, nextMap);
	}
	
	void mapload(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{
		string nextMap = getCustomStringKeyvalue(pCaller, "$s_next_map").ToLowercase();		
		load_map_no_repeat(nextMap);
	}
	
	void mapclean(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{
		string cleanMap = getCustomStringKeyvalue(pCaller, "$s_bspguy_map_source").ToLowercase();
		clean_map_no_repeat(cleanMap);
	}
	
	void maprestart(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{
		g_Scheduler.SetTimeout("restart_survival_section", 0, true);
	}
	
	void bspguy(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{		
		string loadMap = getCustomStringKeyvalue(pCaller, "$s_bspguy_load").ToLowercase();
		string cleanMap = getCustomStringKeyvalue(pCaller, "$s_bspguy_clean").ToLowercase();
		string rotate = getCustomStringKeyvalue(pCaller, "$s_bspguy_rotate");
		string trigger = getCustomStringKeyvalue(pCaller, "$s_bspguy_trigger");
		
		if (loadMap.Length() > 0) {
			load_map_no_repeat(loadMap);
		}
		
		if (cleanMap.Length() > 0) {
			array<string> maps = cleanMap.Split("+");
			for (uint i = 0; i < maps.size(); i++) {
				clean_map_no_repeat(maps[i]);
			}
		}
		
		if (rotate.Length() > 0 && pActivator !is null && pCaller !is null) {
			float rot = atof(rotate);
			
			Vector delta = pActivator.pev.origin - pCaller.pev.origin;
			array<float> yawRotMat = rotationMatrix(Vector(0,0,-1), rot);
			pActivator.pev.velocity = matMultVector(yawRotMat, pActivator.pev.velocity);
			pActivator.pev.origin = pCaller.pev.origin + matMultVector(yawRotMat, delta);
			
			if (pActivator.IsPlayer()) {
				pActivator.pev.angles = pActivator.pev.v_angle;
				pActivator.pev.angles.y += rot;
				pActivator.pev.fixangle = 1;
			} else {
				pActivator.pev.angles.y += rot;
			}
		}
		
		if (trigger.Length() > 0) {
			int triggerTypeSep = trigger.Find("#");
			bool killed = false;
			
			if (triggerTypeSep != -1) {
				int triggerType = atoi(trigger.SubString(triggerTypeSep+1));
				trigger = trigger.SubString(0, triggerTypeSep);
				
				if (triggerType == 0) {
					useType = USE_OFF;
				} else if (triggerType == 1) {
					useType = USE_ON;
				} else if (triggerType == 2) {
					killed = true;
					
					CBaseEntity@ ent = null;
					do {
						@ent = g_EntityFuncs.FindEntityByTargetname(ent, trigger);
						if (ent !is null) {
							g_EntityFuncs.Remove(ent);
						}
					} while (ent !is null);
				}
			}
			
			if (!killed)
				g_EntityFuncs.FireTargets(trigger, pActivator, pCaller, useType);
		}
	}
	
	array<float> rotationMatrix(Vector axis, float angle)
	{
		angle = angle * Math.PI / 180.0; // convert to radians
		axis = axis.Normalize();
		float s = sin(angle);
		float c = cos(angle);
		float oc = 1.0 - c;
	 
		array<float> mat = {
			oc * axis.x * axis.x + c,          oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s, 0.0,
			oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c,          oc * axis.y * axis.z - axis.x * s, 0.0,
			oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c,			 0.0,
			0.0,                               0.0,                               0.0,								 1.0
		};
		return mat;
	}
	
	// multiply a matrix with a vector (assumes w component of vector is 1.0f) 
	Vector matMultVector(array<float> rotMat, Vector v)
	{
		Vector outv;
		outv.x = rotMat[0]*v.x + rotMat[4]*v.y + rotMat[8]*v.z  + rotMat[12];
		outv.y = rotMat[1]*v.x + rotMat[5]*v.y + rotMat[9]*v.z  + rotMat[13];
		outv.z = rotMat[2]*v.x + rotMat[6]*v.y + rotMat[10]*v.z + rotMat[14];
		return outv;
	}
	
	void loadMapEnts() {
		string entFileName = string(g_Engine.mapname).ToLowercase() + ".ent";
		string fpath = "scripts/maps/bspguy/maps/" + entFileName;
		File@ f = g_FileSystem.OpenFile( fpath, OpenFile::READ );
		if( f is null or !f.IsOpen())
		{
			println("[bspguy] ERROR: bspguy ent file not found: " + fpath);
			return;
		}

		int lineNum = 0;
		int lastBracket = -1;
		string line;
		
		dictionary current_ent;
		while( !f.EOFReached() )
		{
			f.ReadLine(line);
			
			lineNum++;
			if (line.Length() < 1 || line[0] == '\n')
				continue;

			if (line[0] == '{')
			{
				if (lastBracket == 0)
				{
					println("[bspguy] " + entFileName + " (line " + lineNum + "): Unexpected '{'");
					continue;
				}
				lastBracket = 0;
				current_ent = dictionary();
			}
			else if (line[0] == '}')
			{
				if (lastBracket == 1)
					println("[bspguy] " + entFileName + " (line " + lineNum + "): Unexpected '}'");
				lastBracket = 1;

				if (current_ent.isEmpty())
					continue;

				g_ent_defs.push_back(current_ent);
				current_ent = dictionary();
			}
			else if (lastBracket == 0) // currently defining an entity
			{
				// parse keyvalue
				int begin = -1;
				int end = -1;

				string key = "";
				string value = "";
				int comment = 0;

				for (uint i = 0; i < line.Length(); i++)
				{
					if (line[i] == '/')
					{
						if (++comment >= 2)
						{
							key = value = "";
							break;
						}
					}
					else
						comment = 0;
					if (line[i] == '"')
					{
						if (begin == -1)
							begin = i + 1;
						else
						{
							end = i;
							if (key.Length() == 0)
							{
								key = line.SubString(begin,end-begin);
								begin = end = -1;
							}
							else
							{
								value = line.SubString(begin,end-begin);
								break;
							}
						}
					}
				}
				
				if (key.Length() > 0) {
					current_ent[key] = value;
				}
			}
		}
		
		println("[bspguy] Loaded " + g_ent_defs.size() + " entity definitions from " + entFileName);
	}
	
	bool isSpawnEntity(string cname) {
		return cname == "info_player_deathmatch" or cname == "info_player_start"
				or cname == "info_player_dm2" or cname == "info_player_coop";
	}
	
	void deleteMapEnts(string mapName, bool invertFilter, bool spawnsOnly) {
		mapName = mapName.ToLowercase();
		string infoEntName = "bspguy_info_" + mapName;
		CBaseEntity@ mapchangeEnt = g_EntityFuncs.FindEntityByTargetname(null, infoEntName);
		
		bool minMaxLoaded = false;
		Vector min, max;
		if (mapchangeEnt !is null) {
			min = getCustomVectorKeyvalue(mapchangeEnt, "$v_min");
			max = getCustomVectorKeyvalue(mapchangeEnt, "$v_max");
			minMaxLoaded = true;
		} else {
			println("[bspguy] ERROR: Missing entity '" + infoEntName + "'. Some entities may not be deleted in previous maps, and that can cause lag!");
		}
	
		CBaseEntity@ ent = null;
		do {
			@ent = g_EntityFuncs.FindEntityByClassname(ent, "*");
			if (ent !is null) {
				if (spawnsOnly && !isSpawnEntity(ent.pev.classname))
					continue;
			
				if (ent.IsPlayer() or string(ent.pev.targetname).Find("bspguy") == 0) {
					continue;
				}
				
				// don't remove player items/weapons
				CBasePlayerItem@ item = cast<CBasePlayerItem@>(ent);
				if (item !is null && item.m_hPlayer.IsValid()) {
					continue;
				}
				
				KeyValueBuffer@ pKeyvalues = g_EngineFuncs.GetInfoKeyBuffer( ent.edict() );
				CustomKeyvalues@ pCustom = ent.GetCustomKeyvalues();
				CustomKeyvalue mapKeyvalue( pCustom.GetKeyvalue( "$s_bspguy_map_source" ) );
				if (mapKeyvalue.Exists()) {
					string mapSource = mapKeyvalue.GetString().ToLowercase();
					if (invertFilter && mapSource == mapName) {
						continue;
					} else if (!invertFilter && mapSource != mapName) {
						continue;
					}
				} else if (minMaxLoaded) {
					Vector ori = ent.pev.origin;
					// probably a entity that spawned from a squadmaker or something
					// skip if it's outside the map boundaries
					bool outOfBounds = ori.x < min.x || ori.x > max.x || ori.y < min.y || ori.y > max.y || ori.z < min.z || ori.z > max.z;
					if ((!invertFilter && outOfBounds) || (invertFilter && !outOfBounds)) {
						continue;
					}
				}
				
				if (no_delete_ents.exists(ent.pev.classname)) {
					continue;
				}
				
				g_EntityFuncs.Remove(ent);
			}
		} while (ent !is null);
		
		if (!spawnsOnly) {
			map_loaded.delete(mapName);
			map_cleaned[mapName] = true;
			println("[bspguy] Cleaned section " + mapName);
		} else {
			println("[bspguy] Disabled spawns in section " + mapName);
		}
	}
	
	void spawnMapEnts(string mapName) {
		mapName = mapName.ToLowercase();
	
		for (uint i = 0; i < g_ent_defs.size(); i++) {
			string mapSource;
			g_ent_defs[i].get("$s_bspguy_map_source", mapSource);
			
			if (mapSource == mapName) {
				string classname;
				g_ent_defs[i].get("classname", classname);
				
				if (no_delete_ents.exists(classname) || classname == "info_node" || classname == "info_node_air") {
					continue;
				}
				
				CBaseEntity@ ent = g_EntityFuncs.CreateEntity(classname, g_ent_defs[i], true);
				
				if (ent !is null && string(ent.pev.classname) == "func_train") {
					if (hasCustomKeyvalue(ent, "$i_bspguy_trainfix")) {
						// For some reason survivor3 train at spawn only needs 2 triggers to respond properly.
						// Might as well just allow a custom trigger count. It's getting too complicated
						// to handle all scenarios.
						
						int triggerCount = getCustomIntegerKeyvalue(ent, "$i_bspguy_trainfix");
						for (int t = 0; t < triggerCount; t++) {
							g_Scheduler.SetTimeout("delay_trigger", 0.0f, EHandle(ent));
						}
						println("[bspguy] Triggered " + ent.pev.targetname + " (func_train) " + triggerCount + " times ($i_bspguy_trainfix)");
					}
					else {
						// default trigger logic to fix trains that are spawned late
						
						if (string(ent.pev.targetname).Length() > 0) {
							// triggering is broken the first time when spawned late
							// sometimes needs 2+ triggers, but in those cases the trainfix kevalue should be used
							delay_trigger(EHandle(ent));
						} else {
							// unnamed trains are supposed to start active, but don't when spawned late.
							// It needs to be triggered on separate server frames for it to start moving
							delay_trigger(EHandle(ent));
							g_Scheduler.SetTimeout("delay_trigger", 0.0f, EHandle(ent));
						}
					}
				}
			}
		}
		
		map_loaded[mapName] = true;
		map_cleaned.delete(mapName);
		println("[bspguy] Loaded section " + mapName);
		println("[bspguy] Triggering: " + "bspguy_init_" + mapName);
		g_EntityFuncs.FireTargets("bspguy_init_" + mapName, null, null, USE_TOGGLE);
	}
	
	CustomKeyvalue getCustomKeyvalue(CBaseEntity@ ent, string keyName) {
		KeyValueBuffer@ pKeyvalues = g_EngineFuncs.GetInfoKeyBuffer( ent.edict() );
		CustomKeyvalues@ pCustom = ent.GetCustomKeyvalues();
		return CustomKeyvalue( pCustom.GetKeyvalue( keyName ) );
	}
	
	string getCustomStringKeyvalue(CBaseEntity@ ent, string keyName) {
		CustomKeyvalue keyvalue = getCustomKeyvalue(ent, keyName);
		if (keyvalue.Exists()) {
			return keyvalue.GetString();
		}
		return "";
	}
	
	Vector getCustomVectorKeyvalue(CBaseEntity@ ent, string keyName) {
		CustomKeyvalue keyvalue = getCustomKeyvalue(ent, keyName);
		if (keyvalue.Exists()) {
			return keyvalue.GetVector();
		}
		return Vector(0,0,0);
	}
	
	int getCustomIntegerKeyvalue(CBaseEntity@ ent, string keyName) {
		CustomKeyvalue keyvalue = getCustomKeyvalue(ent, keyName);
		if (keyvalue.Exists()) {
			return keyvalue.GetInteger();
		}
		return 0;
	}
	
	float getCustomFloatKeyvalue(CBaseEntity@ ent, string keyName) {
		CustomKeyvalue keyvalue = getCustomKeyvalue(ent, keyName);
		if (keyvalue.Exists()) {
			return keyvalue.GetFloat();
		}
		return 0;
	}
	
	bool hasCustomKeyvalue(CBaseEntity@ ent, string keyName) {
		CustomKeyvalue keyvalue = getCustomKeyvalue(ent, keyName);
		return keyvalue.Exists();
	}
	
	void MapInit() {
		loadMapEnts();
		
		g_CustomEntityFuncs.RegisterCustomEntity( "bspguy::bspguy_equip", "bspguy_equip" );
		
		no_delete_ents["multi_manager"] = true; // never triggers anything if spawned late
		no_delete_ents["path_track"] = true; // messes up track_train if spawned late
		no_delete_ents["soundcache"] = true; // plugin entity
	}
	
	// restart the current section, rather than the entire map
	void survival_restart_check() {
		if (!g_SurvivalMode.IsActive() || count_living_players() > 0) {
			survival_is_restarting = 0;
			return;
		}
		
		survival_is_restarting += 1;
		if (survival_is_restarting < 5 || survival_is_restarting > 5) {
			// Wait a few seconds to be sure a restart is coming.
			// The ForceSurvival plugin might respawn everyone instead.
			return;
		}
		
		g_PlayerFuncs.ClientPrintAll(HUD_PRINTTALK, "This is a merged map. The current section will reload shortly.\n");	
		g_Scheduler.SetTimeout("restart_survival_section", 3, false);
	}
	
	int count_living_players() {
		int totalLiving = 0;
		
		CBaseEntity@ ent = null;
		do {
			@ent = g_EntityFuncs.FindEntityByClassname(ent, "player");
			if (ent !is null && ent.IsAlive()) {
				totalLiving += 1;
			}
		} while (ent !is null);	
		
		return totalLiving;
	}
	
	void restart_survival_section(bool force_restart) {		
		if (count_living_players() > 0 && !force_restart) {
			return;
		}
		
		string thisMap = map_order[current_map_idx];
		string loadMap = "";
		
		for (uint i = 0; i < map_order.size(); i++) {			
			if (map_loaded.exists(map_order[i])) {
				if (loadMap.Length() == 0) {
					loadMap = map_order[i];
				}
				clean_map_no_repeat(map_order[i]);
			}
		}
		
		mapchange_internal(loadMap, loadMap, true);
	}
	
	void MapActivate() {	
		string firstMapName;
		CBaseEntity@ infoEnt = g_EntityFuncs.FindEntityByTargetname(null, "bspguy_info");
		if (infoEnt !is null) {
			firstMapName = getCustomStringKeyvalue(infoEnt, "$s_map0");
			current_map_idx = 0;
			
			for (int i = 0; i < 64; i++) {
				string mapName = getCustomStringKeyvalue(infoEnt, "$s_map" + i).ToLowercase();
				if (mapName.Length() > 0)
					map_order.insertLast(mapName);
				else
					break;
			}
			
			noscript = getCustomStringKeyvalue(infoEnt, "$s_noscript") == "yes";
		} else {
			println("[bspguy] ERROR: Missing entity 'bspguy_info'. bspguy script disabled!");
			return;
		}
		
		if (noscript) {
			println("[bspguy] WARNING: this map was not intended to be used with the bspguy script!");
			return;
		}
		
		if (firstMapName.Length() == 0) {
			println("[bspguy] ERROR: bspguy_info entity has no $s_mapX keys. bspguy script disabled!");
			return;
		}
		
		dictionary keys;
		keys["delay"] = "0";
		keys["m_iszScriptFile"] = "bspguy/bspguy";
		keys["m_iMode"] = "1"; // trigger
		
		keys["targetname"] = "bspguy_maprestart";
		keys["m_iszScriptFunctionName"] = "bspguy::maprestart";
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		keys["targetname"] = "bspguy_mapchange";
		keys["m_iszScriptFunctionName"] = "bspguy::mapchange";
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		keys["targetname"] = "bspguy_mapload";
		keys["m_iszScriptFunctionName"] = "bspguy::mapload";
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		keys["targetname"] = "bspguy_mapclean";
		keys["m_iszScriptFunctionName"] = "bspguy::mapclean";
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		keys["targetname"] = "bspguy";
		keys["m_iszScriptFunctionName"] = "bspguy::bspguy";
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		// all entities in all sections are spawned by now. Delete everything except for the ents in the first section.
		// It may be a bit slow to spawn all ents at first, but that will ensure everything is precached
		deleteMapEnts(firstMapName, true, false);
		map_loaded[firstMapName] = true;
		map_cleaned.clear();
		
		g_Scheduler.SetTimeout("delay_fire_targets", 0.0f, "bspguy_start_" + firstMapName);
		g_Scheduler.SetInterval("survival_restart_check", 1.0f, -1);
	}

	void printMapSections(EHandle h_plr) {
		CBasePlayer@ plr = cast<CBasePlayer@>(h_plr.GetEntity());
	
		g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, "Map sections:\n");	
		for (uint i = 0; i < map_order.size(); i++) {
			string begin = i < 9 ? "     " : "    ";
			
			bool isLoaded = map_loaded.exists(map_order[i]);
			bool isCleaned = map_cleaned.exists(map_order[i]);
			
			string end = "\n";
			
			if (i == uint(current_map_idx)) {
				end = "    (LOADED + CURRENT)\n";
			}
			else if (isLoaded) {
				end = "    (LOADED)\n";
			} else if (isCleaned) {
				end = "    (CLEANED)\n";
			}
			
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, begin + (i+1) + ") " +  map_order[i] + end);
		}
	}

	void doCommand(CBasePlayer@ plr, const CCommand@ args, bool inConsole) {
		bool isAdmin = g_PlayerFuncs.AdminLevel(plr) >= ADMIN_YES;
		
		if (args.ArgC() >= 2)
		{
			if (args[1] == "version") {
				g_PlayerFuncs.SayText(plr, "bspguy script v2\n");
			}
			if (args[1] == "list") {
				printMapSections(EHandle(plr));
			}
			if (args[1] == "spawn") {
				g_Scheduler.SetInterval("delay_respawn", 0.1, 25);
			}
			if (args[1] == "mapchange") {
				if (!isAdmin) {
					g_PlayerFuncs.SayText(plr, "Only admins can use that command.\n");
					return;
				}
				if (args.ArgC() >= 3) {
					string arg = args[2];
					string thisMap = map_order[current_map_idx];
					string nextMap;
					for (uint i = 0; i < map_order.size(); i++) {
						if (arg.ToLowercase() == map_order[i].ToLowercase()) {
							nextMap = arg;
							break;
						}
					}
					if (nextMap.Length() == 0) {
						uint idx = atoi(arg) - 1;
						if (idx < map_order.size()) {
							nextMap = map_order[idx];
						}
					}
					if (nextMap.Length() == 0) {
						g_PlayerFuncs.SayText(plr, "Invalid section name/number. See \"bspguy list\" output.\n");
					} else {
						mapchange_internal(thisMap, nextMap, true);
						g_Scheduler.SetTimeout("printMapSections", 1.2f, EHandle(plr));
					}
				} else {
					if (current_map_idx >= int(map_order.size())-1) {
						g_PlayerFuncs.SayText(plr, "This is the last map section.\n");
					} else {
						string thisMap = map_order[current_map_idx];
						string nextMap = map_order[current_map_idx+1];
						
						mapchange_internal(thisMap, nextMap, true);
						g_Scheduler.SetTimeout("printMapSections", 1.2f, EHandle(plr));
					}
				}
			}
		} else {			
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, '----------------------------------bspguy commands----------------------------------\n\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, 'Type "bspguy list" to list map sections.\n\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, 'Type "bspguy spawn" to test spawn points.\n\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, 'Type "bspguy mapchange [name|number]" to transition to a new map section.\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, '    [name|number] = Optional. Map section name or number to load (as shown in "bspguy list")\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, '\n-----------------------------------------------------------------------------------\n\n');
		}
	}

	CClientCommand _bspguy("bspguy", "bspguy commands", @bspguy::consoleCmd );

	void consoleCmd( const CCommand@ args ) {
		CBasePlayer@ plr = g_ConCommandSystem.GetCurrentPlayer();
		doCommand(plr, args, true);
	}
}

