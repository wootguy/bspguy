#include "Editor.h"
#include "Gui.h"
#include "Widget.h"
#include "MenuBar.h"
#include "NavRenderer.h"
#include "Entity.h"

void Editor::controlsBegin() {
	for (int i = GLFW_KEY_SPACE; i < GLFW_KEY_LAST; i++) {
		pressed[i] = glfwGetKey(window, i) == GLFW_PRESS;
		released[i] = glfwGetKey(window, i) == GLFW_RELEASE;
	}

	anyCtrlPressed = pressed[GLFW_KEY_LEFT_CONTROL] || pressed[GLFW_KEY_RIGHT_CONTROL];
	anyAltPressed = pressed[GLFW_KEY_LEFT_ALT] || pressed[GLFW_KEY_RIGHT_ALT];
	anyShiftPressed = pressed[GLFW_KEY_LEFT_SHIFT] || pressed[GLFW_KEY_RIGHT_SHIFT];
}

void Editor::controlsEnd() {
	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

	for (int i = GLFW_KEY_SPACE; i < GLFW_KEY_LAST; i++) {
		oldPressed[i] = pressed[i];
		oldReleased[i] = released[i];
	}

	oldScroll = g_scroll;
}

void Editor::viewportControls() {
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	static bool oldWantTextInput = false;
	static bool guiWasFocused = false;

	if (!io.WantCaptureKeyboard && !io.WantCaptureMouse && !guiWasFocused)
		cameraOrigin += getMoveDir() * frameTimeScale;

	moveGrabbedEnts();

	if (!io.WantTextInput && oldWantTextInput) {
		pushEntityUndoState("Edit Keyvalues");
	}

	oldWantTextInput = io.WantTextInput;

	globalShortcutControls();

	if (!io.WantTextInput && !io.WantCaptureMouse && !guiWasFocused) {
		viewportShortcutControls();
		captureMouseControls();
		shortcutControls();
	}
	else if (cameraMouseCapture) {
		captureMouseControls(); // should always be able to disable mouse capture
	}

	if (io.WantTextInput) {
		guiWasFocused = true;
	}

	if (!io.WantCaptureMouse) {
		cameraContextMenus();

		cameraRotationControls();

		makeVectors(cameraAngles, cameraForward, cameraRight, cameraUp);

		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS
			|| glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
			guiWasFocused = false;
		}

		if (!guiWasFocused) {
			cameraObjectHovering();
			vertexEditControls();
			navRenderer->controls();
		}

		cameraPickingControls();
	}
	else if (cameraMouseCapture) {
		cameraRotationControls(); // in case capture point overlaps a gui window
	}
}

void Editor::vertexEditControls() {
	canTransform = true;
	if (transformTarget == TRANSFORM_VERTEX) {
		canTransform = false;
		anyEdgeSelected = false;
		anyVertSelected = false;
		for (int i = 0; i < modelVerts.size(); i++) {
			if (modelVerts[i].selected) {
				canTransform = true;
				anyVertSelected = true;
				break;
			}
		}
		for (int i = 0; i < modelEdges.size(); i++) {
			if (modelEdges[i].selected) {
				canTransform = true;
				anyEdgeSelected = true;
			}
		}
	}

	if (!isTransformableSolid) {
		canTransform = (transformTarget == TRANSFORM_OBJECT || transformTarget == TRANSFORM_ORIGIN) && transformMode == TRANSFORM_MOVE;
	}

	if (pressed[GLFW_KEY_F] && !oldPressed[GLFW_KEY_F])
	{
		if (!anyCtrlPressed) {
			splitFace();
		}
		else {
			gui->widgets[WIDGET_ENT_REPORT]->widgetVisible = !gui->widgets[WIDGET_ENT_REPORT]->widgetVisible;
		}
	}
}

void Editor::cameraPickingControls() {
	static bool transforming;
	static bool clickedInViewport; // fix select from mouse press in imgui then release in viewport
	static bool clickedOnAxes;

	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		if (oldLeftMouse != GLFW_PRESS) {
			clickedInViewport = true;
			clickedOnAxes = hoverAxis != -1;
		}

		transforming = clickedOnAxes ? transformAxisControls() : false;

		int xpos, ypos;
		getMousePos(xpos, ypos);

		if (!isBoxSelecting) {
			isBoxSelecting = true;
			boxSelectStart.x = xpos;
			boxSelectStart.y = ypos;
			boxSelectEnd = boxSelectStart;
		}
		else {
			boxSelectEnd.x = xpos;
			boxSelectEnd.y = ypos;
		}

		bool anyHover = hoverVert != -1 || hoverEdge != -1;
		if (transformTarget == TRANSFORM_VERTEX && isTransformableSolid && anyHover) {
			if (oldLeftMouse != GLFW_PRESS) {
				if (!anyCtrlPressed) {
					for (int i = 0; i < modelEdges.size(); i++) {
						modelEdges[i].selected = false;
					}
					for (int i = 0; i < modelVerts.size(); i++) {
						modelVerts[i].selected = false;
					}
					anyVertSelected = false;
					anyEdgeSelected = false;
				}

				if (hoverVert != -1 && !anyEdgeSelected) {
					modelVerts[hoverVert].selected = !modelVerts[hoverVert].selected;
					anyVertSelected = modelVerts[hoverVert].selected;
				}
				else if (hoverEdge != -1 && !(anyVertSelected && !anyEdgeSelected)) {
					modelEdges[hoverEdge].selected = !modelEdges[hoverEdge].selected;
					for (int i = 0; i < 2; i++) {
						TransformVert& vert = modelVerts[modelEdges[hoverEdge].verts[i]];
						vert.selected = modelEdges[hoverEdge].selected;
					}
					anyEdgeSelected = modelEdges[hoverEdge].selected;
				}

				vertPickCount++;
				applyTransform();
			}

			transforming = true;
		}

		if (transformTarget == TRANSFORM_ORIGIN && originHovered) {
			if (oldLeftMouse != GLFW_PRESS) {
				originSelected = !originSelected;
			}

			transforming = true;
		}
	}
	else { // left mouse not pressed
		if (draggingAxis != -1) {
			draggingAxis = -1;
			applyTransform();
			pushEntityUndoState("Move Entity");
		}

		if (oldLeftMouse == GLFW_PRESS && clickedInViewport && !transforming) {
			applyTransform();

			if (invalidSolid) {
				logf("Reverting invalid solid changes\n");
				for (int i = 0; i < modelVerts.size(); i++) {
					modelVerts[i].pos = modelVerts[i].startPos = modelVerts[i].undoPos;
				}
				for (int i = 0; i < modelFaceVerts.size(); i++) {
					modelFaceVerts[i].pos = modelFaceVerts[i].startPos = modelFaceVerts[i].undoPos;
					if (modelFaceVerts[i].ptr) {
						*modelFaceVerts[i].ptr = modelFaceVerts[i].pos;
					}
				}
				invalidSolid = !pickInfo.getMap()->vertex_manipulation_sync(pickInfo.getModelIndex(), modelVerts, false, true);
				gui->reloadLimits();

				int modelIdx = pickInfo.getModelIndex();
				if (modelIdx >= 0)
					mapRenderer->refreshModel(modelIdx);
			}

			// object picking
			bool bigEnoughBox = (boxSelectEnd - boxSelectStart).length() > 8;
			pickObject(isBoxSelecting && bigEnoughBox);
			pickCount++;
		}

		clickedInViewport = false;
		isBoxSelecting = false;
	}
}


void Editor::cameraRotationControls() {
	static double lastTime = 0;
	double now = glfwGetTime();
	double deltaTime = now - lastTime;
	lastTime = now;
	float ymult = g_settings.invert_y_axis ? -1 : 1;

	vec2 mousePos;
	{
		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);
		mousePos.x = xpos;
		mousePos.y = ypos;
	}

	if (pressed[GLFW_KEY_DOWN]) {
		cameraAngles.x += rotationSpeed * deltaTime * 50 * ymult;
		cameraAngles.x = clamp(cameraAngles.x, -90.0f, 90.0f);
	}
	if (pressed[GLFW_KEY_UP]) {
		cameraAngles.x -= rotationSpeed * deltaTime * 50 * ymult;
		cameraAngles.x = clamp(cameraAngles.x, -90.0f, 90.0f);
	}
	if (pressed[GLFW_KEY_LEFT]) {
		cameraAngles.z -= rotationSpeed * deltaTime * 50;
	}
	if (pressed[GLFW_KEY_RIGHT]) {
		cameraAngles.z += rotationSpeed * deltaTime * 50;
	}

	bool rightMouseHeld = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	bool shouldRotateCam = cameraMouseCapture || rightMouseHeld;

	if (draggingAxis == -1 && shouldRotateCam) {
		if (!cameraIsRotating) {
			lastMousePos = mousePos;
			cameraIsRotating = true;
			totalMouseDrag = vec2();
		}
		else {
			vec2 drag = mousePos - lastMousePos;
			cameraAngles.z += drag.x * rotationSpeed * 0.1f;
			cameraAngles.x += drag.y * rotationSpeed * 0.1f * ymult;

			totalMouseDrag += vec2(fabs(drag.x), fabs(drag.y));

			cameraAngles.x = clamp(cameraAngles.x, -90.0f, 90.0f);
			if (cameraAngles.z > 180.0f) {
				cameraAngles.z -= 360.0f;
			}
			else if (cameraAngles.z < -180.0f) {
				cameraAngles.z += 360.0f;
			}
			lastMousePos = mousePos;

			if (cameraMouseCapture) {
				glfwSetCursorPos(window, windowWidth / 2.0, windowHeight / 2.0);
				double xpos, ypos;
				glfwGetCursorPos(window, &xpos, &ypos);
				lastMousePos.x = xpos;
				lastMousePos.y = ypos;
			}
		}

		ImGui::SetWindowFocus(NULL);
		ImGui::ClearActiveID();
	}
	else {
		cameraIsRotating = false;
		totalMouseDrag = vec2();
	}
}

void Editor::cameraObjectHovering() {
	originHovered = false;

	if (modelUsesSharedStructures && (transformTarget != TRANSFORM_OBJECT || transformMode != TRANSFORM_MOVE))
		return;

	vec3 mapOffset;
	if (pickInfo.getEnt())
		mapOffset = mapRenderer->mapOffset;

	if (transformTarget == TRANSFORM_VERTEX && pickInfo.getEntIndex() > 0) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		float bestDist = FLT_MAX;

		vec3 entOrigin = pickInfo.getOrigin();

		hoverEdge = -1;
		if (!(anyVertSelected && !anyEdgeSelected)) {
			for (int i = 0; i < modelEdges.size(); i++) {
				vec3 ori = getEdgeControlPoint(modelVerts, modelEdges[i]) + entOrigin + mapOffset;
				float s = (ori - cameraOrigin).length() * vertExtentFactor * 2.0f;
				vec3 min = vec3(-s, -s, -s) + ori;
				vec3 max = vec3(s, s, s) + ori;
				if (pickAABB(pickStart, pickDir, min, max, bestDist)) {
					hoverEdge = i;
				}
			}
		}

		hoverVert = -1;
		if (!anyEdgeSelected) {
			for (int i = 0; i < modelVerts.size(); i++) {
				vec3 ori = entOrigin + modelVerts[i].pos + mapOffset;
				float s = (ori - cameraOrigin).length() * vertExtentFactor * 2.0f;
				vec3 min = vec3(-s, -s, -s) + ori;
				vec3 max = vec3(s, s, s) + ori;
				if (pickAABB(pickStart, pickDir, min, max, bestDist)) {
					hoverVert = i;
				}
			}
		}
	}

	if (transformTarget == TRANSFORM_ORIGIN && pickInfo.getModelIndex() > 0) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		float bestDist = FLT_MAX;

		vec3 ori = transformedOrigin + mapOffset;
		float s = (ori - cameraOrigin).length() * vertExtentFactor * 2.0f;
		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		originHovered = pickAABB(pickStart, pickDir, min, max, bestDist);
	}

	if (transformTarget == TRANSFORM_VERTEX && transformMode == TRANSFORM_SCALE)
		return; // 3D scaling disabled in vertex edit mode

	// axis handle hovering
	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);
	hoverAxis = -1;
	if (showDragAxes && !movingEnt && hoverVert == -1 && hoverEdge == -1) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		float bestDist = FLT_MAX;

		bool shouldOffset = false;
		for (Entity* ent : pickInfo.getEnts()) {
			shouldOffset = ent->shouldDisplayDirectionVector();
			break;
		}

		vec3 offset = shouldOffset && transformMode == TRANSFORM_MOVE ? vec3(0, 0, 64) : vec3();
		pickStart -= offset;

		Bsp* map = mapRenderer->map;
		vec3 origin = activeAxes.origin + mapRenderer->mapOffset;

		int axisChecks = transformMode == TRANSFORM_SCALE ? activeAxes.numAxes : 3;
		for (int i = 0; i < axisChecks; i++) {
			if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[i], origin + activeAxes.maxs[i], bestDist)) {
				hoverAxis = i;
			}
		}

		// center cube gets priority for selection (hard to select from some angles otherwise)
		if (transformMode == TRANSFORM_MOVE) {
			float bestDist = FLT_MAX;
			if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[3], origin + activeAxes.maxs[3], bestDist)) {
				hoverAxis = 3;
			}
		}
	}
}

void Editor::cameraContextMenus() {
	// context menus
	bool wasTurning = cameraIsRotating && totalMouseDrag.length() >= 1;
	if (draggingAxis == -1 && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE && oldRightMouse != GLFW_RELEASE && !wasTurning) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);

		int entIdx, faceIdx, leafIdx;
		float bestDist = FLT_MAX;
		mapRenderer->pickPoly(pickStart, pickDir, clipnodeRenderHull, entIdx, faceIdx, leafIdx, bestDist);

		if (entIdx != 0 && pickInfo.isEntSelected(entIdx)) {
			gui->openContextMenu(pickInfo.getEntIndex());
		}
		else {
			gui->openContextMenu(-1);
		}
	}
}


void Editor::shortcutControls() {
	ImGuiIO& io = ImGui::GetIO();

	if (pickMode == PICK_OBJECT) {
		bool anyEnterPressed = (pressed[GLFW_KEY_ENTER] && !oldPressed[GLFW_KEY_ENTER]) ||
			(pressed[GLFW_KEY_KP_ENTER] && !oldPressed[GLFW_KEY_KP_ENTER]);

		if (pressed[GLFW_KEY_G] == GLFW_PRESS && oldPressed[GLFW_KEY_G] != GLFW_PRESS) {
			if (!movingEnt)
				grabEnts();
			else {
				ungrabEnts();
			}
		}
		if (pressed[GLFW_KEY_H] && !oldPressed[GLFW_KEY_H]) {
			bool shouldHide = pickInfo.shouldHideSelection();

			if (shouldHide) {
				hideSelectedEnts();
			}
			else {
				unhideSelectedEnts();
			}
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_C] && !oldPressed[GLFW_KEY_C]) {
			copyEnts(false);
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_X] && !oldPressed[GLFW_KEY_X]) {
			cutEnts();
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_V] && !oldPressed[GLFW_KEY_V]) {
			if (isLoading) {
				logf("Can't paste while map is loading!\n");
			}
			else {
				pasteEnts(false);
			}
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_M] && !oldPressed[GLFW_KEY_M]) {
			gui->widgets[WIDGET_TRANSFORM]->widgetVisible = !gui->widgets[WIDGET_TRANSFORM]->widgetVisible;
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_O] && !oldPressed[GLFW_KEY_O]) {
			openMap((char*)NULL);
		}
		if (anyCtrlPressed && anyAltPressed && pressed[GLFW_KEY_S] && !oldPressed[GLFW_KEY_S]) {
			gui->menuBar->saveAs();
		}
		if (anyAltPressed && anyEnterPressed) {
			gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible = !gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible;
		}
		if (pressed[GLFW_KEY_DELETE] && !oldPressed[GLFW_KEY_DELETE]) {
			deleteEnts();
		}
	}
	else if (pickMode == PICK_FACE) {
		if (anyCtrlPressed && pressed[GLFW_KEY_C] && !oldPressed[GLFW_KEY_C]) {
			gui->copyTexture();
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_V] && !oldPressed[GLFW_KEY_V]) {
			gui->pasteTexture();
		}
		if (pressed[GLFW_KEY_H] && !oldPressed[GLFW_KEY_H]) {
			hideSelectedFaces();
		}
	}
	else if (pickMode == PICK_LEAF) {
		if (pressed[GLFW_KEY_H] && !oldPressed[GLFW_KEY_H]) {
			hideSelectedLeaves();
		}
		if (pressed[GLFW_KEY_P] && !oldPressed[GLFW_KEY_P]) {
			gui->selectLeafPvs();
		}
	}
}

void Editor::globalShortcutControls() {
	if (pressed[GLFW_KEY_F10] && !oldPressed[GLFW_KEY_F10]) {
		g_settings.fullscreen = !g_settings.fullscreen;
		toggleFullscreen();
	}
}

void Editor::viewportShortcutControls() {
	if (anyCtrlPressed && pressed[GLFW_KEY_Z] && !oldPressed[GLFW_KEY_Z]) {
		undo();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_Y] && !oldPressed[GLFW_KEY_Y]) {
		redo();
	}

	static bool oldPreview = previewMode;
	previewMode = pressed[GLFW_KEY_R];

	if (previewMode != oldPreview) {
		mapRenderer->reloadMegaBuffers();
		if (!previewMode) {
			for (Entity* ent : mapRenderer->map->ents)
				ent->didStudioDraw = false; // fix ents disappearing when models are disabled
		}
	}
	oldPreview = previewMode;
}

void Editor::captureMouseControls() {
	if (!anyCtrlPressed && pressed[GLFW_KEY_Z] && !oldPressed[GLFW_KEY_Z]) {
		cameraMouseCapture = !cameraMouseCapture;

		ImGuiIO& io = ImGui::GetIO(); (void)io;
		if (cameraMouseCapture) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		}
		else {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
		}
	}
}


bool Editor::transformAxisControls() {

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	if (!canTransform || pickInfo.getEntIndex() < 0) {
		return false;
	}

	// axis handle dragging
	if (showDragAxes && !movingEnt && hoverAxis != -1 && draggingAxis == -1) {
		draggingAxis = hoverAxis;

		Bsp* map = mapRenderer->map;

		axisDragEntOriginStart.clear();
		for (int i = 0; i < pickInfo.ents.size(); i++) {
			Entity* ent = map->ents[pickInfo.ents[i]];
			vec3 ori = getEntOrigin(map, ent);
			axisDragEntOriginStart.push_back(ori);
		}

		axisDragStart = getAxisDragPoint(axisDragEntOriginStart[0]);
	}

	if (showDragAxes && !movingEnt && draggingAxis >= 0) {
		Bsp* map = pickInfo.getMap();

		activeAxes.model[draggingAxis].setColor(activeAxes.hoverColor[draggingAxis]);

		vec3 dragPoint = getAxisDragPoint(axisDragEntOriginStart[0]);
		if (gridSnappingEnabled) {
			dragPoint = snapToGrid(dragPoint);
		}
		vec3 delta = dragPoint - axisDragStart;


		float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 2.0f : 1.0f;
		if (pressed[GLFW_KEY_LEFT_CONTROL] == GLFW_PRESS)
			moveScale = 0.1f;

		float maxDragDist = 8192; // don't throw ents out to infinity
		for (int i = 0; i < 3; i++) {
			if (i != draggingAxis % 3)
				((float*)&delta)[i] = 0;
			else
				((float*)&delta)[i] = clamp(((float*)&delta)[i] * moveScale, -maxDragDist, maxDragDist);
		}

		if (transformMode == TRANSFORM_MOVE) {
			if (transformTarget == TRANSFORM_VERTEX) {
				moveSelectedVerts(delta);
			}
			else if (transformTarget == TRANSFORM_OBJECT) {
				for (int i = 0; i < pickInfo.ents.size(); i++) {
					int entidx = pickInfo.ents[i];
					Entity* ent = map->ents[entidx];
					vec3 offset = getEntOffset(map, ent);
					vec3 newOrigin = (axisDragEntOriginStart[i] + delta) - offset;
					vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

					ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
					mapRenderer->refreshEnt(entidx);
				}
				updateEntConnectionPositions();
			}
			else if (transformTarget == TRANSFORM_ORIGIN) {
				transformedOrigin = (oldOrigin + delta);
				transformedOrigin = gridSnappingEnabled ? snapToGrid(transformedOrigin) : transformedOrigin;

				//mapRenderers[pickInfo.mapIdx]->refreshEnt(pickInfo.entIdx);
			}

		}
		else {
			Entity* ent = pickInfo.getEnt();
			if (ent->isBspModel() && delta.length() != 0) {

				vec3 scaleDirs[6]{
					vec3(1, 0, 0),
					vec3(0, 1, 0),
					vec3(0, 0, 1),
					vec3(-1, 0, 0),
					vec3(0, -1, 0),
					vec3(0, 0, -1),
				};

				scaleSelectedObject(delta, scaleDirs[draggingAxis]);
				mapRenderer->refreshModel(ent->getBspModelIdx());
			}
		}

		return true;
	}

	return false;
}

void Editor::getMousePos(int& x, int& y) {
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	if (viewportFbo) {
		x = xpos * viewportScale;
		y = ypos * viewportScale;
	}
	else {
		x = xpos;
		y = ypos;
	}
}
