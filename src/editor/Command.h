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
