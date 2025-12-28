#include "Command.h"
#include "Renderer.h"
#include "Gui.h"
#include <lodepng.h>
#include "Bsp.h"
#include "Entity.h"
#include "util.h"
#include "globals.h"
#include <sstream>
#include <algorithm>

#include "icons/aaatrigger.h"

Command::Command(string desc) {
	this->desc = desc;
	debugf("New undo command added: %s\n", desc.c_str());
}

Bsp* Command::getBsp() {
	return g_app->mapRenderer->map;
}

BspRenderer* Command::getBspRenderer() {
	return g_app->mapRenderer;
}


//
// Edit entity
//
EditEntitiesCommand::EditEntitiesCommand(string desc, vector<EntityState>& oldEntData) : Command(desc) {
	for (int i = 0; i < oldEntData.size(); i++) {
		int idx = oldEntData[i].index;
		Entity* ent = g_app->pickInfo.getMap()->ents[idx];

		Entity* copy = new Entity();
		*copy = *ent;
		this->newEntData.push_back(copy);

		copy = new Entity();
		*copy = *oldEntData[i].ent;
		this->oldEntData.push_back(copy);
		
		entIndexes.push_back(idx);
	}

	this->allowedDuringLoad = true;
}

EditEntitiesCommand::~EditEntitiesCommand() {
	for (int i = 0; i < newEntData.size(); i++) {
		delete oldEntData[i];
		delete newEntData[i];
	}
	oldEntData.clear();
	newEntData.clear();
}

void EditEntitiesCommand::execute() {
	for (int i = 0; i < entIndexes.size(); i++) {
		Entity* target = getEntForIndex(entIndexes[i]);
		if (target) {
			*target = *newEntData[i];
		}
	}
	
	refresh();
}

void EditEntitiesCommand::undo() {
	for (int i = 0; i < entIndexes.size(); i++) {
		Entity* target = getEntForIndex(entIndexes[i]);
		if (target) {
			*target = *oldEntData[i];
		}
	}
	refresh();
}

Entity* EditEntitiesCommand::getEntForIndex(int idx) {
	Bsp* map = getBsp();

	if (!map || idx < 0 || idx >= map->ents.size()) {
		return NULL;
	}

	return map->ents[idx];
}

void EditEntitiesCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	
	g_app->updateEntConnections();
	g_app->updateEntityUndoState();
	g_app->pickCount++; // force GUI update
	g_app->mapRenderer->preRenderEnts(); // in case a point entity lost/gained a model
}

int EditEntitiesCommand::memoryUsage() {
	int sz = sizeof(EditEntitiesCommand) + entIndexes.size()*(sizeof(int) + sizeof(Entity*)*2);
	for (int i = 0; i < entIndexes.size(); i++) {
		sz += oldEntData[i]->getMemoryUsage();
		sz += newEntData[i]->getMemoryUsage();
	}
	return sz;
}


//
// Delete entity
//
DeleteEntitiesCommand::DeleteEntitiesCommand(string desc, vector<int> delEnts) : Command(desc) {
	// sort highest index to lowest, so they can be deleted in order without recalculating indexes
	std::sort(delEnts.begin(), delEnts.end(), [](const int& a, const int& b) {
		return a > b;
	});

	for (int entidx : delEnts) {
		entIndexes.push_back(entidx);
		Entity* copy = new Entity();
		*copy = *g_app->pickInfo.getMap()->ents[entidx];
		entData.push_back(copy);
	}
	
	this->allowedDuringLoad = true;
}

DeleteEntitiesCommand::~DeleteEntitiesCommand() {
	for (Entity* ent : entData) {
		delete ent;
	}
	entData.clear();
}

void DeleteEntitiesCommand::execute() {
	Bsp* map = getBsp();
	
	/*
	if (g_app->pickInfo.getEntIndex() == entIdx) {
		g_app->deselectObject();
	}
	else if (g_app->pickInfo.getEntIndex() > entIdx) {
		g_app->pickInfo.selectEnt(g_app->pickInfo.getEntIndex()-1);
	}
	*/
	g_app->deselectObject();

	for (int entidx : entIndexes) {
		delete map->ents[entidx];
		map->ents.erase(map->ents.begin() + entidx);
	}
	
	refresh();
}

void DeleteEntitiesCommand::undo() {
	Bsp* map = getBsp();
	
	/*
	if (g_app->pickInfo.getEntIndex() >= entIdx) {
		g_app->pickInfo.selectEnt(g_app->pickInfo.getEntIndex() + 1);
	}
	*/

	// create in reverse order (lowest index to highest)
	for (int i = entIndexes.size()-1; i >= 0; i--) {
		Entity* newEnt = new Entity();
		*newEnt = *entData[i];
		map->ents.insert(map->ents.begin() + entIndexes[i], newEnt);
	}
	
	refresh();
}

void DeleteEntitiesCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	renderer->preRenderEnts();
	g_app->gui->refresh();
	g_app->updateCullBox();
}

int DeleteEntitiesCommand::memoryUsage() {
	int sz = sizeof(DeleteEntitiesCommand);
	for (Entity* ent : entData) {
		sz += ent->getMemoryUsage();
	}
	return sz;
}


//
// Create Entity
//
CreateEntitiesCommand::CreateEntitiesCommand(string desc, vector<Entity*> pasteEnts) : Command(desc) {
	for (Entity* paste : pasteEnts) {
		Entity* copy = new Entity();
		*copy = *paste;
		entData.push_back(copy);
	}
	
	this->allowedDuringLoad = true;
}

CreateEntitiesCommand::~CreateEntitiesCommand() {
	for (Entity* ent : entData) {
		delete ent;
	}
	entData.clear();
}

void CreateEntitiesCommand::execute() {
	Bsp* map = getBsp();
	
	for (Entity* paste : entData) {
		Entity* newEnt = new Entity();
		*newEnt = *paste;
		map->ents.push_back(newEnt);
	}
	
	refresh();
}

void CreateEntitiesCommand::undo() {
	Bsp* map = getBsp();

	g_app->deselectObject();

	for (int i = 0; i < entData.size(); i++) {
		delete map->ents[map->ents.size() - 1];
		map->ents.pop_back();
	}
	
	refresh();
}

void CreateEntitiesCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	renderer->preRenderEnts();
	g_app->gui->refresh();
	g_app->updateCullBox();
}

int CreateEntitiesCommand::memoryUsage() {
	int sz = sizeof(CreateEntitiesCommand);
	for (Entity* ent : entData) {
		sz += ent->getMemoryUsage();
	}
	return sz;
}

//
// Create Entities From Text
//
CreateEntityFromTextCommand::CreateEntityFromTextCommand(string desc, string textData) : Command(desc) {
	this->textData = textData;
	this->allowedDuringLoad = true;
}

CreateEntityFromTextCommand::~CreateEntityFromTextCommand() {
}

vector<Entity*> CreateEntityFromTextCommand::parse() {
	std::istringstream in(textData);

	int lineNum = 0;
	int lastBracket = -1;
	Entity* ent = NULL;

	vector<Entity*> ents;

	string line = "";
	while (std::getline(in, line))
	{
		lineNum++;
		if (line.length() < 1 || line[0] == '\n')
			continue;

		if (line[0] == '{')
		{
			if (lastBracket == 0)
			{
				logf("clipboard ent text data (line %d): Unexpected '{'\n", lineNum);
				continue;
			}
			lastBracket = 0;

			if (ent != NULL)
				delete ent;
			ent = new Entity();
		}
		else if (line[0] == '}')
		{
			if (lastBracket == 1)
				logf("clipboard ent text data (line %d): Unexpected '}'\n", lineNum);
			lastBracket = 1;

			if (ent == NULL)
				continue;

			if (ent->hasKey("classname"))
				ents.push_back(ent);
			else
				logf("Found unknown classname entity. Skip it.\n");
			ent = NULL;

			// you can end/start an ent on the same line, you know
			if (line.find("{") != string::npos)
			{
				ent = new Entity();
				lastBracket = 0;
			}
		}
		else if (lastBracket == 0 && ent != NULL) // currently defining an entity
		{
			Keyvalue k(line);
			if (k.key.length() && k.value.length())
				ent->setOrAddKeyvalue(k.key, k.value);
		}
	}

	return ents;
}

void CreateEntityFromTextCommand::execute() {
	Bsp* map = getBsp();
	vector<Entity*> ents = parse();

	for (Entity* ent : ents) {
		map->ents.push_back(ent);
	}
	createdEnts = ents.size();
	
	refresh();
}

void CreateEntityFromTextCommand::undo() {
	Bsp* map = getBsp();

	g_app->deselectObject();

	for (int i = 0; i < createdEnts; i++) {
		delete map->ents[map->ents.size() - 1];
		map->ents.pop_back();
	}
	
	refresh();
}

void CreateEntityFromTextCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	renderer->preRenderEnts();
	g_app->gui->refresh();
	g_app->updateCullBox();
}

int CreateEntityFromTextCommand::memoryUsage() {
	return sizeof(CreateEntityFromTextCommand) + textData.size();
}


//
// Edit BSP model
//
EditBspModelCommand::EditBspModelCommand(string desc, PickInfo& pickInfo, LumpState oldLumps, LumpState newLumps, 
		vec3 oldOrigin) : Command(desc) {
	this->modelIdx = pickInfo.getModelIndex();
	this->entIdx = pickInfo.getEntIndex();
	this->oldLumps = oldLumps;
	this->newLumps = newLumps;
	this->allowedDuringLoad = false;
	this->oldOrigin = oldOrigin;
	this->newOrigin = pickInfo.getOrigin();
}

EditBspModelCommand::~EditBspModelCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
		if (newLumps.lumps[i])
			delete[] newLumps.lumps[i];
	}
}

void EditBspModelCommand::execute() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	map->replace_lumps(newLumps);
	map->ents[entIdx]->setOrAddKeyvalue("origin", newOrigin.toKeyvalueString());
	g_app->undoEntOrigin = newOrigin;

	refresh();
}

void EditBspModelCommand::undo() {
	Bsp* map = getBsp();
	
	map->replace_lumps(oldLumps);
	map->ents[entIdx]->setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
	g_app->undoEntOrigin = oldOrigin;

	refresh();
}

void EditBspModelCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();
	Entity* ent = map->ents[entIdx];

	renderer->updateLightmapInfos();
	renderer->calcFaceMaths();
	renderer->refreshModel(modelIdx);
	renderer->refreshEnt(entIdx);
	g_app->gui->refresh();
	g_app->saveLumpState(map, 0xffffff, true);
	g_app->updateEntityUndoState();
	g_app->gui->lightmapEditorNeedsUpdate = true;

	if (g_app->pickInfo.getEntIndex() == entIdx) {
		g_app->updateModelVerts();
	}
}

int EditBspModelCommand::memoryUsage() {
	int size = sizeof(EditBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i] + newLumps.lumpLen[i];
	}

	return size;
}


//
// A command that makes heavy modifations to the BSP and will need a full reload
//
LumpReplaceCommand::LumpReplaceCommand(string desc, bool norefresh) : Command(desc) {
	this->allowedDuringLoad = false;
	this->norefresh = norefresh;

	Bsp* map = g_app->mapRenderer->map;
	g_app->saveLumpState(map, 0xffffffff, true);
	this->oldLumps = g_app->undoLumpState;
	g_app->saveLumpState(map, 0xffffffff, false);
}

LumpReplaceCommand::~LumpReplaceCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
		if (newLumps.lumps[i])
			delete[] newLumps.lumps[i];
	}
}

void LumpReplaceCommand::execute() {
	Bsp* map = getBsp();
	map->replace_lumps(newLumps);
	refresh();
}

void LumpReplaceCommand::pushUndoState(bool norefresh) {
	Bsp* map = g_app->mapRenderer->map;

	this->newLumps = map->duplicate_lumps(0xffffffff);
	memset(differences, 0, sizeof(bool) * HEADER_LUMPS);

	bool anyDifference = false;
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (newLumps.lumps[i] && oldLumps.lumps[i]) {
			if (newLumps.lumpLen[i] != oldLumps.lumpLen[i] || memcmp(newLumps.lumps[i], oldLumps.lumps[i], newLumps.lumpLen[i]) != 0) {
				anyDifference = true;
				differences[i] = true;
			}
		}
	}

	// delete lumps that have no differences to save space
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (!differences[i]) {
			delete[] oldLumps.lumps[i];
			delete[] newLumps.lumps[i];
			oldLumps.lumps[i] = newLumps.lumps[i] = NULL;
			oldLumps.lumpLen[i] = newLumps.lumpLen[i] = 0;
		}
	}

	if (!norefresh)
		refresh();

	g_app->pushUndoCommand(this);
}

void LumpReplaceCommand::undo() {
	Bsp* map = getBsp();
	map->replace_lumps(oldLumps);
	refresh();
}

void LumpReplaceCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	g_app->gui->refresh();

	if (norefresh) {
		return;
	}

	renderer->reload();
	g_app->deselectObject();
	g_app->saveLumpState(map, 0xffffffff, true);
}

int LumpReplaceCommand::memoryUsage() {
	int size = sizeof(LumpReplaceCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
		size += newLumps.lumpLen[i];
	}

	return size;
}




ModelEditCommand::ModelEditCommand(string desc, vector<int> modelIndexes) : LumpReplaceCommand(desc) {
	this->modelIndexes = modelIndexes;
}

void ModelEditCommand::refresh() {
	for (int idx : modelIndexes) {
		g_app->mapRenderer->refreshModel(idx);
		g_app->mapRenderer->refreshModelClipnodes(idx);
	}
	g_app->gui->refresh();
}

int ModelEditCommand::memoryUsage() {
	return LumpReplaceCommand::memoryUsage() + sizeof(ModelEditCommand) - sizeof(LumpReplaceCommand);
}


FacesEditCommand::FacesEditCommand(string desc) : LumpReplaceCommand(desc) {
	for (int idx : g_app->pickInfo.getModelIndexes()) {
		this->modelRefreshes.insert(idx);
	}
	this->faces = g_app->pickInfo.faces;
	textureDataReloadNeeded = false;
}


void FacesEditCommand::refresh() {
	if (textureDataReloadNeeded) {
		g_app->mapRenderer->reloadTextures(true);
	}

	g_app->mapRenderer->highlightPickedFaces(false);
	g_app->pickInfo.deselect();

	for (int idx : modelRefreshes)
		g_app->mapRenderer->refreshModel(idx, false);

	if (g_app->pickMode == PICK_FACE) {
		for (int i = 0; i < faces.size(); i++) {
			g_app->pickInfo.selectFace(faces[i]);
		}
		g_app->mapRenderer->highlightPickedFaces(true);
	}

	g_app->pickCount++;
	g_app->updateTextureAxes();
	g_app->gui->refresh();
}

int FacesEditCommand::memoryUsage() {
	return LumpReplaceCommand::memoryUsage() + sizeof(FacesEditCommand) - sizeof(LumpReplaceCommand)
		+ modelRefreshes.size()*sizeof(int);
}



LightmapsEditCommand::LightmapsEditCommand(string desc) : LumpReplaceCommand(desc) {}

void LightmapsEditCommand::refresh() {
	g_app->mapRenderer->reloadLightmaps();
	g_app->gui->refresh();
}

int LightmapsEditCommand::memoryUsage() {
	return LumpReplaceCommand::memoryUsage() + sizeof(LightmapsEditCommand) - sizeof(LumpReplaceCommand);
}