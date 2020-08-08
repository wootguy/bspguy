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

	Command(string desc, int mapIdx);
	virtual bool execute() = 0;
	virtual bool undo() = 0;
	
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

	bool execute();
	bool undo();
	Entity* getEnt();
	bool refresh();
};


class DeleteEntityCommand : public Command {
public:
	int entIdx;
	Entity* entData;

	DeleteEntityCommand(string desc, PickInfo& pickInfo);
	~DeleteEntityCommand();

	bool execute();
	bool undo();
	bool refresh();
};


class CreateEntityCommand : public Command {
public:
	Entity* entData;

	CreateEntityCommand(string desc, int mapIdx, Entity* entData);
	~CreateEntityCommand();

	bool execute();
	bool undo();
	bool refresh();
};