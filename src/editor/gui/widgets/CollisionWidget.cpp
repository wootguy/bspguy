#include "Widget.h"
#include <algorithm>

void CollisionWidget::draw() {
	ImGui::TextWrapped("Click a collision block, then Delete. Ctrl+click adds or removes blocks. Drag to select through the scene. Selected volumes are orange.");
	ImGui::Separator();
	if (g_settings.ripent_safe_mode) {
		ImGui::TextWrapped("Turn off Ripent Safe Mode in Settings > Editor Setup to edit and save collision.");
		return;
	}
	if (app->pickMode != PICK_CLIPNODE) {
		ImGui::BeginDisabled(app->isLoading || app->emptyMapLoaded || app->mapArrangeMode);
		if (ImGui::Button("Select collision blocks"))
			app->selectCollisionMode();
		ImGui::EndDisabled();
		return;
	}

	int hull = app->clipnodeRenderHull - 1;
	ImGui::BeginDisabled(app->isLoading);
	if (ImGui::Combo("Collision hull", &hull, "1 - Standing player\0" "2 - Large objects\0" "3 - Crouching player\0"))
		app->selectCollisionMode(hull + 1);
	ImGui::EndDisabled();
	ImGui::TextWrapped("Only the displayed hull is edited. Check standing and crouching hulls when clearing a player obstacle. Visual models are selected and deleted in Object mode.");

	if (app->isLoading || !app->mapRenderer->collisionReady(app->clipnodeRenderHull)) {
		ImGui::TextUnformatted("Loading collision volumes...");
		return;
	}

	int count = (int)app->pickInfo.clipnodes.size();
	ImGui::Text("%d collision volume(s) selected", count);
	ImGui::BeginDisabled(count == 0);
	if (ImGui::Button("Delete selected collision", ImVec2(-1, 0))) {
		app->deleteSelectedCollision();
		ImGui::EndDisabled();
		return;
	}
	if (ImGui::Button("Clear selection")) {
		app->pickInfo.clipnodes.clear();
		app->mapRenderer->updateCollisionSelection();
		app->pickCount++;
	}
	ImGui::EndDisabled();
	ImGui::TextUnformatted("Undo: Ctrl+Z    Redo: Ctrl+Y");

	if (ImGui::CollapsingHeader("Select at coordinates")) {
		ImGui::InputFloat3("Position", &point.x, "%.1f");
		if (ImGui::Button("Use camera position"))
			point = app->cameraOrigin;
		ImGui::Checkbox("Add to selection", &addToSelection);
		if (ImGui::Button("Select block at position")) {
			int idx = app->mapRenderer->collisionAtPoint(point, app->clipnodeRenderHull);
			if (!addToSelection)
				app->pickInfo.clipnodes.clear();
			if (idx >= 0 && std::find(app->pickInfo.clipnodes.begin(), app->pickInfo.clipnodes.end(), idx) == app->pickInfo.clipnodes.end())
				app->pickInfo.clipnodes.push_back(idx);
			pointIsEmpty = idx < 0;
			app->mapRenderer->updateCollisionSelection();
			app->pickCount++;
		}
		if (pointIsEmpty)
			ImGui::TextUnformatted("No solid collision block at this position.");
	}
}
