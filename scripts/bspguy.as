
namespace bspguy {
	array<dictionary> g_ent_defs;
	bool noscript = false; // true if this script shouldn't be used but is loaded anyway
	
	dictionary no_delete_ents; // entity classes that don't work right if spawned late

	void print(string text) { g_Game.AlertMessage( at_console, text); }
	void println(string text) { print(text + "\n"); }
	
	void delay_respawn() {
		g_PlayerFuncs.RespawnAllPlayers(true, true);
	}
	
	void mapchange(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{
		string thisMap = getCustomStringKeyvalue(pCaller, "$s_bspguy_map_source");
		string nextMap = getCustomStringKeyvalue(pCaller, "$s_next_map");
		println("Transition from " + thisMap + " to " + nextMap);
		
		spawnMapEnts(nextMap);
		deleteMapEnts(thisMap, false, true); // delete spawns immediately
		g_Scheduler.SetTimeout("delay_respawn", 0.5f);
		g_Scheduler.SetTimeout("deleteMapEnts", 1.0f, thisMap, false, false); // delete everything else
	}
	
	void loadMapEnts() {
		string entFileName = string(g_Engine.mapname).ToLowercase() + ".ent";
		string fpath = "scripts/maps/bspguy/maps/" + entFileName;
		File@ f = g_FileSystem.OpenFile( fpath, OpenFile::READ );
		if( f is null or !f.IsOpen())
		{
			println("ERROR: bspguy ent file not found: " + fpath);
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
					println(entFileName + " (line " + lineNum + "): Unexpected '{'");
					continue;
				}
				lastBracket = 0;
				current_ent = dictionary();
			}
			else if (line[0] == '}')
			{
				if (lastBracket == 1)
					println(entFileName + " (line " + lineNum + "): Unexpected '}'");
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
		
		println("Loaded " + g_ent_defs.size() + " entity definitions from " + entFileName);
	}
	
	void deleteMapEnts(string mapName, bool invertFilter, bool spawnsOnly) {
	
		string infoEntName = "bspguy_info_" + mapName;
		CBaseEntity@ mapchangeEnt = g_EntityFuncs.FindEntityByTargetname(null, infoEntName);
		
		bool minMaxLoaded = false;
		Vector min, max;
		if (mapchangeEnt !is null) {
			min = getCustomVectorKeyvalue(mapchangeEnt, "$v_min");
			max = getCustomVectorKeyvalue(mapchangeEnt, "$v_max");
			minMaxLoaded = true;
		} else {
			println("ERROR: Missing entity '" + infoEntName + "'. Some entities may not be deleted in previous maps, and that can cause lag!");
		}
	
		CBaseEntity@ ent = null;
		do {
			@ent = g_EntityFuncs.FindEntityByClassname(ent, "*");
			if (ent !is null) {
				if (spawnsOnly && string(ent.pev.classname) != "info_player_deathmatch")
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
					string mapSource = mapKeyvalue.GetString();
					if (invertFilter && mapSource == mapName) {
						continue;
					} else if (!invertFilter && mapSource != mapName) {
						continue;
					}
				} else if (minMaxLoaded) {
					Vector ori = ent.pev.origin;
					// probably a entity that spawned from a squadmaker or something
					// skip if it's outside the map boundaries
					if (ori.x < min.x || ori.x > max.x || ori.y < min.y || ori.y > max.y || ori.z < min.z || ori.z > max.z) {
						continue;
					}
				}
				
				if (no_delete_ents.exists(ent.pev.classname)) {
					continue;
				}
				
				g_EntityFuncs.Remove(ent);
			}
		} while (ent !is null);
	}
	
	void spawnMapEnts(string mapName) {
		for (uint i = 0; i < g_ent_defs.size(); i++) {
			string mapSource;
			g_ent_defs[i].get("$s_bspguy_map_source", mapSource);
			
			if (mapSource == mapName) {
				string classname;
				g_ent_defs[i].get("classname", classname);
				
				if (no_delete_ents.exists(classname)) {
					continue;
				}
				
				g_EntityFuncs.CreateEntity(classname, g_ent_defs[i], true);
			}
		}
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
	
	void MapInit() {
		loadMapEnts();
		
		no_delete_ents["multi_manager"] = true; // never triggers anything if spawned late
	}
	
	void MapActivate() {		
		string firstMapName;
		CBaseEntity@ mapchangeEnt = g_EntityFuncs.FindEntityByTargetname(null, "bspguy_info");
		if (mapchangeEnt !is null) {
			firstMapName = getCustomStringKeyvalue(mapchangeEnt, "$s_first_map");
			noscript = getCustomStringKeyvalue(mapchangeEnt, "$s_noscript") == "yes";
		} else {
			println("ERROR: Missing entity 'bspguy_mapchage'. The BSP guy doesn't which map comes first!");
		}
		
		if (noscript) {
			println("WARNING: this map was not intended to be used with the bspguy script!");
			return;
		}
		
		dictionary keys;
		keys["targetname"] = "bspguy_mapchange";
		keys["delay"] = "0";
		keys["m_iszScriptFile"] = "bspguy/bspguy";
		keys["m_iszScriptFunctionName"] = "bspguy::mapchange";
		keys["m_iMode"] = "1"; // trigger
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		// all entities in all sections are spawned by now. Delete everything except for the ents in the first section.
		// It may be a bit slow to spawn all ents at first, but that will ensure everything is precached
		deleteMapEnts(firstMapName, true, false);
	}
}

