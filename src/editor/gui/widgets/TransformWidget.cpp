#include "Widget.h"
#include "Entity.h"

void TransformWidget::draw() {
	bool transformingEnt = app->pickInfo.getEntIndex() >= 0;

	BspRenderer* bspRenderer = app->mapRenderer;

	static int x, y, z; // grid snapped origin
	static float fx, fy, fz; // raw origin
	static float last_fx, last_fy, last_fz;
	static float sx, sy, sz; // scaling
	static float frx, fry, frz; // raw rotation
	static int rx, ry, rz; // grid snapped rotation

	static int lastPickCount = -1;
	static int lastVertPickCount = -1;
	static int oldSnappingEnabled = app->gridSnappingEnabled;
	static int oldTransformTarget = -1;
	static int oldMultiselect;
	static vector<vec3> multiselectOrigins; // reference point for multiselect transforms
	static vector<vec3> multiselectAngles; // reference point for multiselect transforms

	ImGuiStyle& style = ImGui::GetStyle();

	int multiSelect = app->pickInfo.ents.size();

	bool shouldUpdateUi = lastPickCount != app->pickCount ||
		app->draggingAxis != -1 ||
		app->movingEnt ||
		oldSnappingEnabled != app->gridSnappingEnabled ||
		lastVertPickCount != app->vertPickCount ||
		oldTransformTarget != app->transformTarget ||
		oldMultiselect != multiSelect ||
		g_app->forceRefreshTransformWindow;

	TransformAxes& activeAxes = *(app->transformMode == TRANSFORM_SCALE ? &app->scaleAxes : &app->moveAxes);

	if (shouldUpdateUi) {
		if (transformingEnt) {
			if (app->transformTarget == TRANSFORM_VERTEX) {
				x = fx = last_fx = activeAxes.origin.x;
				y = fy = last_fy = activeAxes.origin.y;
				z = fz = last_fz = activeAxes.origin.z;
			}
			else {
				if (multiSelect > 1) {
					if (multiSelect != oldMultiselect || lastPickCount != app->pickCount || g_app->forceRefreshTransformWindow) {
						multiselectOrigins.clear();
						multiselectAngles.clear();
						for (int i = 0; i < app->pickInfo.ents.size(); i++) {
							Entity* ent = map->ents[app->pickInfo.ents[i]];
							vec3 ori = ent->getOrigin();
							vec3 angles = ent->getAngles();
							multiselectOrigins.push_back(ori);
							multiselectAngles.push_back(angles);
						}
						frx = rx = x = fx = 0;
						fry = ry = y = fy = 0;
						frz = rz = z = fz = 0;
					}
				}
				else {
					Entity* ent = app->pickInfo.getEnt();
					vec3 ori = ent->getOrigin();
					vec3 angles = ent->getAngles();
					if (app->originSelected) {
						ori = app->transformedOrigin;
					}
					x = fx = ori.x;
					y = fy = ori.y;
					z = fz = ori.z;
					frx = rx = angles.x;
					fry = ry = angles.y;
					frz = rz = angles.z;
				}
			}

		}
		else {
			frx = rx = x = fx = 0;
			fry = ry = y = fy = 0;
			frz = rz = z = fz = 0;
		}
		sx = sy = sz = 1;

		g_app->forceRefreshTransformWindow = false;
	}

	oldMultiselect = multiSelect;
	oldTransformTarget = app->transformTarget;
	oldSnappingEnabled = app->gridSnappingEnabled;
	lastVertPickCount = app->vertPickCount;
	lastPickCount = app->pickCount;

	bool scaled = false;
	bool originChanged = false;
	bool anglesChanged = false;
	gui->guiHoverAxis = -1;

	float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
	float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
	float inputWidth4 = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.25f;

	static bool inputsWereDragged = false;
	bool inputsAreDragging = false;
	bool canEditBspModel = app->pickInfo.getModel() && !app->modelUsesSharedStructures && app->isTransformableSolid;

	if (!canEditBspModel)
		app->transformTarget = TRANSFORM_OBJECT;

	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(roundf(2.0f * uiScale), roundf(2.0f * uiScale)));
	if (ImGui::BeginTable("TransformTable", 5, ImGuiTableFlags_SizingFixedFit)) {
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, uiScale * 60);
		ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("pad", ImGuiTableColumnFlags_WidthFixed, uiScale * 5);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		ImGui::BeginDisabled(app->pickInfo.ents.empty());
		ImGui::Text("Move");
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (app->gridSnappingEnabled) {
			if (ImGui::DragInt("##xpos", &x, 0.1f, 0, 0, "X: %d")) { originChanged = true; }
		}
		else {
			if (ImGui::DragFloat("##xpos2", &fx, 0.1f, 0, 0, "X: %.2f")) { originChanged = true; }
		}
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			gui->guiHoverAxis = 0;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (app->gridSnappingEnabled) {
			if (ImGui::DragInt("##ypos", &y, 0.1f, 0, 0, "Y: %d")) { originChanged = true; }
		}
		else {
			if (ImGui::DragFloat("##ypos2", &fy, 0.1f, 0, 0, "Y: %.2f")) { originChanged = true; }
		}
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			gui->guiHoverAxis = 1;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (app->gridSnappingEnabled) {
			if (ImGui::DragInt("##zpos", &z, 0.1f, 0, 0, "Z: %d")) { originChanged = true; }
		}
		else {
			if (ImGui::DragFloat("##zpos2", &fz, 0.1f, 0, 0, "Z: %.2f")) { originChanged = true; }
		}
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			gui->guiHoverAxis = 2;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::TableNextColumn();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::EndDisabled();

		ImGui::BeginDisabled(app->transformTarget != TRANSFORM_OBJECT || g_app->pickInfo.getEntIndex() == 0 || app->pickInfo.ents.empty());
		ImGui::Text("Rotate");
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (app->gridSnappingEnabled) {
			if (ImGui::DragInt("##xrot", &rx, 0.1f, 0, 0, "X: %d")) {
				rx = rx > 0 ? (rx % 360) : -(-rx % 360);
				anglesChanged = true;
			}
		}
		else {
			if (ImGui::DragFloat("##xrot2", &frx, 0.1f, 0, 0, "X: %.2f")) {
				frx = normalizeRangef(frx, -360, 360);
				anglesChanged = true;
			}
		}
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (app->gridSnappingEnabled) {
			if (ImGui::DragInt("##yrot", &ry, 0.1f, 0, 0, "Y: %d")) {
				ry = ry > 0 ? (ry % 360) : -(-ry % 360);
				anglesChanged = true;
			}
		}
		else {
			if (ImGui::DragFloat("##yrot2", &fry, 0.1f, 0, 0, "Y: %.2f")) {
				fry = normalizeRangef(fry, -360, 360);
				anglesChanged = true;
			}
		}
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (app->gridSnappingEnabled) {
			if (ImGui::DragInt("##zrot", &rz, 0.1f, 0, 0, "Z: %d")) {
				rz = rz > 0 ? (rz % 360) : -(-rz % 360);
				anglesChanged = true;
			}
		}
		else {
			if (ImGui::DragFloat("##zrot2", &frz, 0.1f, 0, 0, "Z: %.2f")) {
				frz = normalizeRangef(frz, -360, 360);
				anglesChanged = true;
			}
		}
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::EndDisabled();

		ImGui::TableNextColumn();
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		ImGui::BeginDisabled(!canEditBspModel || app->transformTarget != TRANSFORM_OBJECT || app->pickInfo.ents.empty());
		ImGui::Text("Scale");
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(-FLT_MIN);

		if (ImGui::DragFloat("##xscale", &sx, 0.002f, 0, 0, "X: %.3f")) { scaled = true; }
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			gui->guiHoverAxis = 0;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##yscale", &sy, 0.002f, 0, 0, "Y: %.3f")) { scaled = true; }
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			gui->guiHoverAxis = 1;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##zscale", &sz, 0.002f, 0, 0, "Z: %.3f")) { scaled = true; }
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			gui->guiHoverAxis = 2;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::EndDisabled();

		ImGui::EndTable();
	}
	ImGui::PopStyleVar();
	//ImGui::Columns(1);
	//ImGui::PopStyleVar();

	if (inputsWereDragged && !inputsAreDragging) {
		int plural = app->pickInfo.ents.size() > 1;
		app->pushEntityUndoState(plural ? "Transform Entities" : "Transform Entity");

		if (transformingEnt) {
			app->applyTransform(true);

			if (app->gridSnappingEnabled) {
				fx = last_fx = x;
				fy = last_fy = y;
				fz = last_fz = z;
			}
			else {
				x = last_fx = fx;
				y = last_fy = fy;
				z = last_fz = fz;
			}

			sx = sy = sz = 1;
		}
	}

	ImGui::Dummy(ImVec2(0, style.FramePadding.y));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));

	const int grid_snap_modes = 11;
	const char* element_names[grid_snap_modes] = { "0", "1", "2", "4", "8", "16", "32", "64", "128", "256", "512" };
	static int current_element = app->gridSnapLevel + 1;

	ImGui::Columns(2, 0, false);
	ImGui::SetColumnWidth(0, inputWidth4);
	ImGui::SetColumnWidth(1, inputWidth4 * 3);
	ImGui::Text("Grid Snap:"); ImGui::NextColumn();
	ImGui::SetNextItemWidth(inputWidth4 * 3);
	if (ImGui::SliderInt("##gridsnap", &current_element, 0, grid_snap_modes - 1, element_names[current_element])) {
		app->gridSnapLevel = current_element - 1;
		app->gridSnappingEnabled = current_element != 0;
		originChanged = true;
	}
	ImGui::Columns(1);

	ImGui::Columns(4, 0, false);
	ImGui::SetColumnWidth(0, inputWidth4);
	ImGui::SetColumnWidth(1, inputWidth4);
	ImGui::SetColumnWidth(2, inputWidth4);
	ImGui::SetColumnWidth(3, inputWidth4);
	ImGui::AlignTextToFramePadding();
	ImGui::Text("Target: "); ImGui::NextColumn();

	ImGui::RadioButton("Entity", &app->transformTarget, TRANSFORM_OBJECT); ImGui::NextColumn();
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Apply transformation to an entity origin and angles keyvalues.");
	}

	ImGui::BeginDisabled(!canEditBspModel);
	ImGui::RadioButton("Vertex", &app->transformTarget, TRANSFORM_VERTEX); ImGui::NextColumn();
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Apply transformation to BSP model vertices.");
	}

	ImGui::RadioButton("Origin", &app->transformTarget, TRANSFORM_ORIGIN); ImGui::NextColumn();
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Apply transformation to a BSP model's origin (not the origin keyvalue).\nOrigins are used as a point of reference for rotation.");
	}

	ImGui::Text("3D Axes: "); ImGui::NextColumn();
	if (ImGui::RadioButton("Hide", &app->transformMode, TRANSFORM_NONE))
		app->showDragAxes = false;

	ImGui::NextColumn();
	if (ImGui::RadioButton("Move", &app->transformMode, TRANSFORM_MOVE))
		app->showDragAxes = true;

	ImGui::NextColumn();
	ImGui::BeginDisabled(!canEditBspModel || app->transformTarget != TRANSFORM_OBJECT);
	if (ImGui::RadioButton("Scale", &app->transformMode, TRANSFORM_SCALE))
		app->showDragAxes = true;
	ImGui::EndDisabled();
	ImGui::NextColumn();

	ImGui::Columns(2, "checkboxes", false);

	if (ImGui::Checkbox("Force Rotate", &app->forceAngleRotation)) {
		app->updateEntConnectionPositions();
		app->mapRenderer->reloadMegaBuffers();

		for (int i = 0; i < map->ents.size(); i++) {
			Entity* ent = map->ents[i];
			if (ent->getBspModelIdx() != -1)
				bspRenderer->refreshEnt(i);
		}
	}
	ImGui::NextColumn();
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Force solid entities to rotate by their angles keyvalue, even if they may not appear rotated in-game.\nPoint entities that don't use angles will show directional vectors.\n\nBy default, the program checks the entity class and FGDs to decide if an entity should appear rotated or display vectors.");
	}

	ImGui::PushItemWidth(inputWidth);
	ImGui::BeginDisabled(!canEditBspModel);
	ImGui::Checkbox("Texture lock", &app->textureLock);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("(WIP)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted("Stretches/compresses textures to fit the object while scaling.\nDoes not work with angled faces.");
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();

	ImGui::Columns(1);

	ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
	string w = to_string((int)app->selectionSize.x) + "w ";
	string h = to_string((int)app->selectionSize.y) + "h ";
	string l = to_string((int)app->selectionSize.z) + "l";
	ImGui::Text(("Size: " + w + h + l).c_str());

	if (app->pickInfo.getEntIndex() == 0 && map->ents[0]->getOrigin() != vec3()) {
		ImGui::SameLine();
		const char* butLabel = "Apply BSP Move";
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (ImGui::CalcTextSize(butLabel).x + ImGui::GetStyle().ItemSpacing.x + 30));
		if (ImGui::Button(butLabel)) {
			vec3 moveAmount = map->ents[0]->getOrigin();
			map->ents[0]->removeKeyvalue("origin");
			LumpReplaceCommand* command = new LumpReplaceCommand("Apply Worldspawn Transform");

			map->move(moveAmount);
			map->zero_entity_origins("func_ladder");
			map->zero_entity_origins("func_water"); // water is sometimes invisible after moving in sven
			map->zero_entity_origins("func_mortar_field"); // mortars don't appear in sven

			command->pushUndoState();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Moves all BSP data by the amount set in the worldspawn origin keyvalue.\n"
				"Useful for aligning maps before merging and moving areas inside the map boundaries.");
		}
	}

	if (transformingEnt) {
		if (originChanged) {
			if (app->transformTarget == TRANSFORM_VERTEX) {
				vec3 delta;
				if (app->gridSnappingEnabled) {
					delta = vec3(x - last_fx, y - last_fy, z - last_fz);
				}
				else {
					delta = vec3(fx - last_fx, fy - last_fy, fz - last_fz);
				}

				app->moveSelectedVerts(delta);
			}
			else if (app->transformTarget == TRANSFORM_OBJECT) {
				vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
				newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

				if (app->gridSnappingEnabled) {
					fx = x;
					fy = y;
					fz = z;
				}
				else {
					x = fx;
					y = fy;
					z = fz;
				}

				if (multiSelect > 1) {
					for (int i = 0; i < app->pickInfo.ents.size(); i++) {
						int entidx = app->pickInfo.ents[i];
						Entity* ent = map->ents[entidx];
						vec3 ori = multiselectOrigins[i] + newOrigin;
						ori = app->gridSnappingEnabled ? app->snapToGrid(ori) : ori;
						ent->setOrAddKeyvalue("origin", ori.toKeyvalueString(!app->gridSnappingEnabled));
						bspRenderer->refreshEnt(entidx);
					}
					app->updateEntConnectionPositions();
				}
				else {
					Entity* ent = app->pickInfo.getEnt();
					ent->setOrAddKeyvalue("origin", newOrigin.toKeyvalueString(!app->gridSnappingEnabled));
					bspRenderer->refreshEnt(app->pickInfo.getEntIndex());
					app->updateEntConnectionPositions();
				}
			}
			else if (app->transformTarget == TRANSFORM_ORIGIN) {
				vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
				newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

				app->transformedOrigin = newOrigin;
			}
		}
		if (scaled && app->pickInfo.getEnt()->isBspModel() && app->isTransformableSolid && !app->modelUsesSharedStructures) {
			if (app->transformTarget == TRANSFORM_VERTEX) {
				app->scaleSelectedVerts(sx, sy, sz);
			}
			else if (app->transformTarget == TRANSFORM_OBJECT) {
				int modelIdx = app->pickInfo.getModelIndex();
				app->scaleSelectedObject(sx, sy, sz);
				app->mapRenderer->refreshModel(app->pickInfo.getModelIndex());
			}
			else if (app->transformTarget == TRANSFORM_ORIGIN) {
				logf("Scaling has no effect on origins\n");
			}
		}
		if (anglesChanged) {
			if (app->transformTarget == TRANSFORM_OBJECT) {
				vec3 newAngles = app->gridSnappingEnabled ? vec3(rx, ry, rz) : vec3(frx, fry, frz);

				if (multiSelect > 1) {
					for (int i = 0; i < app->pickInfo.ents.size(); i++) {
						int entidx = app->pickInfo.ents[i];
						Entity* ent = map->ents[entidx];
						vec3 angles = multiselectAngles[i] + newAngles;
						ent->setOrAddKeyvalue("angles", angles.toKeyvalueString(true));
						bspRenderer->refreshEnt(entidx);
					}
					app->updateEntConnectionPositions();
					app->updateEntDirectionVectors();
				}
				else {
					Entity* ent = app->pickInfo.getEnt();
					ent->setOrAddKeyvalue("angles", newAngles.toKeyvalueString(true));
					bspRenderer->refreshEnt(app->pickInfo.getEntIndex());
					if (ent->getBspModelIdx() != -1)
						app->updateEntConnectionPositions();
					else
						app->updateEntDirectionVectors();
				}
			}
		}
	}

	inputsWereDragged = inputsAreDragging;
}
