#pragma once
#include "BspRenderer.h"
#include "bsptypes.h"

// Undoable actions following the Command Pattern

class Command {
public:
	string desc;
	bool allowedDuringLoad = false;

	Command(string desc);
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
	LumpState oldLumps = LumpState();
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
	LumpState oldLumps = LumpState();
	bool initialized = false;
	float size;

	CreateBspModelCommand(string desc, Entity* entData, float size);
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
	LumpState oldLumps = LumpState();
	LumpState newLumps = LumpState();

	EditBspModelCommand(string desc, PickInfo& pickInfo, LumpState oldLumps, LumpState newLumps, vec3 oldOrigin);
	~EditBspModelCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class CleanMapCommand : public Command {
public:
	LumpState oldLumps = LumpState();

	CleanMapCommand(string desc, LumpState oldLumps);
	~CleanMapCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};


class OptimizeMapCommand : public Command {
public:
	LumpState oldLumps = LumpState();

	OptimizeMapCommand(string desc, LumpState oldLumps);
	~OptimizeMapCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};

class DeleteBoxedDataCommand : public Command {
public:
	LumpState oldLumps = LumpState();
	vec3 mins, maxs;

	DeleteBoxedDataCommand(string desc, vec3 mins, vec3 maxs, LumpState oldLumps);
	~DeleteBoxedDataCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};

class DeleteOobDataCommand : public Command {
public:
	LumpState oldLumps = LumpState();
	int clipFlags;

	DeleteOobDataCommand(string desc, int clipFlags, LumpState oldLumps);
	~DeleteOobDataCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};

class FixSurfaceExtentsCommand : public Command {
public:
	LumpState oldLumps = LumpState();
	bool scaleNotSubdivide;
	bool downscaleOnly;
	int maxTextureDim;

	FixSurfaceExtentsCommand(string desc, bool scaleNotSubdivide, bool downscaleOnly, int maxTextureDim, LumpState oldLumps);
	~FixSurfaceExtentsCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};

class DeduplicateModelsCommand : public Command {
public:
	LumpState oldLumps = LumpState();

	DeduplicateModelsCommand(string desc, LumpState oldLumps);
	~DeduplicateModelsCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};

class MoveMapCommand : public Command {
public:
	LumpState oldLumps = LumpState();
	vec3 offset;

	MoveMapCommand(string desc, vec3 offset, LumpState oldLumps);
	~MoveMapCommand();

	void execute();
	void undo();
	void refresh();
	int memoryUsage();
};
