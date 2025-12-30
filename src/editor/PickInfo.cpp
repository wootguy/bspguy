#include "PickInfo.h"
#include "Bsp.h"
#include "Entity.h"
#include "util.h"
#include "Editor.h"
#include "BspRenderer.h"

void PickInfo::selectEnt(int entIdx) {
	Bsp* map = getMap();

	if (entIdx >= 0 && entIdx < map->ents.size()) {
		for (int i = 0; i < ents.size(); i++) {
			if (ents[i] == entIdx) {
				return;
			}
		}
		ents.push_back(entIdx);
	}
	else
		logf("Failed to select ent index out of range %d\n", entIdx);

	//logf("select ent %d\n", entIdx);
}

void PickInfo::selectFace(int faceIdx) {
	for (int i = 0; i < faces.size(); i++) {
		if (faces[i] == faceIdx) {
			return;
		}
	}
	faces.push_back(faceIdx);
	//logf("select face %d\n", faceIdx);
}

void PickInfo::selectLeaf(int leafIdx) {
	for (int i = 0; i < leaves.size(); i++) {
		if (leaves[i] == leafIdx) {
			return;
		}
	}
	leaves.push_back(leafIdx);
}

void PickInfo::deselect() {
	ents.clear();
	faces.clear();
	leaves.clear();
	g_app->pickCount++;
	//logf("Deselect\n");
}

void PickInfo::deselectEnt(int entIdx) {
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i] == entIdx) {
			ents.erase(ents.begin() + i);
			return;
		}
	}
}

void PickInfo::deselectFace(int faceIdx) {
	for (int i = 0; i < faces.size(); i++) {
		if (faces[i] == faceIdx) {
			faces.erase(faces.begin() + i);
			return;
		}
	}
}

void PickInfo::deselectLeaf(int leafIdx) {
	for (int i = 0; i < leaves.size(); i++) {
		if (leaves[i] == leafIdx) {
			leaves.erase(leaves.begin() + i);
			return;
		}
	}
}

Bsp* PickInfo::getMap() {
	return g_app->mapRenderer->map;
}

Entity* PickInfo::getEnt() {
	Bsp* map = getMap();
	int idx = getEntIndex();
	return idx != -1 ? map->ents[idx] : NULL;
}

int PickInfo::getEntIndex() {
	Bsp* map = getMap();
	if (ents.size() && map && ents[0] >= 0 && ents[0] < map->ents.size()) {
		return ents[0];
	}
	return -1;
}

int PickInfo::getModelIndex() {
	Bsp* map = getMap();
	int idx = getEntIndex();
	Entity* ent = idx != -1 ? map->ents[idx] : NULL;
	int faceIdx = faces.size() == 1 ? faces[0] : -1;

	if (idx == 0) {
		return 0;
	}
	else if (ent) {
		return ent->getBspModelIdx();
	}
	else if (faceIdx >= 0 && faceIdx < map->faceCount) {
		return map->get_model_from_face(faceIdx);
	}

	return -1;
}

BSPMODEL* PickInfo::getModel() {
	Bsp* map = getMap();
	int idx = getModelIndex();

	return idx > 0 && idx < map->modelCount ? &map->models[idx] : NULL;
}

BSPFACE* PickInfo::getFace() {
	Bsp* map = getMap();
	int idx = getFaceIndex();
	return idx >= 0 ? &map->faces[idx] : NULL;
}

int PickInfo::getFaceIndex() {
	Bsp* map = getMap();
	int faceIdx = faces.size() == 1 ? faces[0] : -1;
	return faceIdx >= 0 && faceIdx < map->faceCount ? faceIdx : -1;
}

int PickInfo::getLeafIndex() {
	Bsp* map = getMap();
	int leafIdx = leaves.size() == 1 ? leaves[0] : -1;
	return leafIdx >= 0 && leafIdx < map->leafCount ? leafIdx : -1;
}

vec3 PickInfo::getOrigin() {
	Entity* ent = getEnt();
	return ent ? ent->getOrigin() : vec3();
}

bool PickInfo::isFaceSelected(int faceIdx) {
	for (int idx : faces) {
		if (idx == faceIdx) {
			return true;
		}
	}

	return false;
}

bool PickInfo::isLeafSelected(int leafIdx) {
	for (int idx : leaves) {
		if (idx == leafIdx) {
			return true;
		}
	}

	return false;
}

bool PickInfo::isEntSelected(int entIdx) {
	for (int idx : ents) {
		if (idx == entIdx) {
			return true;
		}
	}

	return false;
}

vector<Entity*> PickInfo::getEnts() {
	vector<Entity*> outEnts;
	Bsp* map = getMap();

	for (int i = 0; i < ents.size(); i++) {
		int idx = ents[i];
		if (map && idx >= 0 && idx < map->ents.size()) {
			outEnts.push_back(map->ents[idx]);
		}
	}

	return outEnts;
}

vector<BSPFACE*> PickInfo::getFaces() {
	vector<BSPFACE*> outFaces;
	Bsp* map = getMap();

	for (int i = 0; i < faces.size(); i++) {
		int idx = faces[i];
		if (map && idx >= 0 && idx < map->faceCount) {
			outFaces.push_back(&map->faces[idx]);
		}
	}

	return outFaces;
}

vector<BSPLEAF*> PickInfo::getLeaves() {
	vector<BSPLEAF*> outLeaves;
	Bsp* map = getMap();

	for (int i = 0; i < leaves.size(); i++) {
		int idx = leaves[i];
		if (map && idx >= 0 && idx < map->leafCount) {
			outLeaves.push_back(&map->leaves[idx]);
		}
	}

	return outLeaves;
}

vector<int> PickInfo::getModelIndexes() {
	vector<int> outIdx;
	Bsp* map = getMap();

	for (int i = 0; i < ents.size(); i++) {
		int modelIdx = map->ents[ents[i]]->getBspModelIdx();
		if (modelIdx >= 0)
			outIdx.push_back(modelIdx);
	}
	for (int i = 0; i < faces.size(); i++) {
		outIdx.push_back(map->get_model_from_face(faces[i]));
	}

	return outIdx;
}

bool PickInfo::shouldHideSelection() {
	bool shouldHide = false;
	vector<Entity*> pickEnts = getEnts();
	for (Entity* ent : pickEnts) {
		if (!ent->hidden) {
			return true;
		}
	}

	return false;
}

void PickInfo::selectLeafFaces() {
	Bsp* map = getMap();
	faces.clear();

	for (int i = 0; i < leaves.size(); i++) {
		int idx = leaves[i];
		if (map && idx >= 0 && idx < map->leafCount) {
			BSPLEAF& leaf = map->leaves[idx];

			for (int k = 0; k < leaf.nMarkSurfaces; k++) {
				selectFace(map->marksurfs[leaf.iFirstMarkSurface + k]);
			}
		}
	}
}
