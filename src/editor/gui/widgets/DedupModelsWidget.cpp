#include "Widget.h"
#include "Entity.h"

void DedupModelsWidget::draw() {
	static bool allowShift = false;
	static int dedupEstimate = -1;

	ImGui::Text("Model count will be lowered by: %d", dedupEstimate);

	if (dedupEstimate == -1) {
		dedupEstimate = map->deduplicate_models(allowShift, true);
	}

	ImGui::Dummy(ImVec2(0, 10));

	if (ImGui::Checkbox("Allow Shifted Textures", &allowShift)) {
		dedupEstimate = -1;
	}
	tooltip("Ignore texture shifts when deduplicating. "
		"Textures must still match for every face but the shift values don't need to be exactly the same.");

	ImGui::Dummy(ImVec2(0, 10));

	if (ImGui::Button("Deduplicate")) {
		LumpReplaceCommand* command = new LumpReplaceCommand("Deduplicate models");
		map->deduplicate_models(allowShift, false);
		command->pushUndoState();
		close();
		dedupEstimate = -1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Select Unique")) {
		app->deselectObject();

		for (int m = 1; m < map->modelCount; m++) {
			for (int i = 0; i < map->ents.size(); i++) {
				if (map->ents[i]->getBspModelIdx() == m) {
					app->pickInfo.selectEnt(i);
					break;
				}
			}
		}

		app->postSelectEnt();
		close();
		dedupEstimate = -1;
	}
	tooltip("Don't deduplicate models, just select entities that use a unique model.\n\nUse this then drag the entities out to space to see which models need merging (for when deduplication isn't enough).");

	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		close();
		dedupEstimate = -1;
	}

}