#include "Widget.h"

void ModelMergeWidget::setup() {
	ImGui::SetNextWindowContentSize(ImVec2(widgetSizeDefault.x * uiScale, 0));
}

void ModelMergeWidget::draw() {
	ImGui::TextWrapped("The selected models have clipnodes that are overlapping or can't be merged simply.\n\n"
		"Inspect clipnodes after merging. They will likely be broken.");

	ImGui::Dummy(ImVec2(0, 10));

	if (ImGui::Button("Merge")) {
		LumpReplaceCommand* command = new LumpReplaceCommand("Merge Models");
		Bsp* map = app->mapRenderer->map;
		int newIndex = map->merge_models(app->pickInfo.getEnts(), true);

		if (newIndex >= 0 || newIndex == -3) {
			command->pushUndoState();
		}
		else {
			delete command;
		}

		close();
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		close();
	}
}