#pragma once
#include "BspRenderer.h"
#include "bsptypes.h"
#include "unordered_set"
#include "Editor.h"

// Undoable actions following the Command Pattern

class Command {
public:
	string desc;
	bool allowedDuringLoad = false;

	Command(string desc);
	virtual ~Command() {};
	virtual void execute() = 0;
	virtual void undo() = 0;
	virtual int memoryUsage() = 0;
	
	BspRenderer* getBspRenderer();
	Bsp* getBsp();
};


class EditEntitiesCommand : public Command {
public:
	vector<int> entIndexes;
	vector<Entity*> oldEntData;
	vector<Entity*> newEntData;

	EditEntitiesCommand(string desc, vector<EntityState>& oldEntData);
	~EditEntitiesCommand();

	void execute();
	void undo();
	Entity* getEntForIndex(int idx);
	void refresh();
	int memoryUsage();
};


class DeleteEntitiesCommand : public Command {
public:
	vector<int> entIndexes;
	vector<Entity*> entData;

	DeleteEntitiesCommand(string desc, vector<int> delEnts);
	~DeleteEntitiesCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class CreateEntitiesCommand : public Command {
public:
	vector<Entity*> entData;

	CreateEntitiesCommand(string desc, vector<Entity*> entData);
	~CreateEntitiesCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class CreateEntityFromTextCommand : public Command {
public:
	string textData;
	int createdEnts;

	CreateEntityFromTextCommand(string desc, string textData);
	~CreateEntityFromTextCommand();

	vector<Entity*> parse();
	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class EditBspModelCommand : public Command {
public:
	int modelIdx;
	int entIdx;
	vec3 oldOrigin;
	vec3 newOrigin;
	LumpState oldLumps = LumpState();
	LumpState newLumps = LumpState();

	EditBspModelCommand(string desc, PickInfo& pickInfo, LumpState oldLumps, LumpState newLumps, vec3 oldOrigin);
	~EditBspModelCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


// works differently than other commands. Create the command, do your edits, then pushUndoState.
class LumpReplaceCommand : public Command {
public:
	LumpState newLumps = LumpState();
	LumpState oldLumps = LumpState();
	bool differences[HEADER_LUMPS];
	bool norefresh;
	vector<int> modelRefreshes;

	LumpReplaceCommand(string desc, bool noRefresh = false);
	~LumpReplaceCommand();

	void execute();
	void pushUndoState(bool norefresh=false); // call after you've edited lumps. Not called by redo.
	void undo();
	virtual void refresh();
	int memoryUsage();
};

// refreshes sepcific models instead of the entire map
// TODO: replace the other model edit command with this
class ModelEditCommand : public LumpReplaceCommand {
public:
	vector<int> modelIndexes;

	ModelEditCommand(string desc, vector<int> modelIndexes);

	void refresh() override;
	int memoryUsage() override;
};


// refreshes specific models after face edits
class FacesEditCommand : public LumpReplaceCommand {
public:
	unordered_set<int> modelRefreshes;
	vector<int> faces;
	bool textureDataReloadNeeded;

	FacesEditCommand(string desc);

	void refresh() override;
	int memoryUsage() override;
};


// refreshes lightmaps after edit
class LightmapsEditCommand : public LumpReplaceCommand {
public:
	LightmapsEditCommand(string desc);

	void refresh() override;
	int memoryUsage() override;
};
