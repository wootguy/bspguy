#include "Command.h"
#include "Renderer.h"
#include "Gui.h"

Command::Command(string desc, int mapIdx) {
	this->desc = desc;
	this->mapIdx = mapIdx;
	debugf("New undo command added: %s\n", desc.c_str());
}

Bsp* Command::getBsp() {
	if (mapIdx < 0 || mapIdx >= g_app->mapRenderers.size()) {
		return NULL;
	}

	return g_app->mapRenderers[mapIdx]->map;
}

BspRenderer* Command::getBspRenderer() {
	if (mapIdx < 0 || mapIdx >= g_app->mapRenderers.size()) {
		return NULL;
	}

	return g_app->mapRenderers[mapIdx];
}


//
// Edit entity
//
EditEntityCommand::EditEntityCommand(string desc, PickInfo& pickInfo, Entity* oldEntData, Entity* newEntData) 
		: Command(desc, pickInfo.mapIdx) {
	this->entIdx = pickInfo.entIdx;
	this->oldEntData = new Entity();
	this->newEntData = new Entity();
	*this->oldEntData = *oldEntData;
	*this->newEntData = *newEntData;
	this->allowedDuringLoad = true;
}

EditEntityCommand::~EditEntityCommand() {
	if (oldEntData)
		delete oldEntData;
	if (newEntData)
		delete newEntData;
}

void EditEntityCommand::execute() {
	Entity* target = getEnt();
	*target = *newEntData;
	refresh();
}

void EditEntityCommand::undo() {
	Entity* target = getEnt();
	*target = *oldEntData;
	refresh();
}

Entity* EditEntityCommand::getEnt() {
	Bsp* map = getBsp();

	if (!map || entIdx < 0 || entIdx >= map->ents.size()) {
		return NULL;
	}

	return map->ents[entIdx];
}

void EditEntityCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	Entity* ent = getEnt();
	renderer->refreshEnt(entIdx);
	if (!ent->isBspModel()) {
		renderer->refreshPointEnt(entIdx);
	}
	g_app->updateEntityState(ent);
	g_app->pickCount++; // force GUI update
	true;
}

int EditEntityCommand::memoryUsage() {
	return sizeof(EditEntityCommand) + oldEntData->getMemoryUsage() + newEntData->getMemoryUsage();
}


//
// Delete entity
//
DeleteEntityCommand::DeleteEntityCommand(string desc, PickInfo& pickInfo)
		: Command(desc, pickInfo.mapIdx) {
	this->entIdx = pickInfo.entIdx;
	this->entData = new Entity();
	*this->entData = *pickInfo.ent;
	this->allowedDuringLoad = true;
}

DeleteEntityCommand::~DeleteEntityCommand() {
	if (entData)
		delete entData;
}

void DeleteEntityCommand::execute() {
	Bsp* map = getBsp();

	if (g_app->pickInfo.entIdx == entIdx) {
		g_app->deselectObject();
	}
	else if (g_app->pickInfo.entIdx > entIdx) {
		g_app->pickInfo.entIdx -= 1;
	}

	delete map->ents[entIdx];
	map->ents.erase(map->ents.begin() + entIdx);

	refresh();
}

void DeleteEntityCommand::undo() {
	Bsp* map = getBsp();

	if (g_app->pickInfo.entIdx >= entIdx) {
		g_app->pickInfo.entIdx += 1;
	}

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.insert(map->ents.begin() + entIdx, newEnt);

	refresh();
}

void DeleteEntityCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	renderer->preRenderEnts();
	g_app->gui->reloadLimits();
}

int DeleteEntityCommand::memoryUsage() {
	return sizeof(DeleteEntityCommand) + entData->getMemoryUsage();
}


//
// Create Entity
//
CreateEntityCommand::CreateEntityCommand(string desc, int mapIdx, Entity* entData) : Command(desc, mapIdx) {
	this->entData = new Entity();
	*this->entData = *entData;
	this->allowedDuringLoad = true;
}

CreateEntityCommand::~CreateEntityCommand() {
	if (entData) {
		delete entData;
	}
}

void CreateEntityCommand::execute() {
	Bsp* map = getBsp();
	
	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.push_back(newEnt);

	refresh();
}

void CreateEntityCommand::undo() {
	Bsp* map = getBsp();

	if (g_app->pickInfo.entIdx == map->ents.size() - 1) {
		g_app->deselectObject();
	}
	delete map->ents[map->ents.size() - 1];
	map->ents.pop_back();

	refresh();
}

void CreateEntityCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	renderer->preRenderEnts();
	g_app->gui->reloadLimits();
}

int CreateEntityCommand::memoryUsage() {
	return sizeof(CreateEntityCommand) + entData->getMemoryUsage();
}


//
// Duplicate BSP Model command
//
DuplicateBspModelCommand::DuplicateBspModelCommand(string desc, PickInfo& pickInfo) 
		: Command(desc, pickInfo.mapIdx) {
	this->oldModelIdx = pickInfo.modelIdx;
	this->newModelIdx = -1;
	this->entIdx = pickInfo.entIdx;
	this->initialized = false;
	this->allowedDuringLoad = false;
}

DuplicateBspModelCommand::~DuplicateBspModelCommand() {
	if (initialized) {
		for (int i = 0; i < HEADER_LUMPS; i++) {
			delete[] oldLumps.lumps[i];
		}
	}
}

void DuplicateBspModelCommand::execute() {
	Bsp* map = getBsp();
	Entity* ent = map->ents[entIdx];
	BspRenderer* renderer = getBspRenderer();

	if (!initialized) {
		int dupLumps = CLIPNODES | EDGES | FACES | NODES | PLANES | SURFEDGES | TEXINFO | VERTICES | LIGHTING | MODELS;
		oldLumps = map->duplicate_lumps(dupLumps);
		initialized = true;
	}

	newModelIdx = map->duplicate_model(oldModelIdx);
	ent->setOrAddKeyvalue("model", "*" + to_string(newModelIdx));	

	renderer->updateLightmapInfos();
	renderer->calcFaceMaths();
	renderer->preRenderFaces();
	renderer->preRenderEnts();
	renderer->reloadLightmaps();
	renderer->addClipnodeModel(newModelIdx);
	g_app->gui->reloadLimits();

	g_app->deselectObject();
	/*
	if (g_app->pickInfo.entIdx == entIdx) {
		g_app->pickInfo.modelIdx = newModelIdx;
		g_app->updateModelVerts();
	}
	*/
}

void DuplicateBspModelCommand::undo() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	Entity* ent = map->ents[entIdx];
	map->replace_lumps(oldLumps);
	ent->setOrAddKeyvalue("model", "*" + to_string(oldModelIdx));

	if (g_app->pickInfo.modelIdx == newModelIdx) {
		g_app->pickInfo.modelIdx = oldModelIdx;
		
	}
	else if (g_app->pickInfo.modelIdx > newModelIdx) {
		g_app->pickInfo.modelIdx -= 1;
	}

	renderer->reload();
	g_app->gui->reloadLimits();

	g_app->deselectObject();
	/*
	if (g_app->pickInfo.entIdx == entIdx) {
		g_app->pickInfo.modelIdx = oldModelIdx;
		g_app->updateModelVerts();
	}
	*/
}

int DuplicateBspModelCommand::memoryUsage() {
	int size = sizeof(DuplicateBspModelCommand);

	if (initialized) {
		for (int i = 0; i < HEADER_LUMPS; i++) {
			size += oldLumps.lumpLen[i];
		}
	}

	return size;
}
