#include "Widget.h"

void FixExtentsWidget::draw() {
	static int minDim = 224;
	static int step = 16;
	static int subdivideMax = 20;
	static bool shouldDownscale = true;
	static bool shouldSubdivide = false;
	static bool shouldScale = false;

	ImGui::TextWrapped("Fixes are applied in order.");

	ImVec2 cellPadding(5.0f * uiScale, 10.0f * uiScale); // x = horizontal, y = vertical
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);

	if (ImGui::BeginTable("MyTable", 4, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Fixed1", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Fixed2", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Fixed3", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Auto");

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		ImGui::Text("1)");
		ImGui::TableNextColumn();
		ImGui::Checkbox("Subdivide Faces", &shouldSubdivide);
		tooltip("Subdivide faces for a texture if additional faces would be below the Subdivide Limit. The drawback to this method is reduced in-game performace from higher poly counts.");

		ImGui::TableNextColumn();
		ImGui::Dummy(ImVec2(20 * uiScale, 0));

		ImGui::TableNextColumn();
		ImGui::BeginDisabled(!shouldSubdivide);
		ImGui::SetNextItemWidth(200 * uiScale);
		ImGui::DragInt("Face Limit", &subdivideMax, 0.1f, 0, g_limits.max_faces);
		tooltip("This limit is applied per texture, not globally. All faces for a texture will be subdivided only if the newly created face count is less than this limit.\n");
		ImGui::EndDisabled();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("2)");

		ImGui::TableNextColumn();
		ImGui::Checkbox("Downscale Textures", &shouldDownscale);
		tooltip("Downscale a texture if subdivision would create more faces than desired.\n");

		ImGui::TableNextColumn();
		ImGui::Dummy(ImVec2(20 * uiScale, 0));

		ImGui::TableNextColumn();
		ImGui::BeginDisabled(!shouldDownscale);
		ImGui::SetNextItemWidth(200 * uiScale);
		if (ImGui::InputScalar("Size Limit", ImGuiDataType_U32, (void*)&minDim, &step)) {
			minDim = max(16, (minDim / 16) * 16);
		}
		tooltip("Textures will be downscaled no lower than the limit you set here. Increase for higher texture quality. Decrease to fix more errors."
			"\n\nThis applies only to the largest dimension of the texture. For example, setting 128 "
			"here will allow downscaling a texture from 256x128 to 128x64, and no lower than that.\n\n"
			"For Sven Co-op maps compiled with -subdivide 528:\n"
			"224 fixes extents for texture sizes 512 and up.\n"
			"112 fixes extents for texture sizes 256 and up.\n"
			"48 fixes extents for texture sizes 128 and up.\n"
			"16 fixes as many errors as possible.");
		ImGui::EndDisabled();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("3)");

		ImGui::TableNextColumn();
		ImGui::Checkbox("Scale Faces", &shouldScale);
		tooltip("Scale up faces if both downscaling and subdivsion failed. This ensures every face "
			"has valid extents, but will misalign textures. In some cases that's ok (texture shifting "
			"often doesn't matter for noisy terrain textures).\n");

		ImGui::EndTable();
	}
	ImGui::PopStyleVar();
	ImGui::Dummy(ImVec2(0, 10 * uiScale));

	ImGui::BeginDisabled(!shouldSubdivide && !shouldScale && !shouldDownscale);
	if (ImGui::Button("Apply Fixes")) {
		LumpReplaceCommand* command = new LumpReplaceCommand("Fix Bad Surface Extents");
		if (shouldSubdivide)
			map->fix_all_bad_surface_extents_with_subdivide(subdivideMax);

		if (shouldDownscale)
			map->fix_bad_surface_extents_with_downscale(minDim);

		if (shouldScale)
			map->fix_bad_surface_extents_with_scale();

		command->pushUndoState();
		close();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();

	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		close();
	}
}