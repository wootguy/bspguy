#pragma once

#include "util.h"
#include "Bsp.h"
#include "Entity.h"
#include "BspRenderer.h"

// Undoable actions following the Command Pattern

class Command {
public:
	string desc;
	int mapIdx;
	bool allowedDuringLoad = false;

	Command(string desc, int mapIdx);
	virtual void execute() = 0;
	virtual void undo() = 0;
	virtual int memoryUsage() = 0;
	
	BspRenderer* getBspRenderer();
	Bsp* getBsp();
};


class EditEntityCommand : public Command {
public:
	int entIdx;
	Entity* oldEntData;
	Entity* newEntData;

	EditEntityCommand(string desc, PickInfo& pickInfo, Entity* oldEntData, Entity* newEntData);
	~EditEntityCommand();

	void execute();
	void undo();
	Entity* getEnt();
	void refresh();
	int memoryUsage();
};


class DeleteEntityCommand : public Command {
public:
	int entIdx;
	Entity* entData;

	DeleteEntityCommand(string desc, PickInfo& pickInfo);
	~DeleteEntityCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class CreateEntityCommand : public Command {
public:
	Entity* entData;

	CreateEntityCommand(string desc, int mapIdx, Entity* entData);
	~CreateEntityCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class DuplicateBspModelCommand : public Command {
public:
	int oldModelIdx;
	int newModelIdx; // TODO: could break redos if this is ever not deterministic
	int entIdx;
	LumpState oldLumps;
	bool initialized = false;

	DuplicateBspModelCommand(string desc, PickInfo& pickInfo);
	~DuplicateBspModelCommand();

	void execute();
	void undo();
	int memoryUsage();
};


class CreateBspModelCommand : public Command {
public:
	Entity* entData;
	LumpState oldLumps;
	bool initialized = false;
	float size;

	CreateBspModelCommand(string desc, int mapIdx, Entity* entData, float size);
	~CreateBspModelCommand();

	void execute();
	void undo();
	int memoryUsage();

private:
	int getDefaultTextureIdx();
	int addDefaultTexture();
};


class EditBspModelCommand : public Command {
public:
	int modelIdx;
	int entIdx;
	vec3 oldOrigin;
	vec3 newOrigin;
	LumpState oldLumps;
	LumpState newLumps;

	EditBspModelCommand(string desc, PickInfo& pickInfo, LumpState oldLumps, LumpState newLumps, vec3 oldOrigin);
	~EditBspModelCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class CleanMapCommand : public Command {
public:
	LumpState oldLumps;

	CleanMapCommand(string desc, int mapIdx, LumpState oldLumps);
	~CleanMapCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class OptimizeMapCommand : public Command {
public:
	LumpState oldLumps;

	OptimizeMapCommand(string desc, int mapIdx, LumpState oldLumps);
	~OptimizeMapCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};
