#include "Command.h"
#include "Renderer.h"
#include "Gui.h"
#include <lodepng.h>
#include "Bsp.h"
#include "Entity.h"
#include "util.h"
#include "globals.h"

#include "icons/aaatrigger.h"

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
	g_app->updateModelVerts();
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
	g_app->gui->refresh();
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
	g_app->gui->refresh();
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
	memset(&oldLumps, 0, sizeof(LumpState));
}

DuplicateBspModelCommand::~DuplicateBspModelCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
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
	g_app->gui->refresh();

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
	g_app->gui->refresh();

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

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}


//
// Create BSP model
//
CreateBspModelCommand::CreateBspModelCommand(string desc, int mapIdx, Entity* entData, float size) : Command(desc, mapIdx) {
	this->entData = new Entity();
	*this->entData = *entData;
	this->size = size;
	this->initialized = false;
	memset(&oldLumps, 0, sizeof(LumpState));
}

CreateBspModelCommand::~CreateBspModelCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
	if (entData != nullptr)
	{
		delete entData;
		entData = nullptr;
	}
}

void CreateBspModelCommand::execute() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	int aaatriggerIdx = getDefaultTextureIdx();

	if (!initialized) {
		int dupLumps = CLIPNODES | EDGES | FACES | NODES | PLANES | SURFEDGES | TEXINFO | VERTICES | LIGHTING | MODELS;
		if (aaatriggerIdx == -1) {
			dupLumps |= TEXTURES;
		}
		oldLumps = map->duplicate_lumps(dupLumps);
	}

	// add the aaatrigger texture if it doesn't already exist
	if (aaatriggerIdx == -1) {
		aaatriggerIdx = addDefaultTexture();
		renderer->reloadTextures();
	}

	vec3 mins = vec3(-size, -size, -size);
	vec3 maxs = vec3(size, size, size);
	int modelIdx = map->create_solid(mins, maxs, aaatriggerIdx);

	if (!initialized) {
		entData->addKeyvalue("model", "*" + to_string(modelIdx));
	}

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.push_back(newEnt);

	g_app->deselectObject();
	renderer->reload();
	g_app->gui->refresh();

	initialized = true;
}

void CreateBspModelCommand::undo() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	map->replace_lumps(oldLumps);

	delete map->ents[map->ents.size() - 1];
	map->ents.pop_back();

	renderer->reload();
	g_app->gui->refresh();
	g_app->deselectObject();
}

int CreateBspModelCommand::memoryUsage() {
	int size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}

int CreateBspModelCommand::getDefaultTextureIdx() {
	Bsp* map = getBsp();

	int32_t totalTextures = ((int32_t*)map->textures)[0];
	for (uint i = 0; i < totalTextures; i++) {
		int32_t texOffset = ((int32_t*)map->textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
		if (strcmp(tex.szName, "aaatrigger") == 0) {
			return i;
		}
	}

	return -1;
}

int CreateBspModelCommand::addDefaultTexture() {
	Bsp* map = getBsp();
	byte* tex_dat = NULL;
	uint w, h;

	lodepng_decode24(&tex_dat, &w, &h, aaatrigger_dat, sizeof(aaatrigger_dat));
	int aaatriggerIdx = map->add_texture("aaatrigger", tex_dat, w, h);
	//renderer->reloadTextures();

	lodepng_encode24_file("test.png", (byte*)tex_dat, w, h);
	delete[] tex_dat;

	return aaatriggerIdx;
}


//
// Edit BSP model
//
EditBspModelCommand::EditBspModelCommand(string desc, PickInfo& pickInfo, LumpState oldLumps, LumpState newLumps, 
		vec3 oldOrigin) : Command(desc, pickInfo.mapIdx) {
	this->modelIdx = pickInfo.modelIdx;
	this->entIdx = pickInfo.entIdx;
	this->oldLumps = oldLumps;
	this->newLumps = newLumps;
	this->allowedDuringLoad = false;
	this->oldOrigin = oldOrigin;
	this->newOrigin = pickInfo.ent->getOrigin();
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
	g_app->updateEntityState(ent);

	if (g_app->pickInfo.entIdx == entIdx) {
		g_app->updateModelVerts();
	}
}

int EditBspModelCommand::memoryUsage() {
	int size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i] + newLumps.lumpLen[i];
	}

	return size;
}



//
// Clean Map
//
CleanMapCommand::CleanMapCommand(string desc, int mapIdx, LumpState oldLumps) : Command(desc, mapIdx) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
}

CleanMapCommand::~CleanMapCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void CleanMapCommand::execute() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	logf("Cleaning %s\n", map->name.c_str());
	map->remove_unused_model_structures().print_delete_stats(1);

	refresh();
}

void CleanMapCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);

	refresh();
}

void CleanMapCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	g_app->saveLumpState(map, 0xffffffff, true);
}

int CleanMapCommand::memoryUsage() {
	int size = sizeof(CleanMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}



//
// Optimize Map
//
OptimizeMapCommand::OptimizeMapCommand(string desc, int mapIdx, LumpState oldLumps) : Command(desc, mapIdx) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
}

OptimizeMapCommand::~OptimizeMapCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void OptimizeMapCommand::execute() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	logf("Optimizing %s\n", map->name.c_str());
	if (!map->has_hull2_ents()) {
		logf("    Redirecting hull 2 to hull 1 because there are no large monsters/pushables\n");
		map->delete_hull(2, 1);
	}

	bool oldVerbose = g_verbose;
	g_verbose = true;
	map->delete_unused_hulls(true).print_delete_stats(1);
	g_verbose = oldVerbose;

	refresh();
}

void OptimizeMapCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);

	refresh();
}

void OptimizeMapCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	g_app->saveLumpState(map, 0xffffffff, true);
}

int OptimizeMapCommand::memoryUsage() {
	int size = sizeof(OptimizeMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}