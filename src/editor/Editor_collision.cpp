#include "Editor.h"
#include "Gui.h"
#include "Widget.h"
#include "Command.h"
#include <algorithm>

void Editor::setClipnodeHull(int hull) {
	if (isLoading)
		return;
	if (pickMode == PICK_CLIPNODE) {
		if (hull > 0) {
			selectCollisionMode(hull);
			return;
		}
		deselectObject();
		pickMode = PICK_OBJECT;
		mapRenderer->updateCollisionSelection();
	}
	clipnodeRenderHull = hull;
	isLoading = true;
	mapRenderer->loadMoreClipnodes();
}
void Editor::selectCollisionMode(int hull) {
	if (isLoading || mapArrangeMode || emptyMapLoaded || g_settings.ripent_safe_mode
		|| hull < 1 || hull >= MAX_MAP_HULLS)
		return;

	bool entering = pickMode != PICK_CLIPNODE;
	deselectFaces();
	deselectObject();
	hiddenLeaves.clear();
	transformTarget = TRANSFORM_OBJECT;
	pickMode = PICK_CLIPNODE;
	clipnodeRenderHull = hull;
	g_settings.render_flags |= RENDER_WORLD_CLIPNODES;
	gui->widgets[WIDGET_FACE_EDITOR]->widgetVisible = false;
	gui->widgets[WIDGET_COLLISION]->widgetVisible = true;
	mapRenderer->updateCollisionSelection();
	if (entering) {
		isLoading = true;
		mapRenderer->reloadClipnodes();
	}
}

void Editor::pickCollisionVolumes(bool boxSelect) {
	if (isLoading || previewMode || !mapRenderer->collisionReady(clipnodeRenderHull))
		return;

	unordered_set<int> clicked;
	if (boxSelect) {
		mapRenderer->pickCollisionFrustum(getPickFrustum(), clipnodeRenderHull, clicked);
	}
	else {
		vec3 start, dir;
		getPickRay(start, dir);
		int volume = mapRenderer->pickCollision(start, dir, clipnodeRenderHull);
		if (volume >= 0)
			clicked.insert(volume);
	}
	vector<int>& selected = pickInfo.clipnodes;
	if (!anyCtrlPressed)
		selected.clear();
	for (int volume : clicked) {
		auto found = std::find(selected.begin(), selected.end(), volume);
		if (found == selected.end())
			selected.push_back(volume);
		else if (!boxSelect)
			selected.erase(found);
	}
	mapRenderer->updateCollisionSelection();
	pickCount++;
}

void Editor::deleteSelectedCollision() {
	if (pickMode != PICK_CLIPNODE || isLoading || mapArrangeMode || previewMode
		|| g_settings.ripent_safe_mode || pickInfo.clipnodes.empty()
		|| !mapRenderer->collisionReady(clipnodeRenderHull))
		return;

	const vector<NodeVolumeCuts>& volumes = mapRenderer->getCollisionVolumes(clipnodeRenderHull);
	vector<vector<int>> branches;
	for (int idx : pickInfo.clipnodes) {
		if (idx < 0 || idx >= (int)volumes.size())
			return;
		branches.push_back(volumes[idx].branch);
	}
	CollisionEditCommand* command = new CollisionEditCommand("Delete World Collision (Hull "
		+ to_string(clipnodeRenderHull) + ")");
	int removed = mapRenderer->map->delete_clipnode_volumes(0, clipnodeRenderHull, branches);
	if (removed <= 0) {
		logf("Could not delete the selected collision volumes. No changes made.\n");
		delete command;
		return;
	}
	logf("Deleted %d world collision volumes from hull %d. Other hulls are unchanged.\n",
		removed, clipnodeRenderHull);
	command->pushUndoState();
}
