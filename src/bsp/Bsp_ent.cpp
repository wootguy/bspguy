#include "Bsp.h"
#include "util.h"
#include "Entity.h"
#include <sstream>

bool Bsp::has_hull2_ents() {
	// monsters that use hull 2 by default
	static set<string> largeMonsters{
		"monster_alien_grunt",
		"monster_alien_tor",
		"monster_alien_voltigore",
		"monster_babygarg",
		"monster_bigmomma",
		"monster_bullchicken",
		"monster_gargantua",
		"monster_ichthyosaur",
		"monster_kingpin",
		"monster_apache",
		"monster_blkop_apache"
		// osprey, nihilanth, and tentacle are huge but are basically nonsolid (no brush collision or triggers)
	};

	for (int i = 0; i < ents.size(); i++) {
		string cname = ents[i]->getClassname();
		string tname = ents[i]->getTargetname();

		if (cname.find("monster_") == 0) {
			vec3 minhull;
			vec3 maxhull;

			if (!ents[i]->getKeyvalue("minhullsize").empty())
				minhull = Keyvalue("", ents[i]->getKeyvalue("minhullsize")).getVector();
			if (!ents[i]->getKeyvalue("maxhullsize").empty())
				maxhull = Keyvalue("", ents[i]->getKeyvalue("maxhullsize")).getVector();

			if (minhull == vec3(0, 0, 0) && maxhull == vec3(0, 0, 0)) {
				// monster is using its default hull size
				if (largeMonsters.find(cname) != largeMonsters.end()) {
					return true;
				}
			}
			else if (abs(minhull.x) > MAX_HULL1_EXTENT_MONSTER || abs(maxhull.x) > MAX_HULL1_EXTENT_MONSTER
				|| abs(minhull.y) > MAX_HULL1_EXTENT_MONSTER || abs(maxhull.y) > MAX_HULL1_EXTENT_MONSTER) {
				return true;
			}
		}
		else if (cname == "func_pushable") {
			int modelIdx = ents[i]->getBspModelIdx();
			if (modelIdx < modelCount) {
				BSPMODEL& model = models[modelIdx];
				vec3 size = model.nMaxs - model.nMins;

				if (size.x > MAX_HULL1_SIZE_PUSHABLE || size.y > MAX_HULL1_SIZE_PUSHABLE) {
					return true;
				}
			}
		}
	}

	return false;
}

int Bsp::get_entity_index(Entity* ent) {
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i] == ent) {
			return i;
		}
	}

	return -1;
}

int Bsp::zero_entity_origins(string classname) {
	int moveCount = 0;

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname() == classname) {
			vec3 ori = ents[i]->getOrigin();
			if (ori.x || ori.y || ori.z) {
				int modelIdx = ents[i]->getBspModelIdx();
				if (modelIdx > 0) {
					if (does_model_use_shared_structures(modelIdx)) {
						modelIdx = duplicate_model(modelIdx);
						ents[i]->setOrAddKeyvalue("model", "*" + to_string(modelIdx));
					}

					g_progress.hide = true;
					move(ori, modelIdx);
					g_progress.hide = false;

					ents[i]->setOrAddKeyvalue("origin", "0 0 0");
					moveCount++;
				}
			}
		}
	}

	if (moveCount)
		logf("Zeroed %d %s origins\n", moveCount, classname.c_str());

	return moveCount;
}

int Bsp::zero_sensitive_entity_origins() {
	static const char* sensitive_ents[] = {
		"func_ladder",			// players crash or get launched at lightning speed in HL
		"func_water",			// water is sometimes invisible after moving in sven
		"func_mortar_field",	// mortars don't appear in sven
		"func_friction",		// flickers if given a water texture (TODO: this probably applies to any visible entity with a water texture)
		"env_bubbles",			// was told it was needed but I didn't ask why
	};

	int moveCount = 0;
	for (int i = 0; i < sizeof(sensitive_ents) / sizeof(sensitive_ents[0]); i++) {
		moveCount += zero_entity_origins(sensitive_ents[i]);
	}

	return moveCount;
}

bool Bsp::is_invisible_solid(Entity* ent) {
	if (!ent->isBspModel())
		return false;

	string tname = ent->getTargetname();
	int rendermode = atoi(ent->getKeyvalue("rendermode").c_str());
	int renderamt = atoi(ent->getKeyvalue("renderamt").c_str());
	int renderfx = atoi(ent->getKeyvalue("renderfx").c_str());

	if (rendermode == 0 || renderamt != 0) {
		return false;
	}
	switch (renderfx) {
	case 1: case 2: case 3: case 4: case 7:
	case 8: case 15: case 16: case 17:
		return false;
	default:
		break;
	}

	static set<string> renderKeys{
		"rendermode",
		"renderamt",
		"renderfx"
	};

	for (int i = 0; i < ents.size(); i++) {
		string cname = ents[i]->getClassname();

		if (cname == "env_render") {
			return false; // assume it will affect the brush since it can be moved anywhere
		}
		else if (cname == "env_render_individual") {
			if (ents[i]->getKeyvalue("target") == tname) {
				return false; // assume it's making the ent visible
			}
		}
		else if (cname == "trigger_changevalue") {
			if (ents[i]->getKeyvalue("target") == tname) {
				if (renderKeys.find(ents[i]->getKeyvalue("m_iszValueName")) != renderKeys.end()) {
					return false; // assume it's making the ent visible
				}
			}
		}
		else if (cname == "trigger_copyvalue") {
			if (ents[i]->getKeyvalue("target") == tname) {
				if (renderKeys.find(ents[i]->getKeyvalue("m_iszDstValueName")) != renderKeys.end()) {
					return false; // assume it's making the ent visible
				}
			}
		}
		else if (cname == "trigger_createentity") {
			if (ents[i]->getKeyvalue("+model") == tname || ents[i]->getKeyvalue("-model") == ent->getKeyvalue("model")) {
				return false; // assume this new ent will be visible at some point
			}
		}
		else if (cname == "trigger_changemodel") {
			if (ents[i]->getKeyvalue("model") == ent->getKeyvalue("model")) {
				return false; // assume the target is visible
			}
		}
	}

	return true;
}

void Bsp::update_ent_lump(bool stripNodes) {
	int len;
	byte* dat = create_ent_lump(ents, len, stripNodes);
	replace_lump(LUMP_ENTITIES, dat, len);
}

byte* Bsp::create_ent_lump(vector<Entity*>& entList, int& len, bool stripNodes) {
	stringstream ent_data;

	for (int i = 0; i < entList.size(); i++) {
		if (stripNodes) {
			string cname = entList[i]->getClassname();
			if (cname == "info_node" || cname == "info_node_air") {
				continue;
			}
		}

		ent_data << "{\n";

		ent_data << entList[i]->getFullKvString();

		ent_data << "}";
		if (i < entList.size() - 1) {
			ent_data << "\n"; // trailing newline crashes sven, and only sven, and only sometimes
		}
	}

	string str_data = ent_data.str();

	byte* newEntData = new byte[str_data.size() + 1];
	memcpy(newEntData, str_data.c_str(), str_data.size());
	newEntData[str_data.size()] = 0; // null terminator required too(?)

	len = str_data.size() + 1;
	return newEntData;
}

void Bsp::load_ents(byte* lump, int lumpLen, vector<Entity*>& entList)
{
	for (int i = 0; i < entList.size(); i++)
		delete entList[i];
	entList.clear();

	bool verbose = true;
	membuf sbuf((char*)lump, lumpLen);
	istream in(&sbuf);

	Entity* ent = NULL;

	string current, key, value;
	bool inString = false;
	bool readingKey = true;

	char c;
	while (in.get(c)) {
		if (inString) {
			if (c == '"') {
				inString = false;

				if (readingKey) {
					key = current;
					readingKey = false;
				}
				else {
					value = current;

					if (ent && !key.empty())
						ent->setOrAddKeyvalue(key, value);

					key.clear();
					value.clear();
					readingKey = true;
				}

				current.clear();
			}
			else {
				current += c; // keep everything, including newlines
			}
			continue;
		}

		// not inside string
		if (c == '"') {
			inString = true;
			current.clear();
		}
		else if (c == '{') {
			if (ent)
				delete ent;

			ent = new Entity();
			readingKey = true;
			key.clear();
			value.clear();
		}
		else if (c == '}') {
			if (!ent)
				continue;

			if (ent->hasKey("classname"))
				entList.push_back(ent);
			else
				logf("Removed entity with no classname.\n");

			ent = NULL;
		}
		else {
			// ignore everything else outside strings (whitespace, newlines, etc.)
		}
	}

	if (entList.size() > 1)
	{
		if (ents[0]->getClassname() != "worldspawn")
		{
			for (int i = 1; i < entList.size(); i++)
			{
				if (entList[i]->getClassname() == "worldspawn")
				{
					warnf("'%s' and 'woldspawn' entities were swapped so that 'worldspawn' comes first "
						"in the entity list.\n", entList[0]->getClassname().c_str());
					std::swap(entList[0], entList[i]);
					break;
				}
			}
		}
	}

	if (ent != NULL)
		delete ent;
}

string Bsp::get_model_usage(int modelIdx) {
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getBspModelIdx() == modelIdx) {
			return "\"" + ents[i]->getTargetname() + "\" (" + ents[i]->getClassname() + ")";
		}
	}
	return "(unused)";
}

vector<Entity*> Bsp::get_model_ents(int modelIdx) {
	vector<Entity*> uses;
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getBspModelIdx() == modelIdx) {
			uses.push_back(ents[i]);
		}
	}
	return uses;
}

bool Bsp::do_entities_share_models() {
	unordered_set<int> uniqueModels;

	for (Entity* ent : ents) {
		int modelIdx = ent->getBspModelIdx();
		if (modelIdx > 0) {
			if (uniqueModels.count(modelIdx)) {
				return true;
			}
			uniqueModels.insert(modelIdx);
		}
	}

	return false;
}

STRUCTCOUNT Bsp::delete_unused_hulls(bool noProgress) {
	if (!noProgress) {
		if (g_verbose)
			g_progress.update("", 0);
		else
			g_progress.update("Deleting unused hulls", modelCount - 1);
	}

	int deletedHulls = 0;

	for (int i = 1; i < modelCount; i++) {
		if (!g_verbose && !noProgress)
			g_progress.tick();

		vector<Entity*> usageEnts = get_model_ents(i);

		if (usageEnts.size() == 0) {
			debugf("Deleting unused model %d\n", i);

			for (int k = 0; k < MAX_MAP_HULLS; k++)
				deletedHulls += models[i].iHeadnodes[k] >= 0;

			delete_model(i);
			//modelCount--; automatically updated when lump is replaced
			i--;
			continue;
		}

		set<string> conditionalPointEntTriggers;
		conditionalPointEntTriggers.insert("trigger_once");
		conditionalPointEntTriggers.insert("trigger_multiple");
		conditionalPointEntTriggers.insert("trigger_counter");
		conditionalPointEntTriggers.insert("trigger_gravity");
		conditionalPointEntTriggers.insert("trigger_teleport");

		set<string> entsThatNeverNeedAnyHulls;
		entsThatNeverNeedAnyHulls.insert("env_bubbles");
		entsThatNeverNeedAnyHulls.insert("func_tankcontrols");
		entsThatNeverNeedAnyHulls.insert("func_traincontrols");
		entsThatNeverNeedAnyHulls.insert("func_vehiclecontrols");
		//entsThatNeverNeedAnyHulls.insert("trigger_autosave"); // obsolete in sven
		//entsThatNeverNeedAnyHulls.insert("trigger_endsection"); // obsolete in sven

		set<string> entsThatNeverNeedCollision;
		entsThatNeverNeedCollision.insert("func_illusionary");
		entsThatNeverNeedCollision.insert("func_mortar_field");

		set<string> passableEnts;
		passableEnts.insert("func_door");
		passableEnts.insert("func_door_rotating");
		passableEnts.insert("func_pendulum");
		passableEnts.insert("func_tracktrain");
		passableEnts.insert("func_train");
		passableEnts.insert("func_water");
		passableEnts.insert("momentary_door");

		set<string> playerOnlyTriggers;
		playerOnlyTriggers.insert("func_ladder");
		playerOnlyTriggers.insert("game_zone_player");
		playerOnlyTriggers.insert("player_respawn_zone");
		playerOnlyTriggers.insert("trigger_cdaudio");
		playerOnlyTriggers.insert("trigger_changelevel");
		playerOnlyTriggers.insert("trigger_transition");

		set<string> monsterOnlyTriggers;
		monsterOnlyTriggers.insert("func_monsterclip");
		monsterOnlyTriggers.insert("trigger_monsterjump");

		string uses = "";
		bool needsPlayerHulls = false; // HULL 1 + HULL 3
		bool needsMonsterHulls = false; // All HULLs
		bool needsVisibleHull = false; // HULL 0
		for (int k = 0; k < usageEnts.size(); k++) {
			string cname = usageEnts[k]->getClassname();
			string tname = usageEnts[k]->getTargetname();
			int spawnflags = atoi(usageEnts[k]->getKeyvalue("spawnflags").c_str());

			if (k != 0) {
				uses += ", ";
			}
			uses += "\"" + tname + "\" (" + cname + ")";

			if (entsThatNeverNeedAnyHulls.find(cname) != entsThatNeverNeedAnyHulls.end()) {
				continue; // no collision or faces needed at all
			}
			else if (entsThatNeverNeedCollision.find(cname) != entsThatNeverNeedCollision.end()) {
				needsVisibleHull = !is_invisible_solid(usageEnts[k]);
			}
			else if (passableEnts.find(cname) != passableEnts.end()) {
				bool notPassable = !(spawnflags & 8); // "Passable" or "Not solid" unchecked
				needsPlayerHulls |= notPassable;
				needsMonsterHulls |= notPassable;
				needsVisibleHull |= notPassable || !is_invisible_solid(usageEnts[k]);
			}
			else if (cname.find("trigger_") == 0) {
				if (conditionalPointEntTriggers.find(cname) != conditionalPointEntTriggers.end()) {
					needsVisibleHull |= !!(spawnflags & 8); // "Everything else" flag checked
					needsPlayerHulls |= !(spawnflags & 2); // "No clients" unchecked
					needsMonsterHulls |= (spawnflags & 1) || (spawnflags & 4); // "monsters" or "pushables" checked
				}
				else if (cname == "trigger_push") {
					needsPlayerHulls |= !(spawnflags & 8); // "No clients" unchecked
					needsMonsterHulls |= (spawnflags & 4) || !(spawnflags & 16); // "Pushables" checked or "No monsters" unchecked
					needsVisibleHull |= true; // needed for point-ent pushing
				}
				else if (cname == "trigger_hurt") {
					needsPlayerHulls |= !(spawnflags & 8); // "No clients" unchecked
					needsMonsterHulls |= !(spawnflags & 16) || !(spawnflags & 32); // "Fire/Touch client only" unchecked
				}
				else {
					needsPlayerHulls |= true;
					needsMonsterHulls |= true;
				}
			}
			else if (cname == "func_clip") {
				needsPlayerHulls |= !(spawnflags & 8); // "No clients" not checked
				needsMonsterHulls |= (spawnflags & 8) || !(spawnflags & 16); // "Pushables" checked or "No monsters" unchecked
				needsVisibleHull |= (spawnflags & 32) || (spawnflags & 64); // "Everything else" or "item_inv" checked
			}
			else if (cname == "func_conveyor") {
				bool isSolid = !(spawnflags & 2); // "Not Solid" unchecked
				needsPlayerHulls |= isSolid;
				needsMonsterHulls |= isSolid;
				needsVisibleHull |= isSolid || !is_invisible_solid(usageEnts[k]);
			}
			else if (cname == "func_friction") {
				needsPlayerHulls |= true;
				needsMonsterHulls |= true;
			}
			else if (cname == "func_rot_button") {
				bool isSolid = !(spawnflags & 1); // "Not Solid" unchecked
				needsPlayerHulls |= isSolid;
				needsMonsterHulls |= isSolid;
				needsVisibleHull |= true;
			}
			else if (cname == "func_rotating") {
				bool isSolid = !(spawnflags & 64); // "Not Solid" unchecked
				needsPlayerHulls |= isSolid;
				needsMonsterHulls |= isSolid;
				needsVisibleHull |= true;
			}
			else if (cname == "func_ladder") {
				needsPlayerHulls |= true;
				needsVisibleHull |= true;
			}
			else if (playerOnlyTriggers.find(cname) != playerOnlyTriggers.end()) {
				needsPlayerHulls |= true;
			}
			else if (monsterOnlyTriggers.find(cname) != monsterOnlyTriggers.end()) {
				needsMonsterHulls |= true;
			}
			else {
				// assume all hulls are needed
				needsPlayerHulls |= true;
				needsMonsterHulls |= true;
				needsVisibleHull |= true;
				break;
			}
		}

		BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[i];

		if (!needsVisibleHull && !needsMonsterHulls) {
			if (models[i].iHeadnodes[0] >= 0)
				debugf("Deleting HULL 0 from model %d, used in %s\n", i, uses.c_str());

			deletedHulls += models[i].iHeadnodes[0] >= 0;

			model.iHeadnodes[0] = -1;
			model.nVisLeafs = 0;
			model.nFaces = 0;
			model.iFirstFace = 0;
		}
		if (!needsPlayerHulls && !needsMonsterHulls) {
			bool deletedAnyHulls = false;
			for (int k = 1; k < MAX_MAP_HULLS; k++) {
				deletedHulls += models[i].iHeadnodes[k] >= 0;
				if (models[i].iHeadnodes[k] >= 0) {
					deletedHulls++;
					deletedAnyHulls = true;
				}
			}

			if (deletedAnyHulls)
				debugf("Deleting HULL 1-3 from model %d, used in %s\n", i, uses.c_str());

			model.iHeadnodes[1] = -1;
			model.iHeadnodes[2] = -1;
			model.iHeadnodes[3] = -1;
		}
		else if (!needsMonsterHulls) {
			if (models[i].iHeadnodes[2] >= 0)
				debugf("Deleting HULL 2 from model %d, used in %s\n", i, uses.c_str());

			deletedHulls += models[i].iHeadnodes[2] >= 0;

			model.iHeadnodes[2] = -1;
		}
		else if (!needsPlayerHulls) {
			// monsters use all hulls so can't do anything about this
		}
	}

	STRUCTCOUNT removed = remove_unused_model_structures();

	update_ent_lump();

	if (!g_verbose && !noProgress) {
		g_progress.clear();
	}

	return removed;
}
