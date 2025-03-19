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

			if (ent->keyvalues.count("classname"))
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
				ent->addKeyvalue(k);
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
	if (createdEnts)
		logf("Pasted %d entities from clipboard\n", createdEnts);
	
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
// Duplicate BSP Model command
//
DuplicateBspModelCommand::DuplicateBspModelCommand(string desc, PickInfo& pickInfo) 
		: Command(desc) {
	this->oldModelIdx = pickInfo.getModelIndex();
	this->newModelIdx = -1;
	this->entIdx = pickInfo.getEntIndex();
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
	
	g_app->pickInfo.deselect();
	/*
	if (g_app->pickInfo.getModelIndex() == newModelIdx) {
		//g_app->pickInfo.modelIdx = oldModelIdx;
		
	}
	else if (g_app->pickInfo.getModelIndex() > newModelIdx) {
		//g_app->pickInfo.modelIdx -= 1;
	}
	*/

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
CreateBspModelCommand::CreateBspModelCommand(string desc, Entity* entData, float size) : Command(desc) {
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

	if (g_app->pickInfo.getEntIndex() == entIdx) {
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
CleanMapCommand::CleanMapCommand(string desc, LumpState oldLumps) : Command(desc) {
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
OptimizeMapCommand::OptimizeMapCommand(string desc, LumpState oldLumps) : Command(desc) {
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



//
// Delete boxed data
//
DeleteBoxedDataCommand::DeleteBoxedDataCommand(string desc, vec3 mins, vec3 maxs, LumpState oldLumps) : Command(desc) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
	this->mins = mins;
	this->maxs = maxs;
}

DeleteBoxedDataCommand::~DeleteBoxedDataCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void DeleteBoxedDataCommand::execute() {
	Bsp* map = getBsp();

	map->delete_box_data(mins, maxs);

	refresh();
}

void DeleteBoxedDataCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);
	map->load_ents();

	refresh();
}

void DeleteBoxedDataCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	g_app->saveLumpState(map, 0xffffffff, true);
}

int DeleteBoxedDataCommand::memoryUsage() {
	int size = sizeof(DeleteBoxedDataCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}



//
// Delete OOB data
//
DeleteOobDataCommand::DeleteOobDataCommand(string desc, int clipFlags, LumpState oldLumps) : Command(desc) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
	this->clipFlags = clipFlags;
}

DeleteOobDataCommand::~DeleteOobDataCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void DeleteOobDataCommand::execute() {
	Bsp* map = getBsp();

	map->delete_oob_data(clipFlags);

	refresh();
}

void DeleteOobDataCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);
	map->load_ents();

	refresh();
}

void DeleteOobDataCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	g_app->saveLumpState(map, 0xffffffff, true);
}

int DeleteOobDataCommand::memoryUsage() {
	int size = sizeof(DeleteOobDataCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}


//
// Fix bad surface extents
//
FixSurfaceExtentsCommand::FixSurfaceExtentsCommand(string desc, bool scaleNotSubdivide,
		bool downscaleOnly, int maxTextureDim, LumpState oldLumps) : Command(desc) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
	this->scaleNotSubdivide = scaleNotSubdivide;
	this->downscaleOnly = downscaleOnly;
	this->maxTextureDim = maxTextureDim;
}

FixSurfaceExtentsCommand::~FixSurfaceExtentsCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void FixSurfaceExtentsCommand::execute() {
	Bsp* map = getBsp();

	map->fix_bad_surface_extents(scaleNotSubdivide, downscaleOnly, maxTextureDim);

	refresh();
}

void FixSurfaceExtentsCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);

	refresh();
}

void FixSurfaceExtentsCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	g_app->saveLumpState(map, 0xffffffff, true);
}

int FixSurfaceExtentsCommand::memoryUsage() {
	int size = sizeof(FixSurfaceExtentsCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}



//
// Deduplicate models
//
DeduplicateModelsCommand::DeduplicateModelsCommand(string desc, LumpState oldLumps) : Command(desc) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
}

DeduplicateModelsCommand::~DeduplicateModelsCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void DeduplicateModelsCommand::execute() {
	Bsp* map = getBsp();

	map->deduplicate_models();

	refresh();
}

void DeduplicateModelsCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);
	map->load_ents();

	refresh();
}

void DeduplicateModelsCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	g_app->saveLumpState(map, 0xffffffff, true);
}

int DeduplicateModelsCommand::memoryUsage() {
	int size = sizeof(DeduplicateModelsCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}


//
// Move the entire map
//
MoveMapCommand::MoveMapCommand(string desc, vec3 offset, LumpState oldLumps) : Command(desc) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
	this->offset = offset;
}

MoveMapCommand::~MoveMapCommand() {
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void MoveMapCommand::execute() {
	Bsp* map = getBsp();

	map->ents[0]->removeKeyvalue("origin");
	map->move(offset);

	map->zero_entity_origins("func_ladder");
	map->zero_entity_origins("func_water"); // water is sometimes invisible after moving in sven
	map->zero_entity_origins("func_mortar_field"); // mortars don't appear in sven

	refresh();
}

void MoveMapCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);
	map->ents[0]->setOrAddKeyvalue("origin", offset.toKeyvalueString());

	refresh();
}

void MoveMapCommand::refresh() {
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	g_app->saveLumpState(map, 0xffffffff, true);
}

int MoveMapCommand::memoryUsage() {
	int size = sizeof(MoveMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumpLen[i];
	}

	return size;
}