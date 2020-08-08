#include "Command.h"
#include "Renderer.h"
#include "Gui.h"

Command::Command(string desc, int mapIdx) {
	this->desc = desc;
	this->mapIdx = mapIdx;
	//logf("New undo command added: %s\n", desc.c_str());
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
}

EditEntityCommand::~EditEntityCommand() {
	if (oldEntData)
		delete oldEntData;
	if (newEntData)
		delete newEntData;
}

bool EditEntityCommand::execute() {
	Entity* target = getEnt();
	*target = *newEntData;
	return refresh();
}

bool EditEntityCommand::undo() {
	Entity* target = getEnt();
	*target = *oldEntData;
	return refresh();
}

Entity* EditEntityCommand::getEnt() {
	Bsp* map = getBsp();

	if (!map || entIdx < 0 || entIdx >= map->ents.size()) {
		return NULL;
	}

	return map->ents[entIdx];
}

bool EditEntityCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	Entity* ent = getEnt();
	renderer->refreshEnt(entIdx);
	if (!ent->isBspModel()) {
		renderer->refreshPointEnt(entIdx);
	}
	g_app->updateEntityState(ent);
	g_app->pickCount++; // force GUI update
	return true;
}


//
// Delete entity
//
DeleteEntityCommand::DeleteEntityCommand(string desc, PickInfo& pickInfo)
		: Command(desc, pickInfo.mapIdx) {
	this->entIdx = pickInfo.entIdx;
	this->entData = new Entity();
	*this->entData = *pickInfo.ent;
}

DeleteEntityCommand::~DeleteEntityCommand() {
	if (entData)
		delete entData;
}

bool DeleteEntityCommand::execute() {
	Bsp* map = getBsp();

	if (g_app->pickInfo.entIdx == entIdx) {
		g_app->deselectObject();
	}
	else if (g_app->pickInfo.entIdx > entIdx) {
		g_app->pickInfo.entIdx -= 1;
	}

	delete map->ents[entIdx];
	map->ents.erase(map->ents.begin() + entIdx);

	return refresh();
}

bool DeleteEntityCommand::undo() {
	Bsp* map = getBsp();

	if (g_app->pickInfo.entIdx >= entIdx) {
		g_app->pickInfo.entIdx += 1;
	}

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.insert(map->ents.begin() + entIdx, newEnt);

	return refresh();
}

bool DeleteEntityCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	renderer->preRenderEnts();
	g_app->gui->reloadLimits();
	return true;
}


//
// Create Entity
//
CreateEntityCommand::CreateEntityCommand(string desc, int mapIdx, Entity* entData) : Command(desc, mapIdx) {
	this->entData = new Entity();
	*this->entData = *entData;
}

CreateEntityCommand::~CreateEntityCommand() {
	if (entData) {
		delete entData;
	}
}

bool CreateEntityCommand::execute() {
	Bsp* map = getBsp();
	
	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.push_back(newEnt);

	return refresh();
}

bool CreateEntityCommand::undo() {
	Bsp* map = getBsp();

	if (g_app->pickInfo.entIdx == map->ents.size() - 1) {
		g_app->deselectObject();
	}
	delete map->ents[map->ents.size() - 1];
	map->ents.pop_back();

	return refresh();
}

bool CreateEntityCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	renderer->preRenderEnts();
	g_app->gui->reloadLimits();
	return true;
}
