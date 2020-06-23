#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Gui.h"
#include <algorithm>
#include <map>

void error_callback(int error, const char* description)
{
	logf("GLFW Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int g_scroll = 0;

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	g_scroll += round(yoffset);
}

Renderer::Renderer() {
	if (!glfwInit())
	{
		logf("GLFW initialization failed\n");
		return;
	}

	glfwSetErrorCallback(error_callback);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	window = glfwCreateWindow(1920, 1080, "bspguy", NULL, NULL);
	if (!window)
	{
		logf("Window creation failed\n");
		return;
	}

	glfwMakeContextCurrent(window);
	glfwSetKeyCallback(window, key_callback);
	glfwSetScrollCallback(window, scroll_callback);

	glewInit();

	gui = new Gui(this);

	bspShader = new ShaderProgram(g_shader_multitexture_vertex, g_shader_multitexture_fragment);
	bspShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	bspShader->setMatrixNames(NULL, "modelViewProjection");

	colorShader = new ShaderProgram(g_shader_cVert_vertex, g_shader_cVert_fragment);
	colorShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	colorShader->setMatrixNames(NULL, "modelViewProjection");
	colorShader->setVertexAttributeNames("vPosition", "vColor", NULL);

	g_render_flags = RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_SPECIAL 
		| RENDER_ENTS | RENDER_SPECIAL_ENTS | RENDER_POINT_ENTS | RENDER_WIREFRAME;
	
	pickInfo.valid = false;

	fgd = new Fgd(g_game_path + "/svencoop/sven-coop.fgd");
	fgd->parse();

	pointEntRenderer = new PointEntRenderer(fgd, colorShader);

	movingEnt = false;
	draggingAxis = -1;
	showDragAxes = true;
	gridSnapLevel = 0;
	gridSnappingEnabled = true;
	textureLock = false;
	transformMode = TRANSFORM_MOVE;
	//transformTarget = TRANSFORM_OBJECT;
	transformTarget = TRANSFORM_VERTEX;

	copiedEnt = NULL;

	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

	cameraOrigin = vec3(-70, 343, 19);
	cameraAngles = vec3(22, 0, 90);
}

Renderer::~Renderer() {
	glfwTerminate();
}

void Renderer::renderLoop() {
	glfwSwapInterval(1);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);

	{
		moveAxes.dimColor[0] = { 110, 0, 160 };
		moveAxes.dimColor[1] = { 0, 0, 220 };
		moveAxes.dimColor[2] = { 0, 160, 0 };
		moveAxes.dimColor[3] = { 160, 160, 160 };

		moveAxes.hoverColor[0] = { 128, 64, 255 };
		moveAxes.hoverColor[1] = { 64, 64, 255 };
		moveAxes.hoverColor[2] = { 64, 255, 64 };
		moveAxes.hoverColor[3] = { 255, 255, 255 };

		// flipped for HL coords
		moveAxes.model = new cCube[4];
		moveAxes.buffer = new VertexBuffer(colorShader, COLOR_3B | POS_3F, moveAxes.model, 6 * 6 * 4);
		moveAxes.numAxes = 4;
	}

	{
		scaleAxes.dimColor[0] = { 110, 0, 160 };
		scaleAxes.dimColor[1] = { 0, 0, 220 };
		scaleAxes.dimColor[2] = { 0, 160, 0 };

		scaleAxes.dimColor[3] = { 110, 0, 160 };
		scaleAxes.dimColor[4] = { 0, 0, 220 };
		scaleAxes.dimColor[5] = { 0, 160, 0 };

		scaleAxes.hoverColor[0] = { 128, 64, 255 };
		scaleAxes.hoverColor[1] = { 64, 64, 255 };
		scaleAxes.hoverColor[2] = { 64, 255, 64 };

		scaleAxes.hoverColor[3] = { 128, 64, 255 };		
		scaleAxes.hoverColor[4] = { 64, 64, 255 };
		scaleAxes.hoverColor[5] = { 64, 255, 64 };

		// flipped for HL coords
		scaleAxes.model = new cCube[6];
		scaleAxes.buffer = new VertexBuffer(colorShader, COLOR_3B | POS_3F, scaleAxes.model, 6 * 6 * 6);
		scaleAxes.numAxes = 6;
	}

	updateDragAxes();

	float s = 1.0f;
	cCube vertCube(vec3(-s, -s, -s), vec3(s, s, s), { 0, 128, 255 });
	VertexBuffer vertCubeBuffer(colorShader, COLOR_3B | POS_3F, &vertCube, 6 * 6);

	float lastFrameTime = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float frameDelta = glfwGetTime() - lastFrameTime;
		frameTimeScale = 0.05f / frameDelta;
		float fps = 1.0f / frameDelta;
		
		frameTimeScale = 144.0f / fps;

		lastFrameTime = glfwGetTime();

		controls();

		float spin = glfwGetTime() * 2;
		model.loadIdentity();
		model.rotateZ(spin);
		model.rotateX(spin);
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		setupView();
		bspShader->bind();
		glEnable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);

		for (int i = 0; i < mapRenderers.size(); i++) {
			model.loadIdentity();
			bspShader->updateMatrixes();

			int highlightEnt = -1;
			if (pickInfo.valid && pickInfo.mapIdx == i) {
				highlightEnt = pickInfo.entIdx;
			}
			mapRenderers[i]->render(highlightEnt, transformTarget == TRANSFORM_VERTEX);
		}

		model.loadIdentity();
		colorShader->bind();

		if (true) {
			if (debugClipnodes && pickInfo.valid && pickInfo.modelIdx > 0) {
				BSPMODEL& pickModel = pickInfo.map->models[pickInfo.modelIdx];
				glDisable(GL_CULL_FACE);
				int currentPlane = 0;
				drawClipnodes(pickInfo.map, pickModel.iHeadnodes[1], currentPlane, debugInt);
				debugIntMax = currentPlane-1;
				glEnable(GL_CULL_FACE);
			}

			colorShader->bind();
			model.loadIdentity();
			colorShader->updateMatrixes();
			drawLine(debugPoint - vec3(32, 0, 0), debugPoint + vec3(32, 0, 0), { 128, 128, 255 });
			drawLine(debugPoint - vec3(0, 32, 0), debugPoint + vec3(0, 32, 0), { 0, 255, 0 });
			drawLine(debugPoint - vec3(0, 0, 32), debugPoint + vec3(0, 0, 32), { 0, 0, 255 });
		}

		if (showDragAxes && !movingEnt && pickInfo.valid && pickInfo.entIdx > 0) {
			drawTransformAxes();
		}

		if (pickInfo.valid && pickInfo.modelIdx > 0 && transformTarget == TRANSFORM_VERTEX && isTransformableSolid) {
			drawModelVerts();
		}

		vec3 forward, right, up;
		makeVectors(cameraAngles, forward, right, up);
		//logf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

		gui->draw();

		glfwSwapBuffers(window);
	}

	glfwTerminate();
}

void Renderer::drawModelVerts() {	
	glClear(GL_DEPTH_BUFFER_BIT);

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	Entity* ent = map->ents[pickInfo.entIdx];	

	const COLOR3 vertDimColor = { 200, 200, 200 };
	const COLOR3 vertHoverColor = { 255, 255, 255 };
	const COLOR3 edgeDimColor = { 255, 128, 0 };
	const COLOR3 edgeHoverColor = { 255, 255, 0 };
	const COLOR3 selectColor = { 0, 128, 255 };
	const COLOR3 hoverSelectColor = { 96, 200, 255 };
	vec3 entOrigin = ent->getOrigin();

	int cubeIdx = 0;
	for (int i = 0; i < modelVerts.size(); i++) {
		vec3 ori = modelVerts[i].pos + entOrigin;
		float s = (ori - cameraOrigin).length() * vertExtentFactor;
		ori = ori.flip();

		if (anyEdgeSelected) {
			s = 0; // can't select certs when edges are selected
		}

		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		COLOR3 color;
		if (modelVerts[i].selected) {
			color = i == hoverVert ? hoverSelectColor : selectColor;
		}
		else {
			color = i == hoverVert ? vertHoverColor : vertDimColor;
		}
		modelVertCubes[cubeIdx++] = cCube(min, max, color);
	}

	for (int i = 0; i < modelEdges.size(); i++) {
		vec3 ori = getEdgeControlPoint(modelEdges[i]) + entOrigin;
		float s = (ori - cameraOrigin).length() * vertExtentFactor;
		ori = ori.flip();

		if (anyVertSelected && !anyEdgeSelected) {
			s = 0; // can't select edges when verts are selected
		}

		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		COLOR3 color;
		if (modelEdges[i].selected) {
			color = i == hoverEdge ? hoverSelectColor : selectColor;
		}
		else {
			color = i == hoverEdge ? edgeHoverColor : edgeDimColor;
		}
		modelVertCubes[cubeIdx++] = cCube(min, max, color);
	}

	model.loadIdentity();
	colorShader->updateMatrixes();
	modelVertBuff->draw(GL_TRIANGLES);
}

void Renderer::drawTransformAxes() {
	if (!canTransform) {
		return;
	}

	glClear(GL_DEPTH_BUFFER_BIT);
	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	Entity* ent = map->ents[pickInfo.entIdx];

	updateDragAxes();

	glDisable(GL_CULL_FACE);

	if (transformMode == TRANSFORM_SCALE && transformTarget == TRANSFORM_OBJECT) {
		vec3 ori = scaleAxes.origin;
		model.translate(ori.x, ori.z, -ori.y);
		colorShader->updateMatrixes();
		scaleAxes.buffer->draw(GL_TRIANGLES);
	}
	if (transformMode == TRANSFORM_MOVE) {
		vec3 ori = moveAxes.origin;
		model.translate(ori.x, ori.z, -ori.y);
		colorShader->updateMatrixes();
		moveAxes.buffer->draw(GL_TRIANGLES);
	}
}

void Renderer::controls() {
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	for (int i = GLFW_KEY_SPACE; i < GLFW_KEY_LAST; i++) {
		pressed[i] = glfwGetKey(window, i) == GLFW_PRESS;
		released[i] = glfwGetKey(window, i) == GLFW_RELEASE;
	}

	anyCtrlPressed = pressed[GLFW_KEY_LEFT_CONTROL] || pressed[GLFW_KEY_RIGHT_CONTROL];
	anyAltPressed = pressed[GLFW_KEY_LEFT_ALT] || pressed[GLFW_KEY_RIGHT_ALT];
	anyShiftPressed = pressed[GLFW_KEY_LEFT_SHIFT] || pressed[GLFW_KEY_RIGHT_SHIFT];

	if (!io.WantCaptureKeyboard)
		cameraOrigin += getMoveDir() * frameTimeScale;

	moveGrabbedEnt();

	if (io.WantCaptureMouse)
		return;

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	vec2 mousePos(xpos, ypos);

	cameraContextMenus();

	cameraRotationControls(mousePos);

	makeVectors(cameraAngles, cameraForward, cameraRight, cameraUp);

	cameraObjectHovering();

	vertexEditControls();

	cameraPickingControls();

	shortcutControls();

	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
	
	for (int i = GLFW_KEY_SPACE; i < GLFW_KEY_LAST; i++) {
		oldPressed[i] = pressed[i];
		oldReleased[i] = released[i];
	}

	oldScroll = g_scroll;
}

void Renderer::vertexEditControls() {
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
		canTransform = transformTarget == TRANSFORM_OBJECT && transformMode == TRANSFORM_MOVE;
	}

	if (pressed[GLFW_KEY_F] && !oldPressed[GLFW_KEY_F]) {
		vector<int> selectedEdges;
		for (int i = 0; i < modelEdges.size(); i++) {
			if (modelEdges[i].selected) {
				selectedEdges.push_back(i);
			}
		}

		if (selectedEdges.size() != 2) {
			logf("Exactly 2 edges must be selected before splitting a face\n");
		}

		HullEdge& edge1 = modelEdges[selectedEdges[0]];
		HullEdge& edge2 = modelEdges[selectedEdges[1]];
		int commonPlane = -1;
		for (int i = 0; i < 2 && commonPlane == -1; i++) {
			int thisPlane = edge1.planes[i];
			for (int k = 0; k < 2; k++) {
				int otherPlane = edge2.planes[k];
				if (thisPlane == otherPlane) {
					commonPlane = thisPlane;
					break;
				}
			}
		}

		if (commonPlane == -1) {
			logf("Can't split edges that don't share a plane\n");
		}

		BSPPLANE& splitPlane = pickInfo.map->planes[commonPlane];
		vec3 splitPoints[2] = {
			getEdgeControlPoint(edge1),
			getEdgeControlPoint(edge2)
		};

		for (int i = 0; i < modelVerts.size(); i++) {
			modelVerts[i].selected = false;
		}
		for (int i = 0; i < modelEdges.size(); i++) {
			modelEdges[i].selected = false;
		}

		// TODO: split the face
		// - extrude split edge out a bit so the planes aren't coplanar and can be selected
		// - use extruded split edge and 
	}
}

void Renderer::cameraPickingControls() {
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		bool transforming = transformAxisControls();

		bool anyHover = hoverVert != -1 || hoverEdge != -1;
		if (isTransformableSolid && anyHover) {
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

		// object picking
		if (!transforming && oldLeftMouse != GLFW_PRESS) {

			applyTransform();

			if (invalidSolid) {
				for (int i = 0; i < modelVerts.size(); i++) {
					if (modelVerts[i].ptr) {
						*modelVerts[i].ptr = modelVerts[i].undoPos;
					}
				}
				invalidSolid = false;
				mapRenderers[pickInfo.mapIdx]->refreshModel(pickInfo.ent->getBspModelIdx());
			}
			
			pickObject();
			pickCount++;
		}
	}
	else {
		if (draggingAxis != -1) {
			draggingAxis = -1;
			applyTransform();
		}
	}
}

void Renderer::applyTransform() {
	if (pickInfo.valid && pickInfo.modelIdx > 0) {
		bool transformingVerts = transformTarget == TRANSFORM_VERTEX && isTransformableSolid;
		bool scalingObject = transformTarget == TRANSFORM_OBJECT && transformMode == TRANSFORM_SCALE;

		if (transformingVerts || scalingObject) {
			invalidSolid = !pickInfo.map->vertex_manipulation_sync(pickInfo.modelIdx, modelVerts, false);
			for (int i = 0; i < modelVerts.size(); i++) {
				modelVerts[i].startPos = modelVerts[i].pos;
				if (!invalidSolid) {
					modelVerts[i].undoPos = modelVerts[i].pos;
				}
			}

			if (scalingObject) {
				for (int i = 0; i < scaleTexinfos.size(); i++) {
					BSPTEXTUREINFO& info = pickInfo.map->texinfos[scaleTexinfos[i].texinfoIdx];
					scaleTexinfos[i].oldShiftS = info.shiftS;
					scaleTexinfos[i].oldShiftT = info.shiftT;
					scaleTexinfos[i].oldS = info.vS;
					scaleTexinfos[i].oldT = info.vT;
				}
			}
		}
	}
}

void Renderer::cameraRotationControls(vec2 mousePos) {
	// camera rotation
	if (draggingAxis == -1 && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
		if (!cameraIsRotating) {
			lastMousePos = mousePos;
			cameraIsRotating = true;
			totalMouseDrag = vec2();
		}
		else {
			vec2 drag = mousePos - lastMousePos;
			cameraAngles.z += drag.x * 0.5f;
			cameraAngles.x += drag.y * 0.5f;

			totalMouseDrag += vec2(fabs(drag.x), fabs(drag.y));

			cameraAngles.x = clamp(cameraAngles.x, -90.0f, 90.0f);
			if (cameraAngles.z > 180.0f) {
				cameraAngles.z -= 360.0f;
			}
			else if (cameraAngles.z < -180.0f) {
				cameraAngles.z += 360.0f;
			}
			lastMousePos = mousePos;
		}

		ImGui::SetWindowFocus(NULL);
		ImGui::ClearActiveID();
	}
	else {
		cameraIsRotating = false;
		totalMouseDrag = vec2();
	}
}

void Renderer::cameraObjectHovering() {
	if (transformTarget == TRANSFORM_VERTEX && pickInfo.valid && pickInfo.entIdx > 0) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		PickInfo vertPick;
		memset(&vertPick, 0, sizeof(PickInfo));
		vertPick.bestDist = 9e99;

		vec3 entOrigin = pickInfo.ent->getOrigin();
		
		hoverEdge = -1;
		if (!(anyVertSelected && !anyEdgeSelected)) {
			for (int i = 0; i < modelEdges.size(); i++) {
				vec3 ori = getEdgeControlPoint(modelEdges[i]) + entOrigin;
				float s = (ori - cameraOrigin).length() * vertExtentFactor * 2.0f;
				vec3 min = vec3(-s, -s, -s) + ori;
				vec3 max = vec3(s, s, s) + ori;
				if (pickAABB(pickStart, pickDir, min, max, vertPick.bestDist)) {
					hoverEdge = i;
				}
			}
		}

		hoverVert = -1;
		if (!anyEdgeSelected) {
			for (int i = 0; i < modelVerts.size(); i++) {
				vec3 ori = entOrigin + modelVerts[i].pos;
				float s = (ori - cameraOrigin).length() * vertExtentFactor * 2.0f;
				vec3 min = vec3(-s, -s, -s) + ori;
				vec3 max = vec3(s, s, s) + ori;
				if (pickAABB(pickStart, pickDir, min, max, vertPick.bestDist)) {
					hoverVert = i;
				}
			}
		}
	}

	if (transformTarget == TRANSFORM_VERTEX && transformMode == TRANSFORM_SCALE)
		return; // 3D scaling disabled in vertex edit mode

	// axis handle hovering
	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);
	hoverAxis = -1;
	if (showDragAxes && !movingEnt && pickInfo.valid && pickInfo.entIdx > 0 && hoverVert == -1 && hoverEdge == -1) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		PickInfo axisPick;
		memset(&axisPick, 0, sizeof(PickInfo));
		axisPick.bestDist = 9e99;

		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		Entity* ent = map->ents[pickInfo.entIdx];
		vec3 origin = activeAxes.origin;

		int axisChecks = transformMode == TRANSFORM_SCALE ? activeAxes.numAxes : 3;
		for (int i = 0; i < axisChecks; i++) {
			if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[i], origin + activeAxes.maxs[i], axisPick.bestDist)) {
				hoverAxis = i;
			}
		}

		// center cube gets priority for selection (hard to select from some angles otherwise)
		if (transformMode == TRANSFORM_MOVE) {
			float bestDist = 9e99;
			if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[3], origin + activeAxes.maxs[3], bestDist)) {
				hoverAxis = 3;
			}
		}
	}
}

void Renderer::cameraContextMenus() {
	// context menus
	bool wasTurning = cameraIsRotating && totalMouseDrag.length() >= 1;
	if (draggingAxis == -1 && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE && oldRightMouse != GLFW_RELEASE && !wasTurning) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);

		PickInfo tempPick;
		memset(&tempPick, 0, sizeof(PickInfo));
		tempPick.bestDist = 9e99;
		for (int i = 0; i < mapRenderers.size(); i++) {
			if (mapRenderers[i]->pickPoly(pickStart, pickDir, tempPick)) {
				tempPick.mapIdx = i;
			}
		}

		if (tempPick.entIdx != 0 && tempPick.entIdx == pickInfo.entIdx) {
			gui->openContextMenu(pickInfo.entIdx);
		}
		else {
			gui->openContextMenu(-1);
		}
	}
}

void Renderer::moveGrabbedEnt() {
	// grabbing
	if (pickInfo.valid && movingEnt) {
		if (g_scroll != oldScroll) {
			float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 4.0f : 2.0f;
			if (pressed[GLFW_KEY_LEFT_CONTROL])
				moveScale = 1.0f;
			if (g_scroll < oldScroll)
				moveScale *= -1;

			grabDist += 16 * moveScale;
		}

		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		vec3 delta = (cameraOrigin + cameraForward * grabDist) - grabStartOrigin;
		Entity* ent = map->ents[pickInfo.entIdx];

		vec3 oldOrigin = gragStartEntOrigin;
		vec3 offset = getEntOffset(map, ent);
		vec3 newOrigin = (oldOrigin + delta) - offset;
		vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

		ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
		mapRenderers[pickInfo.mapIdx]->refreshEnt(pickInfo.entIdx);
	}
}

void Renderer::shortcutControls() {
	// shortcuts
	if (pressed[GLFW_KEY_G] == GLFW_PRESS && oldPressed[GLFW_KEY_G] != GLFW_PRESS) {
		movingEnt = !movingEnt;
		if (movingEnt)
			grabEnt();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_C] && !oldPressed[GLFW_KEY_C]) {
		copyEnt();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_X] && !oldPressed[GLFW_KEY_X]) {
		cutEnt();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_V] && !oldPressed[GLFW_KEY_V]) {
		pasteEnt(false);
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_M] && !oldPressed[GLFW_KEY_M]) {
		gui->showTransformWidget = !gui->showTransformWidget;
	}
	if (anyAltPressed && pressed[GLFW_KEY_ENTER] && !oldPressed[GLFW_KEY_ENTER]) {
		gui->showKeyvalueWidget = !gui->showKeyvalueWidget;
	}
	if (pressed[GLFW_KEY_DELETE] && !oldPressed[GLFW_KEY_DELETE]) {
		deleteEnt();
	}
}

void Renderer::pickObject() {
	vec3 pickStart, pickDir;
	getPickRay(pickStart, pickDir);

	int oldEntIdx = pickInfo.entIdx;
	memset(&pickInfo, 0, sizeof(PickInfo));
	pickInfo.bestDist = 9e99;
	for (int i = 0; i < mapRenderers.size(); i++) {
		if (mapRenderers[i]->pickPoly(pickStart, pickDir, pickInfo)) {
			pickInfo.mapIdx = i;
		}
	}

	if (movingEnt && oldEntIdx != pickInfo.entIdx) {
		movingEnt = false;
	}

	if (pickInfo.modelIdx > 0)
		pickInfo.map->print_model_hull(pickInfo.modelIdx, 1);

	updateModelVerts();

	isTransformableSolid = true;
	if (pickInfo.modelIdx > 0) {
		isTransformableSolid = pickInfo.map->is_convex(pickInfo.modelIdx);
	}
}

bool Renderer::transformAxisControls() {

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	if (!canTransform) {
		return false;
	}

	// axis handle dragging
	if (showDragAxes && !movingEnt && hoverAxis != -1 && draggingAxis == -1) {
		draggingAxis = hoverAxis;

		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		Entity* ent = map->ents[pickInfo.entIdx];

		axisDragEntOriginStart = getEntOrigin(map, ent);
		axisDragStart = getAxisDragPoint(axisDragEntOriginStart);
	}

	if (showDragAxes && !movingEnt && draggingAxis >= 0) {
		Bsp* map = pickInfo.map;
		Entity* ent = pickInfo.ent;

		activeAxes.model[draggingAxis].setColor(activeAxes.hoverColor[draggingAxis]);

		vec3 dragPoint = getAxisDragPoint(axisDragEntOriginStart);
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
			else {
				vec3 offset = getEntOffset(map, ent);
				vec3 newOrigin = (axisDragEntOriginStart + delta) - offset;
				vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

				ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
				mapRenderers[pickInfo.mapIdx]->refreshEnt(pickInfo.entIdx);
			}
			
		}
		else {
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
				mapRenderers[pickInfo.mapIdx]->refreshModel(ent->getBspModelIdx());
			}
		}

		return true;
	}

	return false;
}

vec3 Renderer::getMoveDir()
{
	mat4x4 rotMat;
	rotMat.loadIdentity();
	rotMat.rotateX(PI * cameraAngles.x / 180.0f);
	rotMat.rotateZ(PI * cameraAngles.z / 180.0f);

	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);


	vec3 wishdir(0, 0, 0);
	if (pressed[GLFW_KEY_A])
	{
		wishdir -= right;
	}
	if (pressed[GLFW_KEY_D])
	{
		wishdir += right;
	}
	if (pressed[GLFW_KEY_W])
	{
		wishdir += forward;
	}
	if (pressed[GLFW_KEY_S])
	{
		wishdir -= forward;
	}

	wishdir *= moveSpeed;

	if (anyShiftPressed)
		wishdir *= 4.0f;
	if (anyCtrlPressed)
		wishdir *= 0.1f;
	return wishdir;
}

void Renderer::getPickRay(vec3& start, vec3& pickDir) {
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	// invert ypos
	ypos = windowHeight - ypos;

	// translate mouse coordinates so that the origin lies in the center and is a scaler from +/-1.0
	float mouseX = ((xpos / (double)windowWidth) * 2.0f) - 1.0f;
	float mouseY = ((ypos / (double)windowHeight) * 2.0f) - 1.0f;

	// http://schabby.de/picking-opengl-ray-tracing/
	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);

	vec3 view = forward.normalize(1.0f);
	vec3 h = crossProduct(view, up).normalize(1.0f); // 3D float vector
	vec3 v = crossProduct(h, view).normalize(1.0f); // 3D float vector

	// convert fovy to radians 
	float rad = fov * PI / 180.0f;
	float vLength = tan(rad / 2.0f) * zNear;
	float hLength = vLength * (windowWidth / (float)windowHeight);

	v *= vLength;
	h *= hLength;

	// linear combination to compute intersection of picking ray with view port plane
	start = cameraOrigin + view * zNear + h * mouseX + v * mouseY;

	// compute direction of picking ray by subtracting intersection point with camera position
	pickDir = (start - cameraOrigin).normalize(1.0f);
}

BspRenderer* Renderer::getMapContainingCamera() {
	for (int i = 0; i < mapRenderers.size(); i++) {
		Bsp* map = mapRenderers[i]->map;

		vec3 mins, maxs;
		map->get_bounding_box(mins, maxs);

		if (cameraOrigin.x > mins.x && cameraOrigin.y > mins.y && cameraOrigin.z > mins.z &&
			cameraOrigin.x < maxs.x && cameraOrigin.y < maxs.y && cameraOrigin.z < maxs.z) {
			return mapRenderers[i];
		}
	}
	return mapRenderers[0];
}

void Renderer::setupView() {
	fov = 75.0f;
	zNear = 1.0f;
	zFar = 262144.0f;

	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	glViewport(0, 0, windowWidth, windowHeight);

	projection.perspective(fov, (float)windowWidth / (float)windowHeight, zNear, zFar);

	view.loadIdentity();
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}

void Renderer::addMap(Bsp* map) {
	BspRenderer* mapRenderer = new BspRenderer(map, bspShader, colorShader, pointEntRenderer);

	mapRenderers.push_back(mapRenderer);
}

void Renderer::drawLine(vec3 start, vec3 end, COLOR3 color) {
	cVert verts[2];

	verts[0].x = start.x;
	verts[0].y = start.z;
	verts[0].z = -start.y;
	verts[0].c = color;

	verts[1].x = end.x;
	verts[1].y = end.z;
	verts[1].z = -end.y;
	verts[1].c = color;

	VertexBuffer buffer(colorShader, COLOR_3B | POS_3F, &verts[0], 2);
	buffer.draw(GL_LINES);
}

void Renderer::drawPlane(BSPPLANE& plane, COLOR3 color) {

	vec3 ori = plane.vNormal * plane.fDist;
	vec3 crossDir = fabs(plane.vNormal.z) > 0.9f ? vec3(1, 0, 0) : vec3(0, 0, 1);
	vec3 right = crossProduct(plane.vNormal, crossDir);
	vec3 up = crossProduct(right, plane.vNormal);

	float s = 100.0f;

	vec3 topLeft = vec3(ori + right * -s + up * s).flip();
	vec3 topRight = vec3(ori + right * s + up * s).flip();
	vec3 bottomLeft = vec3(ori + right * -s + up * -s).flip();
	vec3 bottomRight = vec3(ori + right * s + up * -s).flip();

	cVert topLeftVert(topLeft, color);
	cVert topRightVert(topRight, color);
	cVert bottomLeftVert(bottomLeft, color);
	cVert bottomRightVert(bottomRight, color);
	cQuad quad(bottomRightVert, bottomLeftVert, topLeftVert, topRightVert);

	VertexBuffer buffer(colorShader, COLOR_3B | POS_3F, &quad, 6);
	buffer.draw(GL_TRIANGLES);
}

void Renderer::drawClipnodes(Bsp* map, int iNode, int& currentPlane, int activePlane) {
	BSPCLIPNODE& node = map->clipnodes[iNode];

	if (currentPlane == activePlane)
		drawPlane(map->planes[node.iPlane], { 255, 255, 255 });
	currentPlane++;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			drawClipnodes(map, node.iChildren[i], currentPlane, activePlane);
		}
	}
}

vec3 Renderer::getEntOrigin(Bsp* map, Entity* ent) {
	vec3 origin = ent->hasKey("origin") ? parseVector(ent->keyvalues["origin"]) : vec3(0, 0, 0);
	return origin + getEntOffset(map, ent);
}

vec3 Renderer::getEntOffset(Bsp* map, Entity* ent) {
	if (ent->isBspModel()) {
		BSPMODEL& model = map->models[ent->getBspModelIdx()];
		return model.nMins + (model.nMaxs - model.nMins) * 0.5f;
	}
	return vec3(0, 0, 0);
}

void Renderer::updateDragAxes() {
	Bsp* map = NULL;
	Entity* ent = NULL;

	if (pickInfo.valid && pickInfo.entIdx > 0) {
		map = mapRenderers[pickInfo.mapIdx]->map;
		ent = map->ents[pickInfo.entIdx];
	}

	vec3 entMin, entMax;
	// set origin of the axes
	if (transformMode == TRANSFORM_SCALE) {
		if (ent != NULL && ent->isBspModel()) {
			map->get_model_vertex_bounds(ent->getBspModelIdx(), entMin, entMax);
			vec3 modelOrigin = entMin + (entMax - entMin) * 0.5f;

			entMax -= modelOrigin;
			entMin -= modelOrigin;

			scaleAxes.origin = modelOrigin;
			if (ent->hasKey("origin")) {
				scaleAxes.origin += parseVector(ent->keyvalues["origin"]);
			}
		}
	}
	else {
		if (ent != NULL) {
			moveAxes.origin = getEntOrigin(map, ent);
		}

		if (transformTarget == TRANSFORM_VERTEX) {
			vec3 entOrigin = ent ? ent->getOrigin() : vec3();
			vec3 min(9e99, 9e99, 9e99);
			vec3 max(-9e99, -9e99, -9e99);
			int selectTotal = 0;
			for (int i = 0; i < modelVerts.size(); i++) {
				if (modelVerts[i].selected) {
					vec3 v = modelVerts[i].pos + entOrigin;
					if (v.x < min.x) min.x = v.x;
					if (v.y < min.y) min.y = v.y;
					if (v.z < min.z) min.z = v.z;
					if (v.x > max.x) max.x = v.x;
					if (v.y > max.y) max.y = v.y;
					if (v.z > max.z) max.z = v.z;
					selectTotal++;
				}
			}
			if (selectTotal != 0)
				moveAxes.origin = min + (max - min) * 0.5f;
		}
	}

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	float baseScale = (activeAxes.origin - cameraOrigin).length() * 0.005f;
	float s = baseScale;
	float s2 = baseScale*2;
	float d = baseScale*32;

	// create the meshes
	if (transformMode == TRANSFORM_SCALE) {
		vec3 axisMins[6] = {
			vec3(0, -s, -s) + vec3(entMax.x,0,0), // x+
			vec3(-s, 0, -s) + vec3(0,entMax.y,0), // y+
			vec3(-s, -s, 0) + vec3(0,0,entMax.z), // z+

			vec3(-d, -s, -s) + vec3(entMin.x,0,0), // x-
			vec3(-s, -d, -s) + vec3(0,entMin.y,0), // y-
			vec3(-s, -s, -d) + vec3(0,0,entMin.z)  // z-
		};
		vec3 axisMaxs[6] = {
			vec3(d, s, s) + vec3(entMax.x,0,0), // x+
			vec3(s, d, s) + vec3(0,entMax.y,0), // y+
			vec3(s, s, d) + vec3(0,0,entMax.z), // z+

			vec3(0, s, s) + vec3(entMin.x,0,0), // x-
			vec3(s, 0, s) + vec3(0,entMin.y,0), // y-
			vec3(s, s, 0) + vec3(0,0,entMin.z)  // z-
		};
		
		scaleAxes.model[0] = cCube(axisMins[0], axisMaxs[0], scaleAxes.dimColor[0]);
		scaleAxes.model[1] = cCube(axisMins[1], axisMaxs[1], scaleAxes.dimColor[1]);
		scaleAxes.model[2] = cCube(axisMins[2], axisMaxs[2], scaleAxes.dimColor[2]);

		scaleAxes.model[3] = cCube(axisMins[3], axisMaxs[3], scaleAxes.dimColor[3]);
		scaleAxes.model[4] = cCube(axisMins[4], axisMaxs[4], scaleAxes.dimColor[4]);
		scaleAxes.model[5] = cCube(axisMins[5], axisMaxs[5], scaleAxes.dimColor[5]);

		// flip to HL coords
		cVert* verts = (cVert*)scaleAxes.model;
		for (int i = 0; i < 6*6*6; i++) {
			float tmp = verts[i].z;
			verts[i].z = -verts[i].y;
			verts[i].y = tmp;
		}

 		// larger mins/maxs so you can be less precise when selecting them
		s *= 4;
		vec3 grabAxisMins[6] = {
			vec3(0, -s, -s) + vec3(entMax.x,0,0), // x+
			vec3(-s, 0, -s) + vec3(0,entMax.y,0), // y+
			vec3(-s, -s, 0) + vec3(0,0,entMax.z), // z+

			vec3(-d, -s, -s) + vec3(entMin.x,0,0), // x-
			vec3(-s, -d, -s) + vec3(0,entMin.y,0), // y-
			vec3(-s, -s, -d) + vec3(0,0,entMin.z)  // z-
		};
		vec3 grabAxisMaxs[6] = {
			vec3(d, s, s) + vec3(entMax.x,0,0), // x+
			vec3(s, d, s) + vec3(0,entMax.y,0), // y+
			vec3(s, s, d) + vec3(0,0,entMax.z), // z+

			vec3(0, s, s) + vec3(entMin.x,0,0), // x-
			vec3(s, 0, s) + vec3(0,entMin.y,0), // y-
			vec3(s, s, 0) + vec3(0,0,entMin.z)  // z-
		};

		for (int i = 0; i < 6; i++) {
			scaleAxes.mins[i] = grabAxisMins[i];
			scaleAxes.maxs[i] = grabAxisMaxs[i];
		}
	}
	else {
		// flipped for HL coords
		moveAxes.model[0] = cCube(vec3(0, -s, -s), vec3(d, s, s), moveAxes.dimColor[0]);
		moveAxes.model[2] = cCube(vec3(-s, 0, -s), vec3(s, d, s), moveAxes.dimColor[2]);
		moveAxes.model[1] = cCube(vec3(-s, -s, 0), vec3(s, s, -d), moveAxes.dimColor[1]);
		moveAxes.model[3] = cCube(vec3(-s2, -s2, -s2), vec3(s2, s2, s2), moveAxes.dimColor[3]);

		// larger mins/maxs so you can be less precise when selecting them
		s *= 4;
		s2 *= 1.5f;

		activeAxes.mins[0] = vec3(0, -s, -s);
		activeAxes.mins[1] = vec3(-s, 0, -s);
		activeAxes.mins[2] = vec3(-s, -s, 0);
		activeAxes.mins[3] = vec3(-s2, -s2, -s2);

		activeAxes.maxs[0] = vec3(d, s, s);
		activeAxes.maxs[1] = vec3(s, d, s);
		activeAxes.maxs[2] = vec3(s, s, d);
		activeAxes.maxs[3] = vec3(s2, s2, s2);
	}
	

	if (draggingAxis >= 0 && draggingAxis < activeAxes.numAxes) {
		activeAxes.model[draggingAxis].setColor(activeAxes.hoverColor[draggingAxis]);
	}
	else if (hoverAxis >= 0 && hoverAxis < activeAxes.numAxes) {
		activeAxes.model[hoverAxis].setColor(activeAxes.hoverColor[hoverAxis]);
	}
	else if (gui->guiHoverAxis >= 0 && gui->guiHoverAxis < activeAxes.numAxes) {
		activeAxes.model[gui->guiHoverAxis].setColor(activeAxes.hoverColor[gui->guiHoverAxis]);
	}
}

vec3 Renderer::getAxisDragPoint(vec3 origin) {
	vec3 pickStart, pickDir;
	getPickRay(pickStart, pickDir);

	vec3 axisNormals[3] = {
		vec3(1,0,0),
		vec3(0,1,0),
		vec3(0,0,1)
	};

	// get intersection points between the pick ray and each each movement direction plane
	float dots[3];
	for (int i = 0; i < 3; i++) {
		dots[i] = fabs(dotProduct(cameraForward, axisNormals[i]));
	}

	// best movement planee is most perpindicular to the camera direction
	// and ignores the plane being moved
	int bestMovementPlane = 0;
	switch (draggingAxis % 3) {
		case 0: bestMovementPlane = dots[1] > dots[2] ? 1 : 2; break;
		case 1: bestMovementPlane = dots[0] > dots[2] ? 0 : 2; break;
		case 2: bestMovementPlane = dots[1] > dots[0] ? 1 : 0; break;
	}

	float fDist = ((float*)&origin)[bestMovementPlane];
	float intersectDist;
	rayPlaneIntersect(pickStart, pickDir, axisNormals[bestMovementPlane], fDist, intersectDist);

	// don't let ents zoom out to infinity
	if (intersectDist < 0) {
		intersectDist = 0;
	}

	return pickStart + pickDir * intersectDist;
}

void Renderer::updateModelVerts() {
	if (!pickInfo.valid || pickInfo.modelIdx <= 0)
		return;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	int modelIdx = map->ents[pickInfo.entIdx]->getBspModelIdx();

	if (modelVertBuff) {
		delete modelVertBuff;
		delete[] modelVertCubes;
		scaleTexinfos.clear();
		modelEdges.clear();
		map->remove_unused_model_structures();
	}
	
	scaleTexinfos = map->getScalableTexinfos(modelIdx);
	modelVerts = map->getModelPlaneIntersectVerts(pickInfo.modelIdx);

	// get verts for each plane
	std::map<int, vector<int>> planeVerts;
	for (int i = 0; i < modelVerts.size(); i++) {
		for (int k = 0; k < modelVerts[i].iPlanes.size(); k++) {
			int iPlane = modelVerts[i].iPlanes[k];
			planeVerts[iPlane].push_back(i);
		}
	}

	// sort verts CCW on each plane to get edges
	// TODO: reverse edges if the plane normal points inside the solid
	for (auto it = planeVerts.begin(); it != planeVerts.end(); ++it) {
		int iPlane = it->first;
		vector<int> verts = it->second;
		BSPPLANE& plane = map->planes[iPlane];

		vec3 plane_z = plane.vNormal;
		vec3 plane_x = (modelVerts[verts[1]].pos - modelVerts[verts[0]].pos).normalize();
		vec3 plane_y = crossProduct(plane_z, plane_x).normalize();
		if (fabs(dotProduct(plane_z, plane_x)) > 0.99f) {
			logf("ZOMG CHANGE NORMAL\n");
		}
		mat4x4 worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

		vector<vec2> localVerts;
		for (int e = 0; e < verts.size(); e++) {
			vec2 localVert = (worldToLocal * vec4(modelVerts[verts[e]].pos, 1)).xy();
			localVerts.push_back(localVert);
		}

		vec2 center = getCenter(localVerts);
		vector<int> orderedVerts;
		orderedVerts.push_back(verts[0]);
		vec2 lastVert = localVerts[0];
		verts.erase(verts.begin() + 0);
		localVerts.erase(localVerts.begin() + 0);
		for (int k = 0, sz = verts.size(); k < sz; k++) {
			int bestIdx = 0;
			float bestAngle = 9e99;

			for (int i = 0; i < verts.size(); i++) {
				vec2 a = lastVert;
				vec2 b = localVerts[i];
				double a1 = atan2(a.x - center.x, a.y - center.y);
				double a2 = atan2(b.x - center.x, b.y - center.y);
				float angle = a2 - a1;
				if (angle < 0)
					angle += PI * 2;

				if (angle < bestAngle) {
					bestAngle = angle;
					bestIdx = i;
				}
			}

			lastVert = localVerts[bestIdx];
			orderedVerts.push_back(verts[bestIdx]);
			verts.erase(verts.begin() + bestIdx);
			localVerts.erase(localVerts.begin() + bestIdx);
		}

		for (int i = 0; i < orderedVerts.size(); i++) {
			HullEdge edge;
			edge.verts[0] = orderedVerts[i];
			edge.verts[1] = orderedVerts[(i + 1) % orderedVerts.size()];
			edge.selected = false;


			vec3 midPoint = getEdgeControlPoint(edge);
			int planeCount = 0;
			for (auto it2 = planeVerts.begin(); it2 != planeVerts.end(); ++it2) {
				int iPlane = it2->first;
				BSPPLANE& p = map->planes[iPlane];
				if (fabs(dotProduct(midPoint, p.vNormal) - p.fDist) < EPSILON) {
					edge.planes[planeCount % 2] = iPlane;
					planeCount++;
				}
			}
			if (planeCount != 2) {
				logf("ERROR: Edge connected to %d planes!\n", planeCount);
			}

			modelEdges.push_back(edge);
		}
	}

	int numCubes = modelVerts.size() + modelEdges.size();
	modelVertCubes = new cCube[numCubes];
	modelVertBuff = new VertexBuffer(colorShader, COLOR_3B | POS_3F, modelVertCubes, 6 * 6 * numCubes);
	logf("%d intersection points\n", modelVerts.size());
}

void Renderer::scaleSelectedObject(float x, float y, float z) {
	vec3 minDist;
	vec3 maxDist;

	for (int i = 0; i < modelVerts.size(); i++) {
		vec3 v = modelVerts[i].pos;
		if (v.x > maxDist.x) maxDist.x = v.x;
		if (v.x < minDist.x) minDist.x = v.x;

		if (v.y > maxDist.y) maxDist.y = v.y;
		if (v.y < minDist.y) minDist.y = v.y;

		if (v.z > maxDist.z) maxDist.z = v.z;
		if (v.z < minDist.z) minDist.z = v.z;
	}
	vec3 distRange = maxDist - minDist;

	vec3 dir;
	dir.x = (distRange.x * x) - distRange.x;
	dir.y = (distRange.y * y) - distRange.y;
	dir.z = (distRange.z * z) - distRange.z;

	scaleSelectedObject(dir, vec3());
}

void Renderer::scaleSelectedObject(vec3 dir, vec3 fromDir) {
	if (!pickInfo.valid || pickInfo.modelIdx <= 0)
		return;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;

	bool scaleFromOrigin = fromDir.x == 0 && fromDir.y == 0 && fromDir.z == 0;

	vec3 minDist = vec3(9e99, 9e99, 9e99);
	vec3 maxDist = vec3(-9e99, -9e99, -9e99);

	for (int i = 0; i < modelVerts.size(); i++) {
		vec3 v = modelVerts[i].startPos;

		if (v.x > maxDist.x) maxDist.x = v.x;
		if (v.x < minDist.x) minDist.x = v.x;

		if (v.y > maxDist.y) maxDist.y = v.y;
		if (v.y < minDist.y) minDist.y = v.y;

		if (v.z > maxDist.z) maxDist.z = v.z;
		if (v.z < minDist.z) minDist.z = v.z;
	}

	vec3 distRange = maxDist - minDist;

	vec3 scaleFromDist = minDist;
	if (scaleFromOrigin) {
		scaleFromDist = minDist + (maxDist - minDist) * 0.5f;
	}
	else {
		if (fromDir.x < 0) {
			scaleFromDist.x = maxDist.x;
			dir.x = -dir.x;
		}
		if (fromDir.y < 0) {
			scaleFromDist.y = maxDist.y;
			dir.y = -dir.y;
		}
		if (fromDir.z < 0) {
			scaleFromDist.z = maxDist.z;
			dir.z = -dir.z;
		}
	}

	for (int i = 0; i < modelVerts.size(); i++) {
		vec3 stretchFactor = (modelVerts[i].startPos - scaleFromDist) / distRange;
		modelVerts[i].pos = modelVerts[i].startPos + dir * stretchFactor;
		if (modelVerts[i].ptr) {
			*modelVerts[i].ptr = modelVerts[i].pos;
		}
	}

	//
	// TODO: I have no idea what I'm doing but this code scales axis-aligned texture coord axes correctly.
	//       Rewrite all of this after understanding texture axes.
	//

	if (!textureLock)
		return;

	minDist = vec3(9e99, 9e99, 9e99);
	maxDist = vec3(-9e99, -9e99, -9e99);
	
	for (int i = 0; i < modelVerts.size(); i++) {
		vec3 v = modelVerts[i].pos;
		if (v.x > maxDist.x) maxDist.x = v.x;
		if (v.x < minDist.x) minDist.x = v.x;

		if (v.y > maxDist.y) maxDist.y = v.y;
		if (v.y < minDist.y) minDist.y = v.y;

		if (v.z > maxDist.z) maxDist.z = v.z;
		if (v.z < minDist.z) minDist.z = v.z;
	}
	vec3 newDistRange = maxDist - minDist;
	vec3 scaleFactor = distRange / newDistRange;

	mat4x4 scaleMat;
	scaleMat.loadIdentity();
	scaleMat.scale(scaleFactor.x, scaleFactor.y, scaleFactor.z);

	for (int i = 0; i < scaleTexinfos.size(); i++) {
		ScalableTexinfo& oldinfo = scaleTexinfos[i];
		BSPTEXTUREINFO& info = map->texinfos[scaleTexinfos[i].texinfoIdx];
		BSPPLANE& plane = map->planes[scaleTexinfos[i].planeIdx];

		info.vS = (scaleMat * vec4(oldinfo.oldS, 1)).xyz();
		info.vT = (scaleMat * vec4(oldinfo.oldT, 1)).xyz();

		float shiftS = oldinfo.oldShiftS;
		float shiftT = oldinfo.oldShiftT;

		// magic guess-and-check code that somehow works some of the time
		// also its shit
		for (int k = 0; k < 3; k++) {
			vec3 stretchDir;
			if (k == 0) stretchDir = vec3(dir.x, 0, 0).normalize();
			if (k == 1) stretchDir = vec3(0, dir.y, 0).normalize();
			if (k == 2) stretchDir = vec3(0, 0, dir.z).normalize();

			float refDist = 0;
			if (k == 0) refDist = scaleFromDist.x;
			if (k == 1) refDist = scaleFromDist.y;
			if (k == 2) refDist = scaleFromDist.z;

			vec3 texFromDir;
			if (k == 0) texFromDir = dir * vec3(1,0,0);
			if (k == 1) texFromDir = dir * vec3(0,1,0);
			if (k == 2) texFromDir = dir * vec3(0,0,1);

			float dotS = dotProduct(oldinfo.oldS.normalize(), stretchDir);
			float dotT = dotProduct(oldinfo.oldT.normalize(), stretchDir);

			float asdf = dotProduct(texFromDir, info.vS) < 0 ? 1 : -1;
			float asdf2 = dotProduct(texFromDir, info.vT) < 0 ? 1 : -1;

			// hurr dur oh god im fucking retarded huurr
			if (k == 0 && dotProduct(texFromDir, fromDir) < 0 != fromDir.x < 0) {
				asdf *= -1;
				asdf2 *= -1;
			}
			if (k == 1 && dotProduct(texFromDir, fromDir) < 0 != fromDir.y < 0) {
				asdf *= -1;
				asdf2 *= -1;
			}
			if (k == 2 && dotProduct(texFromDir, fromDir) < 0 != fromDir.z < 0) {
				asdf *= -1;
				asdf2 *= -1;
			}

			float vsdiff = info.vS.length() - oldinfo.oldS.length();
			float vtdiff = info.vT.length() - oldinfo.oldT.length();

			shiftS += (refDist * vsdiff * fabs(dotS)) * asdf;
			shiftT += (refDist * vtdiff * fabs(dotT)) * asdf2;
		}

		info.shiftS = shiftS;
		info.shiftT = shiftT;
	}
}

void Renderer::moveSelectedVerts(vec3 delta) {
	for (int i = 0; i < modelVerts.size(); i++) {
		if (modelVerts[i].selected) {
			modelVerts[i].pos = modelVerts[i].startPos + delta;
			if (gridSnappingEnabled)
				modelVerts[i].pos = snapToGrid(modelVerts[i].pos);
			if (modelVerts[i].ptr)
				*modelVerts[i].ptr = modelVerts[i].pos;
		}
	}

	invalidSolid = !pickInfo.map->vertex_manipulation_sync(pickInfo.modelIdx, modelVerts, true);
	mapRenderers[pickInfo.mapIdx]->refreshModel(pickInfo.ent->getBspModelIdx());
}

void Renderer::scaleSelectedVerts(float x, float y, float z) {

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);
	vec3 fromOrigin = activeAxes.origin;

	vec3 min(9e99, 9e99, 9e99);
	vec3 max(-9e99, -9e99, -9e99);
	int selectTotal = 0;
	for (int i = 0; i < modelVerts.size(); i++) {
		if (modelVerts[i].selected) {
			vec3 v = modelVerts[i].pos;
			if (v.x < min.x) min.x = v.x;
			if (v.y < min.y) min.y = v.y;
			if (v.z < min.z) min.z = v.z;
			if (v.x > max.x) max.x = v.x;
			if (v.y > max.y) max.y = v.y;
			if (v.z > max.z) max.z = v.z;
			selectTotal++;
		}
	}
	if (selectTotal != 0)
		fromOrigin = min + (max - min) * 0.5f;

	debugVec0 = fromOrigin;

	for (int i = 0; i < modelVerts.size(); i++) {

		if (modelVerts[i].selected) {
			vec3 delta = modelVerts[i].startPos - fromOrigin;
			modelVerts[i].pos = fromOrigin + delta*vec3(x,y,z);
			if (gridSnappingEnabled)
				modelVerts[i].pos = snapToGrid(modelVerts[i].pos);
			if (modelVerts[i].ptr)
				*modelVerts[i].ptr = modelVerts[i].pos;
		}
	}

	invalidSolid = !pickInfo.map->vertex_manipulation_sync(pickInfo.modelIdx, modelVerts, true);
	mapRenderers[pickInfo.mapIdx]->refreshModel(pickInfo.ent->getBspModelIdx());
}

vec3 Renderer::getEdgeControlPoint(HullEdge& edge) {
	vec3 v0 = modelVerts[edge.verts[0] ].pos;
	vec3 v1 = modelVerts[edge.verts[1] ].pos;
	return v0 + (v1 - v0) * 0.5f;
}

vec3 Renderer::snapToGrid(vec3 pos) {
	float snapSize = pow(2.0, gridSnapLevel);
	float halfSnap = snapSize * 0.5f;
	
	int x = round((pos.x) / snapSize) * snapSize;
	int y = round((pos.y) / snapSize) * snapSize;
	int z = round((pos.z) / snapSize) * snapSize;

	return vec3(x, y, z);
}

void Renderer::grabEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;
	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	grabDist = (getEntOrigin(map, map->ents[pickInfo.entIdx]) - cameraOrigin).length();
	grabStartOrigin = cameraOrigin + cameraForward * grabDist;
	gragStartEntOrigin = cameraOrigin + cameraForward * grabDist;
}

void Renderer::cutEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;

	if (copiedEnt != NULL)
		delete copiedEnt;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	copiedEnt = new Entity();
	*copiedEnt = *map->ents[pickInfo.entIdx];
	delete map->ents[pickInfo.entIdx];
	map->ents.erase(map->ents.begin() + pickInfo.entIdx);
	mapRenderers[pickInfo.mapIdx]->preRenderEnts();
	pickInfo.valid = false;
}

void Renderer::copyEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;

	if (copiedEnt != NULL)
		delete copiedEnt;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	copiedEnt = new Entity();
	*copiedEnt = *map->ents[pickInfo.entIdx];
}

void Renderer::pasteEnt(bool noModifyOrigin) {
	if (copiedEnt == NULL)
		return;

	Bsp* map = getMapContainingCamera()->map;

	Entity* insertEnt = new Entity();
	*insertEnt = *copiedEnt;

	if (!noModifyOrigin) {
		// can't just set camera origin directly because solid ents can have (0,0,0) origins
		vec3 oldOrigin = getEntOrigin(map, insertEnt);
		vec3 modelOffset = getEntOffset(map, insertEnt);

		vec3 moveDist = (cameraOrigin + cameraForward * 100) - oldOrigin;
		vec3 newOri = (oldOrigin + moveDist) - modelOffset;
		vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
		insertEnt->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
	}

	map->ents.push_back(insertEnt);

	pickInfo.entIdx = map->ents.size() - 1;
	pickInfo.valid = true;
	mapRenderers[pickInfo.mapIdx]->preRenderEnts();
}

void Renderer::deleteEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	map->ents.erase(map->ents.begin() + pickInfo.entIdx);
	mapRenderers[pickInfo.mapIdx]->preRenderEnts();
	pickInfo.valid = false;
}