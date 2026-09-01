#include "Editor.h"
#include "BspRenderer.h"
#include "Entity.h"
#include "Fgd.h"
#include "Command.h"
#include "Gui.h"

void Editor::moveGrabbedEnts() {
	// grabbing
	if (movingEnt && pickInfo.getEntIndex() > 0) {
		if (g_scroll != oldScroll) {
			float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 4.0f : 2.0f;
			if (pressed[GLFW_KEY_LEFT_CONTROL])
				moveScale = 1.0f;
			if (g_scroll < oldScroll)
				moveScale *= -1;

			grabDist += 16 * moveScale;
		}

		Bsp* map = mapRenderer->map;
		vec3 mapOffset = mapRenderer->mapOffset;
		vec3 delta = ((cameraOrigin - mapOffset) + cameraForward * grabDist) - grabStartOrigin;

		for (int i = 0; i < pickInfo.ents.size(); i++) {
			int entidx = pickInfo.ents[i];
			Entity* ent = map->ents[entidx];
			vec3 oldOrigin = grabStartEntOrigin[i];
			vec3 newOrigin = (oldOrigin + delta);
			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

			transformedOrigin = this->oldOrigin = rounded;

			ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
			mapRenderer->refreshEnt(entidx);
		}
		updateEntConnectionPositions();
	}
	else {
		ungrabEnts();
	}
}

vec3 Editor::getEntOrigin(Bsp* map, Entity* ent) {
	return ent->getOrigin() + getEntOffset(map, ent);
}

vec3 Editor::getEntOffset(Bsp* map, Entity* ent) {
	int modelIdx = ent->getBspModelIdx();
	if (modelIdx > 0 && modelIdx < map->modelCount) {
		BSPMODEL& model = map->models[modelIdx];
		vec3 modelCenter = model.nMins + (model.nMaxs - model.nMins) * 0.5f;

		if (ent->canRotate()) {
			modelCenter = (ent->getRotationMatrix(true) * vec4(modelCenter, 1)).xyz();
		}

		return modelCenter;
	}
	return vec3(0, 0, 0);
}

void Editor::grabEnts() {
	if (pickInfo.getEntIndex() <= 0)
		return;
	movingEnt = true;
	Bsp* map = mapRenderer->map;
	vec3 mapOffset = mapRenderer->mapOffset;
	vec3 localCamOrigin = cameraOrigin - mapOffset;
	grabDist = (getEntOrigin(map, map->ents[pickInfo.getEntIndex()]) - localCamOrigin).length();

	vec3 centroid;
	grabStartEntOrigin.clear();
	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* ent = map->ents[pickInfo.ents[i]];
		vec3 ori = getEntOrigin(map, ent);
		centroid += ori;
		grabStartEntOrigin.push_back(ent->getOrigin());
	}
	centroid /= (float)pickInfo.ents.size();

	grabStartOrigin = centroid;
}

void Editor::ungrabEnts() {
	if (!movingEnt) {
		return;
	}

	movingEnt = false;

	int plural = pickInfo.ents.size() > 1;
	pushEntityUndoState(plural ? "Move Entities" : "Move Entity");
	pickCount++; // force transform window to recalc offsets
}

void Editor::unhideSelectedEnts() {
	vector<Entity*> ents = pickInfo.getEnts();

	if (ents.empty())
		return;

	for (Entity* ent : ents) {
		ent->hidden = false;
	}

	anyHiddenEnts = false;
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->hidden) {
			anyHiddenEnts = true;
			break;
		}
	}

	deselectObject();
	mapRenderer->preRenderEnts();
}

void Editor::hideSelectedEnts() {
	vector<Entity*> ents = pickInfo.getEnts();

	if (ents.empty() || mapArrangeMode)
		return;

	for (Entity* ent : ents) {
		ent->hidden = true;
	}

	deselectObject();
	anyHiddenEnts = true;
	mapRenderer->preRenderEnts();
}

void Editor::unhideEnts() {
	vector<Entity*> ents = pickInfo.getEnts();
	Bsp* map = mapRenderer->map;

	int numHidden = 0;

	for (int i = 0; i < map->ents.size(); i++) {
		if (map->ents[i]->hidden)
			numHidden++;
		map->ents[i]->hidden = false;
	}

	anyHiddenEnts = false;
	mapRenderer->preRenderEnts();
	mapRenderer->reloadMegaBuffers();
	logf("Unhid %d entities\n", numHidden);
}

void Editor::cutEnts() {
	if (pickInfo.getEntIndex() <= 0 || mapArrangeMode)
		return;

	Bsp* map = mapRenderer->map;

	string serialized = "";

	vector<int> indexes;

	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* copy = new Entity();
		*copy = *map->ents[pickInfo.ents[i]];
		serialized += copy->serialize();
		indexes.push_back(pickInfo.ents[i]);
	}

	DeleteEntitiesCommand* deleteCommand = new DeleteEntitiesCommand("Cut Entity", indexes);
	deleteCommand->execute();
	pushUndoCommand(deleteCommand);

	ImGui::SetClipboardText(serialized.c_str());
}

void Editor::copyEnts(bool stringifyBspModels) {
	if (pickInfo.getEntIndex() <= 0 || mapArrangeMode)
		return;

	Bsp* map = mapRenderer->map;

	string serialized = "";

	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* copy = new Entity();
		*copy = *map->ents[pickInfo.ents[i]];
		serialized += copy->serialize(stringifyBspModels);
	}

	lastCopyStringifiedModels = stringifyBspModels;

	ImGui::SetClipboardText(serialized.c_str());
}

bool Editor::canPasteEnts() {
	const char* clipBoardText = ImGui::GetClipboardText();
	if (!clipBoardText) {
		return false;
	}

	CreateEntityFromTextCommand createCommand("", clipBoardText);
	return !createCommand.parse().empty();
}

void Editor::pasteEnts(bool noModifyOrigin) {
	if (mapArrangeMode)
		return;

	const char* clipBoardText = ImGui::GetClipboardText();
	if (!clipBoardText) {
		logf("No entity data in clipboard\n");
		return;
	}

	Bsp* map = pickInfo.getMap() ? pickInfo.getMap() : mapRenderer->map;

	CreateEntityFromTextCommand* createCommand =
		new CreateEntityFromTextCommand("Paste entities", clipBoardText);
	createCommand->execute();

	if (createCommand->createdEnts == 0) {
		logf("No entity data in clipboard\n");
		return;
	}

	logf("Pasted %d entities from clipboard\n", createCommand->createdEnts);

	pushUndoCommand(createCommand);

	bool shouldReload = false;

	vec3 centroid;
	for (int i = 0; i < createCommand->createdEnts; i++) {
		Entity* ent = map->ents[map->ents.size() - (1 + i)];
		shouldReload |= ent->deserialize();
		centroid += getEntOrigin(map, ent);
	}
	centroid /= (float)createCommand->createdEnts;

	pickInfo.deselect();

	string reserialized;

	for (int i = 0; i < createCommand->createdEnts; i++) {
		if (!noModifyOrigin) {
			Entity* ent = map->ents[map->ents.size() - (1 + i)];
			vec3 oldOrigin = getEntOrigin(map, ent);
			vec3 centroidOffset = oldOrigin - centroid;
			vec3 modelOffset = getEntOffset(map, ent);
			vec3 mapOffset = mapRenderer->mapOffset;

			vec3 moveDist = (cameraOrigin + cameraForward * 100) - oldOrigin;
			vec3 newOri = (oldOrigin + moveDist + centroidOffset) - (modelOffset + mapOffset);
			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
			ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));

			// serialize again so that redoing this command will have the offset applied
			reserialized += ent->serialize(lastCopyStringifiedModels);
		}
		pickInfo.selectEnt(map->ents.size() - (1 + i));
	}

	if (!noModifyOrigin) {
		createCommand->textData = reserialized;
	}

	if (shouldReload) {
		mapRenderer->reload();
	}

	if (createCommand->createdEnts)
		createCommand->refresh();

	postSelectEnt();
}

void Editor::pasteEntsFromText(string text, bool noModifyOrigin) {
	if (mapArrangeMode)
		return;
	Bsp* map = pickInfo.getMap() ? pickInfo.getMap() : mapRenderer->map;

	CreateEntityFromTextCommand* createCommand =
		new CreateEntityFromTextCommand("Paste entities from clipboard", text);
	createCommand->execute();

	if (createCommand->createdEnts == 0) {
		logf("No entity data in clipboard\n");
		return;
	}

	pushUndoCommand(createCommand);

	vec3 centroid;
	for (int i = 0; i < createCommand->createdEnts; i++) {
		Entity* ent = map->ents[map->ents.size() - (1 + i)];
		centroid += getEntOrigin(map, ent);
	}
	centroid /= (float)createCommand->createdEnts;

	pickInfo.deselect();

	for (int i = 0; i < createCommand->createdEnts; i++) {
		if (!noModifyOrigin) {
			Entity* ent = map->ents[map->ents.size() - (1 + i)];
			vec3 oldOrigin = getEntOrigin(map, ent);
			vec3 centroidOffset = oldOrigin - centroid;
			vec3 modelOffset = getEntOffset(map, ent);
			vec3 mapOffset = mapRenderer->mapOffset;

			vec3 moveDist = (cameraOrigin + cameraForward * 100) - oldOrigin;
			vec3 newOri = (oldOrigin + moveDist + centroidOffset) - (modelOffset + mapOffset);
			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
			ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
		}
		pickInfo.selectEnt(map->ents.size() - (1 + i));
	}

	if (createCommand->createdEnts)
		createCommand->refresh();
	postSelectEnt();
}

void Editor::deleteEnts() {
	if (pickInfo.getEntIndex() <= 0 || mapArrangeMode)
		return;

	DeleteEntitiesCommand* deleteCommand = new DeleteEntitiesCommand("Delete Entity", pickInfo.ents);
	deleteCommand->execute();
	pushUndoCommand(deleteCommand);
}

void Editor::postSelectEnt() {
	updateSelectionSize();
	updateEntConnections();
	updateEntityUndoState();
	pickCount++; // force transform window update
}

void Editor::deselectObject() {
	ungrabEnts();

	if (pickInfo.getEnt() && pickInfo.getEnt()->isBspModel())
		saveLumpState(pickInfo.getMap(), 0xffffffff, true);

	// update deselected point ents
	for (int entIdx : pickInfo.ents) {
		Entity* ent = pickInfo.getMap()->ents[entIdx];
		if (!ent->isBspModel()) {
			mapRenderer->refreshPointEnt(entIdx, false);
		}
	}
	mapRenderer->pointEnts->deleteBuffer();
	mapRenderer->pointEnts->upload();

	pickInfo.deselect();
	isTransformableSolid = true;
	modelUsesSharedStructures = false;
	hoverVert = -1;
	hoverEdge = -1;
	hoverAxis = -1;
	updateEntConnections();
}

void Editor::updateEntityUndoState() {
	//logf("Update entity undo state\n");
	for (int i = 0; i < undoEntityState.size(); i++)
		delete undoEntityState[i].ent;
	undoEntityState.clear();

	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* ent = pickInfo.getMap()->ents[pickInfo.ents[i]];

		EntityState state;
		state.ent = new Entity();
		*state.ent = *ent;
		state.index = pickInfo.ents[i];
		undoEntityState.push_back(state);
	}

	if (pickInfo.getEnt())
		undoEntOrigin = pickInfo.getEnt()->getOrigin();
}

void Editor::updateEntityLumpUndoState(Bsp* map) {
	if (undoLumpState.lumps[LUMP_ENTITIES])
		delete[] undoLumpState.lumps[LUMP_ENTITIES];

	LumpState dupLump = map->duplicate_lumps(LUMP_ENTITIES);
	undoLumpState.lumps[LUMP_ENTITIES] = dupLump.lumps[LUMP_ENTITIES];
	undoLumpState.lumpLen[LUMP_ENTITIES] = dupLump.lumpLen[LUMP_ENTITIES];
}

bool Editor::canPushEntityUndoState() {
	if (!undoEntityState.size()) {
		return false;
	}
	if (undoEntityState.size() != pickInfo.ents.size()) {
		return true;
	}

	Bsp* map = pickInfo.getMap();
	for (int i = 0; i < pickInfo.ents.size(); i++) {
		int currentIdx = undoEntityState[i].index;
		if (currentIdx >= map->ents.size() || currentIdx != pickInfo.ents[i]) {
			return true;
		}

		Entity* currentEnt = map->ents[currentIdx];
		Entity* undoEnt = undoEntityState[i].ent;

		if (undoEnt->keyOrder.size() == currentEnt->keyOrder.size()) {
			for (int i = 0; i < undoEnt->keyOrder.size(); i++) {
				string oldKey = undoEnt->keyOrder[i];
				string newKey = currentEnt->keyOrder[i];
				if (oldKey != newKey) {
					return true;
				}
				string oldVal = undoEnt->getKeyvalue(oldKey);
				string newVal = currentEnt->getKeyvalue(oldKey);
				if (oldVal != newVal) {
					return true;
				}
			}
		}
		else {
			return true;
		}
	}

	return false;
}

void Editor::pushEntityUndoState(string actionDesc) {
	if (!canPushEntityUndoState()) {
		//logf("nothint to undo\n");
		return; // nothing to undo
	}

	if (g_app->pickInfo.ents.size() != undoEntityState.size()) {
		debugf("Pushed undo state with bad size\n");
		return;
	}

	//logf("Push undo state: %s\n", actionDesc.c_str());
	pushUndoCommand(new EditEntitiesCommand(actionDesc, undoEntityState));
	updateEntityUndoState();
}

bool Editor::entityHasFgd(string cname) {
	return mergedFgd ? mergedFgd->getFgdClass(cname) != NULL : false;
}

vector<Entity*>& Editor::ents() {
	static vector<Entity*> dummyList;
	return (mapRenderer && mapRenderer->map) ? mapRenderer->map->ents : dummyList;
}
