#pragma once
#include <vector>
#include "bsptypes.h"
#include "vectors.h"

class Bsp;
class Entity;


class PickInfo {
public:
	std::vector<int> ents; // selected entity indexes
	std::vector<int> faces; // selected face indexes
	std::vector<int> leaves; // selected leaf indexes

	PickInfo() {}

	Bsp* getMap();
	void selectEnt(int entIdx);
	void selectFace(int faceIdx);
	void selectLeaf(int leafIdx);
	void deselect();
	void deselectEnt(int entIdx);
	void deselectFace(int faceIdx);
	void deselectLeaf(int leafIdx);
	Entity* getEnt();
	int getEntIndex();
	int getModelIndex();
	BSPMODEL* getModel();
	BSPFACE* getFace();
	int getFaceIndex();
	int getLeafIndex();
	vec3 getOrigin(); // origin of the selected entity
	bool isFaceSelected(int faceIdx);
	bool isLeafSelected(int leafIdx);
	bool isEntSelected(int entIdx);
	std::vector<Entity*> getEnts();
	std::vector<BSPFACE*> getFaces();
	std::vector<BSPLEAF*> getLeaves();
	std::vector<int> getModelIndexes();
	bool shouldHideSelection();
	void selectLeafFaces(); // highlights all faces referenced in selected leaves
};