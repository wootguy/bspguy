#include "Editor.h"
#include "PointEntRenderer.h"
#include "Fgd.h"
#include "Entity.h"
#include "util.h"
#include "tinyfiledialogs.h"
#include "Widget.h"
#include "ModelRenderer.h"
#include "NavRenderer.h"
#include "FrameBuffer.h"

#include <fstream>
#include <algorithm>
#include <unordered_map>

// everything except VIS, ENTITIES, MARKSURFS
#define EDIT_MODEL_LUMPS (PLANES | TEXTURES | VERTICES | NODES | TEXINFO | FACES | LIGHTING | CLIPNODES | LEAVES | EDGES | SURFEDGES | MODELS)

future<void> Editor::fgdFuture;

int glGetErrorDebug() {
	return glGetError();
}

const char* glErrorString(GLenum err)
{
	switch (err)
	{
	case GL_NO_ERROR:          return "GL_NO_ERROR";
	case GL_INVALID_ENUM:      return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:     return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
	case GL_STACK_OVERFLOW:    return "GL_STACK_OVERFLOW";
	case GL_STACK_UNDERFLOW:   return "GL_STACK_UNDERFLOW";
	case GL_OUT_OF_MEMORY:     return "GL_OUT_OF_MEMORY";
	case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
	default: return "Unknown GL error";
	}
}

void glCheckError(const char* checkMessage) {
	// error checking is very expensive
#ifdef DEBUG_MODE
	static int lastError = 0;
	int glerror = glGetError();
	if (glerror != GL_NO_ERROR) {
		if (lastError != glerror)
			errorf("Got OpenGL Error %d (%s) after %s\n", glerror, glErrorString(glerror), checkMessage);
		else
			debugf("Got OpenGL Error %d (%s) after %s\n", glerror, glErrorString(glerror), checkMessage);
		lastError = glerror;
	}
#endif
}

int g_scroll = 0;

Editor::Editor() {
	g_app = this;
	programStartTime = glfwGetTime();
	g_settings.loadDefault();
	g_settings.load();
	loadSettings();
	g_settings.renderer = clamp(g_settings.renderer, 0, RENDERER_COUNT - 1);
	memset(lightStylesEnabled, true, sizeof(bool) * MAXLIGHTMAPS);

	if (!createWindow()) {
		logf("Window creation failed. Does your graphics driver support OpenGL 2.1?\n");
		return;
	}

	glCheckError("window creation");

	const char* openglExts = (const char*)glGetString(GL_EXTENSIONS);
	g_opengl_texture_array_support = strstr(openglExts, "GL_EXT_texture_array");

	GLint texImageUnits, vertexAttributes, varyingFloats;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_max_texture_size);
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texImageUnits);
	glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &g_max_vtf_units);
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &vertexAttributes);
	glGetIntegerv(GL_MAX_VARYING_FLOATS, &varyingFloats);

	if (g_opengl_texture_array_support)
		glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &g_max_texture_array_layers);
	else
		g_max_texture_array_layers = 0;

	logf("\nOpenGL Version: %s\n", (char*)glGetString(GL_VERSION));
	debugf("    Max Texture size: %dx%d\n", g_max_texture_size, g_max_texture_size);
	debugf("    Max Vertex Attributes: %d / %d\n", vertexAttributes, MAX_VERTEX_ATTRIBUTES);
	debugf("    Max Varying Floats: %d / 32\n", varyingFloats);
	debugf("    Texture Units: %d / 3\n", texImageUnits);
	debugf("    Vertex Texture Fetch Units: %d\n", g_max_vtf_units);
	debugf("    Texture Array Layers: %d\n", g_max_texture_array_layers);

	//debugf("OpenGL Extensions:\n%s\n\n", openglExts);
	debugf("\n");

	if (varyingFloats < 32 || vertexAttributes < MAX_VERTEX_ATTRIBUTES || texImageUnits < 2) {
		logf("\nYOUR SYSTEM IS INCOMPATIBLE. EVERYTHING IS BROKEN.\n\n");
	}

	glCheckError("checking extensions");

	glewInit();

	glCheckError("glew init");

	// init to black screen instead of white
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// give ImGui something to push/pop to
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glfwSwapBuffers(window);
	glfwSwapInterval(1);

	glCheckError("glfw buffer setup");

	gui = new Gui(this);

	glCheckError("GUI init");

	updateGpuSupportFlags();
	compileShaderPrograms();

	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

	g_progress.simpleMode = true;

	pointEntRenderer = new PointEntRenderer(NULL, vector<Fgd*>());
	modelRenderer = new ModelRenderer();
	navRenderer = new NavRenderer();

	loadWidgetStates();

	reloading = true;

	memset(&undoLumpState, 0, sizeof(LumpState));

	glCheckError("Initializing context");

	//cameraOrigin = vec3(0, 0, 0);
	//cameraAngles = vec3(0, 0, 0);
}

Editor::~Editor() {
	glfwTerminate();
}

void Editor::postLoadFgds()
{
	delete pointEntRenderer;
	delete mergedFgd;
	for (int i = 0; i < fgds.size(); i++)
		delete fgds[i];
	fgds.clear();

	pointEntRenderer = (PointEntRenderer*)swapPointEntRenderer;
	mergedFgd = pointEntRenderer->mergedFgd;
	fgds = pointEntRenderer->fgds;

	mapRenderer->pointEntRenderer = pointEntRenderer;
	mapRenderer->preRenderEnts();
	if (reloadingGameDir) {
		mapRenderer->reloadTextures();
	}
	mapRenderer->pointEntRenderer->uploadCubeBuffers();

	g_cached_target_keys.clear();
	for (int i = 0; i < mapRenderer->map->ents.size(); i++) {
		Entity* ent = mapRenderer->map->ents[i];
		ent->clearCache();
		ent->getTargets(); // cache ent targets so first selection doesn't lag
		ent->getAllTargetnames(); // cache ent targets so first selection doesn't lag
	}

	swapPointEntRenderer = NULL;

	gui->entityReportFilterNeeded = true;

	updateEntConnections();
	updateEntDirectionVectors();
}

void Editor::postLoadFgdsAndTextures() {
	if (reloading) {
		logf("Previous reload not finished. Aborting reload.");
		return;
	}
	reloading = reloadingGameDir = true;
	fgdFuture = async(launch::async, &Editor::loadFgds, this);
}

void Editor::clearMapData() {
	clearUndoCommands();
	clearRedoCommands();
	mapArrangeMode = false;
	pvsCopyLeaves.clear();
	hiddenFaces.clear();
	hiddenLeaves.clear();
	entLinks.clear();
	entConnectionGraph.clear();
	clearStringMap();
	((FaceEditor*)gui->widgets[WIDGET_FACE_EDITOR])->clearTextureBrowserCache();

	/*
	for (auto item : studioModels) {
		if (item.second)
			delete item.second;
	}
	studioModels.clear();
	studioModelPaths.clear();
	*/

	for (EntityState& state : undoEntityState) {
		if (state.ent)
			delete state.ent;
	}
	undoEntityState.clear();

	if (mapRenderer) {
		delete mapRenderer;
		mapRenderer = NULL;
	}

	for (BspRenderer* arrangeRenderer : arrangeBsps) {
		delete arrangeRenderer;
	}
	arrangeBsps.clear();

	pickInfo = PickInfo();

	if (entConnections) {
		delete entConnections;
		delete entConnectionPoints;
		entConnections = NULL;
		entConnectionPoints = NULL;
		entConnectionLinks.clear();
	}

	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (undoLumpState.lumps[i]) {
			delete[] undoLumpState.lumps[i];
		}
	}
	memset(&undoLumpState, 0, sizeof(LumpState));

	forceAngleRotation = false; // can cause confusion opening a new map
}

void Editor::reloadMaps() {
	if (!g_app->confirmMapExit()) {
		return;
	}

	string reloadPath = mapRenderer->map->path;

	clearMapData();
	addMap(new Bsp(reloadPath));

	updateEntConnections();

	logf("Reloaded maps\n");
}

void Editor::openMap(const char* fpath) {
	if (!g_app->confirmMapExit()) {
		return;
	}

	if (!fpath) {
		fpath = gui->openMap();

		if (!fpath)
			return;
	}
	if (!fileExists(fpath)) {
		logf("File does not exist: %s\n", fpath);
		return;
	}

	if (isLoading) {
		logf("Delayed loading of dropped map until current map load finishes.\n");
		logf("%s\n", fpath);
		openMapAfterLoad = fpath;
		return;
	}

	Bsp* map = new Bsp(fpath);
	openMapAfterLoad = "";

	if (!map->valid) {
		delete map;
		logf("Failed to load map (not a valid BSP file): %s\n", fpath);
		return;
	}

	openMap(map);
}

void Editor::openMap(Bsp* map) {
	for (BspRenderer* render : arrangeBsps) {
		if (render->map == map) {
			render->map = NULL; // don't delete the map about to be opened;
		}
	}

	clearMapData();
	addMap(map);

	gui->refresh();
	updateCullBox();
}

void Editor::saveSettings() {
	g_settings.debug_open = gui->widgets[WIDGET_DEBUG]->widgetVisible;
	g_settings.keyvalue_open = gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible;
	g_settings.transform_open = gui->widgets[WIDGET_TRANSFORM]->widgetVisible;
	g_settings.log_open = gui->widgets[WIDGET_MESSAGES]->widgetVisible;
	g_settings.settings_open = gui->widgets[WIDGET_SETTINGS]->widgetVisible;
	g_settings.limits_open = gui->widgets[WIDGET_LIMITS]->widgetVisible;
	g_settings.entreport_open = gui->widgets[WIDGET_ENT_REPORT]->widgetVisible;
	g_settings.leafgraph_open = gui->widgets[WIDGET_LEAF]->widgetVisible;
	g_settings.settings_tab = gui->settingsTab;
	g_settings.vsync = gui->vsync;
	g_settings.show_transform_axes = showDragAxes;
	g_settings.verboseLogs = g_verbose;
	g_settings.zfar = zFar;
	g_settings.fov = fov;
	g_settings.render_flags = g_settings.render_flags;
	g_settings.undoLevels = undoLevels;
	g_settings.moveSpeed = moveSpeed;
	g_settings.rotSpeed = rotationSpeed;
}

void Editor::loadWidgetStates() {
	gui->widgets[WIDGET_DEBUG]->widgetVisible = g_settings.debug_open;
	gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible = g_settings.keyvalue_open;
	gui->widgets[WIDGET_TRANSFORM]->widgetVisible = g_settings.transform_open;
	gui->widgets[WIDGET_MESSAGES]->widgetVisible = g_settings.log_open;
	gui->widgets[WIDGET_SETTINGS]->widgetVisible = g_settings.settings_open;
	gui->widgets[WIDGET_LIMITS]->widgetVisible = g_settings.limits_open;
	gui->widgets[WIDGET_ENT_REPORT]->widgetVisible = g_settings.entreport_open;
	gui->widgets[WIDGET_LEAF]->widgetVisible = g_settings.leafgraph_open;

	gui->settingsTab = g_settings.settings_tab;
	gui->openSavedTabs = true;
	gui->vsync = g_settings.vsync;
	modelRenderer->renderDist = g_settings.zFarMdl;

	glfwSwapInterval(gui->vsync ? 1 : 0);
}

void Editor::loadSettings() {
	showDragAxes = g_settings.show_transform_axes;
	g_verbose = g_settings.verboseLogs;
	zFar = g_settings.zfar;
	fov = g_settings.fov;
	g_settings.render_flags = g_settings.render_flags;
	undoLevels = g_settings.undoLevels;
	rotationSpeed = g_settings.rotSpeed;
	moveSpeed = g_settings.moveSpeed;

	if (!showDragAxes) {
		transformMode = TRANSFORM_NONE;
	}
}

void Editor::loadFgds() {
	Fgd* mergedFgd = NULL;

	vector<Fgd*> fgds;

	for (int i = 0; i < g_settings.fgdPaths.size(); i++) {
		string path = g_settings.fgdPaths[i];

		g_parsed_fgds.clear();
		g_parsed_fgds.insert(path);

		string loadPath = findAsset(path);
		if (loadPath.empty()) {
			warnf("Missing FGD: %s\n", path.c_str());
			continue;
		}

		Fgd* tmp = new Fgd(loadPath);
		if (!tmp->parse())
		{
			tmp->path = g_settings.gamedir + g_settings.fgdPaths[i];
			if (!tmp->parse())
			{
				continue;
			}
		}

		if (i == 0 || mergedFgd == NULL) {
			mergedFgd = new Fgd("<All FGDs>");
			mergedFgd->merge(tmp);
		}
		else {
			mergedFgd->merge(tmp);
		}
		fgds.push_back(tmp);
	}

	swapPointEntRenderer = new PointEntRenderer(mergedFgd, fgds);
}

void Editor::applyTransform(bool forceUpdate) {
	if (!isTransformableSolid || modelUsesSharedStructures) {
		return;
	}

	if (pickInfo.getModelIndex() > 0 && pickMode == PICK_OBJECT) {
		bool transformingVerts = transformTarget == TRANSFORM_VERTEX;
		bool scalingObject = transformTarget == TRANSFORM_OBJECT && transformMode == TRANSFORM_SCALE;
		bool movingOrigin = transformTarget == TRANSFORM_ORIGIN;
		bool actionIsUndoable = false;

		bool anyVertsChanged = false;
		for (int i = 0; i < modelVerts.size(); i++) {
			if (modelVerts[i].pos != modelVerts[i].startPos || modelVerts[i].pos != modelVerts[i].undoPos) {
				anyVertsChanged = true;
			}
		}

		if (anyVertsChanged && (transformingVerts || scalingObject || forceUpdate)) {

			invalidSolid = !pickInfo.getMap()->vertex_manipulation_sync(pickInfo.getModelIndex(), modelVerts, false, true);
			gui->reloadLimits();

			for (int i = 0; i < modelVerts.size(); i++) {
				modelVerts[i].startPos = modelVerts[i].pos;
				if (!invalidSolid) {
					modelVerts[i].undoPos = modelVerts[i].pos;
				}
			}
			for (int i = 0; i < modelFaceVerts.size(); i++) {
				modelFaceVerts[i].startPos = modelFaceVerts[i].pos;
				if (!invalidSolid) {
					modelFaceVerts[i].undoPos = modelFaceVerts[i].pos;
				}
			}

			if (scalingObject) {
				for (int i = 0; i < scaleTexinfos.size(); i++) {
					BSPTEXTUREINFO& info = pickInfo.getMap()->texinfos[scaleTexinfos[i].texinfoIdx];
					scaleTexinfos[i].oldShiftS = info.shiftS;
					scaleTexinfos[i].oldShiftT = info.shiftT;
					scaleTexinfos[i].oldS = info.vS;
					scaleTexinfos[i].oldT = info.vT;
				}
			}

			actionIsUndoable = !invalidSolid;
		}

		int modelIdx = pickInfo.getModelIndex();
		if (movingOrigin && modelIdx >= 0) {
			if (oldOrigin != transformedOrigin) {
				vec3 delta = transformedOrigin - oldOrigin;

				g_progress.hide = true;
				pickInfo.getMap()->move(delta*-1, modelIdx);
				g_progress.hide = false;

				oldOrigin = transformedOrigin;
				mapRenderer->refreshModel(modelIdx);

				for (int i = 0; i < pickInfo.getMap()->ents.size(); i++) {
					Entity* ent = pickInfo.getMap()->ents[i];
					if (ent->getBspModelIdx() == modelIdx) {
						ent->setOrAddKeyvalue("origin", (ent->getOrigin() + delta).toKeyvalueString());
						mapRenderer->refreshEnt(i);
					}
				}
				
				updateModelVerts();
				//mapRenderers[pickInfo.mapIdx]->reloadLightmaps();

				actionIsUndoable = true;
			}
		}

		if (actionIsUndoable) {
			pushModelUndoState("Edit BSP Model", EDIT_MODEL_LUMPS);
		}
	}
}

void Editor::pickObject(bool boxSelect) {
	vec3 pickStart, pickDir;
	getPickRay(pickStart, pickDir);

	bool multiselect = anyCtrlPressed;

	if (!multiselect) {
		// deselect old faces
		mapRenderer->highlightPickedFaces(false);

		// update deselected point ents
		for (int entIdx : pickInfo.ents) {
			Entity* ent = pickInfo.getMap()->ents[entIdx];
			if (!ent->isBspModel()) {
				mapRenderer->refreshPointEnt(entIdx, false);
			}
		}
		mapRenderer->pointEnts->deleteBuffer();
		mapRenderer->pointEnts->upload();
	}

	unordered_set<int> boxSelectEnts, boxSelectFaces, boxSelectLeaves;
	int oldEntIdx = pickInfo.getEntIndex();
	int clickedEnt = -1, clickedFace = -1, clickedLeaf = -1;
	float bestDist = FLT_MAX;

	if (mapArrangeMode) {
		if (boxSelect) {
			Frustum pickFrustum = getPickFrustum();
			for (int i = 0; i < arrangeBsps.size(); i++) {
				unordered_set<int> ents, faces, leaves;
				arrangeBsps[i]->pickFrustum(pickFrustum, ents, faces, leaves, clipnodeRenderHull);
				if (ents.size() || faces.size())
					boxSelectEnts.insert(i + 1);
			}
		}
		else {
			int bestMapPick = -1;
			for (int i = 0; i < arrangeBsps.size(); i++) {
				if (arrangeBsps[i]->pickPoly(pickStart, pickDir, clipnodeRenderHull, clickedEnt, clickedFace, clickedLeaf, bestDist)) {
					bestMapPick = i;
				}
			}

			if (bestMapPick != -1) {
				clickedEnt = bestMapPick + 1;
			}
		}
	}
	else {
		if (boxSelect) {
			Frustum frustum = getPickFrustum();
			mapRenderer->pickFrustum(frustum, boxSelectEnts, boxSelectFaces, boxSelectLeaves, clipnodeRenderHull);
			boxSelectFaces.erase(-1); // erase clipnode "faces"
		}
		else {
			mapRenderer->pickPoly(pickStart, pickDir, clipnodeRenderHull, clickedEnt, clickedFace, clickedLeaf, bestDist);
		}
	}

	if (movingEnt && oldEntIdx != pickInfo.getEntIndex()) {
		ungrabEnts();
	}

	if (pickInfo.getModelIndex() >= 0) {
		//pickInfo.map->print_model_hull(pickInfo.modelIdx, 0);
	}
	else {
		if (transformMode == TRANSFORM_SCALE)
			transformMode = TRANSFORM_MOVE;
		transformTarget = TRANSFORM_OBJECT;
	}

	if (pickMode == PICK_OBJECT) {
		pushEntityUndoState("Edit Keyvalues");

		if (movingEnt) {
			ungrabEnts();
		}
		if (multiselect) {
			if (boxSelect) {
				for (int idx : boxSelectEnts) {
					pickInfo.selectEnt(idx);
				}
				pickInfo.deselectEnt(0);
			}
			else if (pickInfo.isEntSelected(clickedEnt)) {
				pickInfo.deselectEnt(clickedEnt);
				Entity* ent = pickInfo.getMap()->ents[clickedEnt];
				if (!ent->isBspModel()) {
					mapRenderer->refreshPointEnt(clickedEnt);
				}
			}
			else if (clickedEnt > 0) {
				pickInfo.deselectEnt(0); // don't allow worldspawn in multi selections
				pickInfo.selectEnt(clickedEnt);
			}
		}
		else {
			if (movingEnt)
				ungrabEnts();
			pickInfo.deselect();

			if (boxSelect) {
				for (int idx : boxSelectEnts) {
					pickInfo.selectEnt(idx);
				}
				pickInfo.deselectEnt(0);
			}
			else if (clickedEnt != -1) {
				pickInfo.selectEnt(clickedEnt);
			}
		}
		//logf("%d selected ents\n", pickInfo.ents.size());		

		if (pickInfo.getEnt()) {
			updateModelVerts();
			if (pickInfo.getEnt() && pickInfo.getEnt()->isBspModel())
				saveLumpState(pickInfo.getMap(), 0xffffffff, true);
			pickCount++; // force transform window update
		}

		isTransformableSolid = pickInfo.ents.size() == 1;
		if (isTransformableSolid) {
			for (int idx : pickInfo.getModelIndexes()) {
				isTransformableSolid = pickInfo.getMap()->is_convex(pickInfo.getModelIndex());
				if (!isTransformableSolid)
					break;
			}
		}
	}
	else if (pickMode == PICK_FACE) {
		if (multiselect) {
			mapRenderer->highlightPickedFaces(false);
			if (boxSelect) {
				for (int idx : boxSelectFaces) {
					pickInfo.selectFace(idx);
				}
			}
			else if (pickInfo.isFaceSelected(clickedFace)) {
				pickInfo.deselectFace(clickedFace);
			}
			else if (clickedFace != -1) {
				pickInfo.selectFace(clickedFace);
			}
			mapRenderer->highlightPickedFaces(true);
		}
		else {
			mapRenderer->highlightPickedFaces(false);
			pickInfo.deselect();

			if (boxSelect) {
				for (int idx : boxSelectFaces) {
					pickInfo.selectFace(idx);
				}
			}
			else if (clickedFace != -1) {
				pickInfo.selectFace(clickedFace);
			}
			mapRenderer->highlightPickedFaces(true);
		}
		//logf("%d selected faces\n", pickInfo.faces.size());
		
		gui->lightmapEditorNeedsUpdate = true;
	}
	else if (pickMode == PICK_LEAF) {
		mapRenderer->highlightPickedFaces(false);
		mapRenderer->highlightPickedLeaves(false);

		if (multiselect) {
			if (boxSelect) {
				for (int idx : boxSelectLeaves) {
					pickInfo.selectLeaf(idx);
				}
			}
			else if (pickInfo.isLeafSelected(clickedLeaf)) {
				pickInfo.deselectLeaf(clickedLeaf);
			}
			else if (clickedLeaf != -1) {
				pickInfo.selectLeaf(clickedLeaf);
			}
		}
		else {
			pickInfo.deselect();

			if (boxSelect) {
				for (int idx : boxSelectLeaves) {
					pickInfo.selectLeaf(idx);
				}
			}
			else if (clickedLeaf != -1) {
				pickInfo.selectLeaf(clickedLeaf);
			}
		}

		pickInfo.selectLeafFaces();
		mapRenderer->highlightPickedFaces(true);
		mapRenderer->highlightPickedLeaves(true);
		((LeafWidget*)gui->widgets[WIDGET_LEAF])->selectLeaves(pickInfo.leaves);
	}

	postSelectEnt();
}

vec3 Editor::getMoveDir()
{
	mat4x4 rotMat;
	rotMat.loadIdentity();
	rotMat.rotateX(PI * cameraAngles.x / 180.0f);
	rotMat.rotateZ(PI * cameraAngles.z / 180.0f);

	vec3 forward, right, up;
	vec3 moveAngles = cameraAngles;
	moveAngles.y = 0;
	makeVectors(moveAngles, forward, right, up);


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

void Editor::getPickRay(vec3& start, vec3& pickDir) {
	int xpos, ypos;
	getMousePos(xpos, ypos);
	return getPickRay(vec2(xpos, ypos), start, pickDir);
}

void Editor::getPickRay(vec2 mousePos, vec3& start, vec3& pickDir) {
	// invert ypos
	int wWidth = viewportFbo ? viewportFbo->width : windowWidth;
	int wHeight = viewportFbo ? viewportFbo->height : windowHeight;
	mousePos.y = wHeight - mousePos.y;

	// translate mouse coordinates so that the origin lies in the center and is a scaler from +/-1.0
	float mouseX = ((mousePos.x / (double)wWidth) * 2.0f) - 1.0f;
	float mouseY = ((mousePos.y / (double)wHeight) * 2.0f) - 1.0f;

	// http://schabby.de/picking-opengl-ray-tracing/
	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);

	vec3 view = forward.normalize(1.0f);
	vec3 h = crossProduct(view, up).normalize(1.0f); // 3D float vector
	vec3 v = crossProduct(h, view).normalize(1.0f); // 3D float vector

	// convert fovy to radians 
	float rad = fov * PI / 180.0f;
	float vLength = tan(rad / 2.0f) * zNear;
	float hLength = vLength * (wWidth / (float)wHeight);

	v *= vLength;
	h *= hLength;

	// linear combination to compute intersection of picking ray with view port plane
	start = cameraOrigin + view * zNear + h * mouseX + v * mouseY;

	// compute direction of picking ray by subtracting intersection point with camera position
	pickDir = (start - cameraOrigin).normalize(1.0f);
}

Frustum Editor::getPickFrustum() {
	vec3 rayOrigin[4];
	vec3 rayDir[4];

	vec2 min = vec2(std::min(boxSelectStart.x, boxSelectEnd.x), std::min(boxSelectStart.y, boxSelectEnd.y));
	vec2 max = vec2(std::max(boxSelectStart.x, boxSelectEnd.x), std::max(boxSelectStart.y, boxSelectEnd.y));

	vec2 boxSelectCorners[4] = {
		min,
		vec2(max.x, min.y),
		max,
		vec2(min.x, max.y),
	};

	for (int i = 0; i < 4; i++) {
		getPickRay(boxSelectCorners[i], rayOrigin[i], rayDir[i]);
		//rayDir[i] = rayDir[i]*-1;
	}
	
	Frustum f;
	f.origin = cameraOrigin;
	f.planes[0] = crossProduct(rayDir[1], rayDir[2]).normalize();
	f.planes[1] = crossProduct(rayDir[3], rayDir[0]).normalize();
	f.planes[2] = crossProduct(rayDir[0], rayDir[1]).normalize();
	f.planes[3] = crossProduct(rayDir[2], rayDir[3]).normalize();

	return f;
}

void Editor::addMap(Bsp* map) {
	g_settings.addRecentFile(map->path);
	g_settings.save(); // in case the program crashes

	autoSelectEngine(map, false);

	delete navRenderer;
	navRenderer = new NavRenderer();

	mapRenderer = new BspRenderer(map, pointEntRenderer);

	glCheckError("creating BSP renderer");

	gui->checkValidHulls();

	// Pick default map
	//if (!pickInfo.map) 
	{
		pickInfo.deselect();

		if (map->ents.size())
			pickInfo.selectEnt(0);
		/*
		* TODO: move camera to center of map
		// Move camera to first entity with origin
		for(auto const & ent : map->ents)
		{
			if (ent->getOrigin() != vec3())
			{
				cameraOrigin = ent->getOrigin();
				break;
			}
		}
		*/
	}

	updateCullBox();

	updateWindowTitle();

	emptyMapLoaded = false;

	const char* fmt = map->lastLoadformat >= 0 ? g_bsp_format_names[map->lastLoadformat] : "BSP";
	logf("Loaded %s: %s\n", fmt, map->path.c_str());

	glCheckError("add map");
}

vec3 Editor::getAxisDragPoint(vec3 origin) {
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

void Editor::updateSelectionSize() {
	selectionSize = vec3();

	if (!pickInfo.getEnt() || !pickInfo.getMap()) {
		return;
	}

	int modelIdx = pickInfo.getModelIndex();

	if (modelIdx == 0) {
		vec3 mins, maxs;
		pickInfo.getMap()->get_bounding_box(mins, maxs);
		selectionSize = maxs - mins;
	}
	else {
		vec3 combinedMins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
		vec3 combinedMaxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		for (int i = 0; i < pickInfo.ents.size(); i++) {
			Entity* ent = pickInfo.getMap()->ents[pickInfo.ents[i]];
			vec3 ori = ent->getOrigin();
			modelIdx = ent->getBspModelIdx();

			if (modelIdx > 0 && modelIdx < pickInfo.getMap()->modelCount) {
				vec3 mins, maxs;
				if (pickInfo.getMap()->models[modelIdx].nFaces == 0) {
					mins = pickInfo.getMap()->models[modelIdx].nMins;
					maxs = pickInfo.getMap()->models[modelIdx].nMaxs;
				}
				else {
					pickInfo.getMap()->get_model_vertex_bounds(modelIdx, mins, maxs);
				}
				expandBoundingBox(ori + maxs, combinedMins, combinedMaxs);
				expandBoundingBox(ori + mins, combinedMins, combinedMaxs);
			}
			else {
				EntCube* cube = pointEntRenderer->getEntCube(pickInfo.getEnt());
				if (cube) {
					expandBoundingBox(ori + cube->maxs, combinedMins, combinedMaxs);
					expandBoundingBox(ori + cube->mins, combinedMins, combinedMaxs);
				}
			}
		}

		selectionSize = combinedMaxs - combinedMins;
	}
}

void Editor::updateCullBox() {
	if (!mapRenderer) {
		hasCullbox = false;
		return;
	}

	Bsp* map = mapRenderer->map;

	cullMins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	cullMaxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	int findCount = 0;
	for (Entity* ent : map->ents) {
		if (ent->getClassname() == "cull") {
			expandBoundingBox(ent->getOrigin(), cullMins, cullMaxs);
			findCount++;
		}
	}

	hasCullbox = findCount > 1;
}

bool Editor::getModelSolid(vector<TransformVert>& hullVerts, Bsp* map, Solid& outSolid) {
	outSolid.faces.clear();
	outSolid.hullEdges.clear();
	outSolid.hullVerts.clear();
	outSolid.hullVerts = hullVerts;

	// get verts for each plane
	std::unordered_map<int, vector<int>> planeVerts;
	for (int i = 0; i < hullVerts.size(); i++) {
		for (int k = 0; k < hullVerts[i].iPlanes.size(); k++) {
			int iPlane = hullVerts[i].iPlanes[k];
			planeVerts[iPlane].push_back(i);
		}
	}

	vec3 centroid = getCentroid(hullVerts);

	// sort verts CCW on each plane to get edges
	for (auto it = planeVerts.begin(); it != planeVerts.end(); ++it) {
		int iPlane = it->first;
		vector<int> verts = it->second;
		BSPPLANE& plane = map->planes[iPlane];
		if (verts.size() < 2) {
			logf("Plane with less than 2 verts!?\n"); // hl_c00 pipe in green water place
			return false;
		}

		vector<vec3> tempVerts(verts.size());
		for (int i = 0; i < verts.size(); i++) {
			tempVerts[i] = hullVerts[verts[i]].pos;
		}

		vector<int> orderedVerts = getSortedPlanarVertOrder(tempVerts);
		for (int i = 0; i < orderedVerts.size(); i++) {
			orderedVerts[i] = verts[orderedVerts[i]];
			tempVerts[i] = hullVerts[orderedVerts[i]].pos;
		}

		Face face;
		face.plane = plane;

		vec3 orderedVertsNormal = getNormalFromVerts(&tempVerts[0], tempVerts.size());

		// get plane normal, flipping if it points inside the solid
		vec3 faceNormal = plane.vNormal;
		vec3 planeDir = ((plane.vNormal * plane.fDist) - centroid).normalize();
		face.planeSide = 1;
		if (dotProduct(planeDir, plane.vNormal) > 0) {
			faceNormal = faceNormal.invert();
			face.planeSide = 0;
		}

		// reverse vert order if not CCW when viewed from outside the solid
		if (dotProduct(orderedVertsNormal, faceNormal) < 0) {
			reverse(orderedVerts.begin(), orderedVerts.end());
		}

		for (int i = 0; i < orderedVerts.size(); i++) {
			face.verts.push_back(orderedVerts[i]);
		}
		face.iTextureInfo = 1; // TODO
		outSolid.faces.push_back(face);

		for (int i = 0; i < orderedVerts.size(); i++) {
			HullEdge edge;
			edge.verts[0] = orderedVerts[i];
			edge.verts[1] = orderedVerts[(i + 1) % orderedVerts.size()];
			edge.selected = false;

			// find the planes that this edge joins
			vec3 midPoint = getEdgeControlPoint(hullVerts, edge);
			int planeCount = 0;
			for (auto it2 = planeVerts.begin(); it2 != planeVerts.end(); ++it2) {
				int iPlane = it2->first;
				BSPPLANE& p = map->planes[iPlane];
				float dist = dotProduct(midPoint, p.vNormal) - p.fDist;
				if (fabs(dist) < EPSILON) {
					edge.planes[planeCount % 2] = iPlane;
					planeCount++;
				}
			}
			if (planeCount != 2) {
				errorf("ERROR: Edge connected to %d planes!\n", planeCount);
				return false;
			}

			outSolid.hullEdges.push_back(edge);
		}
	}

	return true;
}

void Editor::scaleSelectedObject(float x, float y, float z) {
	vec3 minDist;
	vec3 maxDist;

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

	vec3 dir;
	dir.x = (distRange.x * x) - distRange.x;
	dir.y = (distRange.y * y) - distRange.y;
	dir.z = (distRange.z * z) - distRange.z;

	scaleSelectedObject(dir, vec3());
}

void Editor::scaleSelectedObject(vec3 dir, vec3 fromDir) {
	if (!pickInfo.getEnt() || pickInfo.getModelIndex() <= 0)
		return;

	Bsp* map = mapRenderer->map;

	bool scaleFromOrigin = fromDir.x == 0 && fromDir.y == 0 && fromDir.z == 0;

	vec3 minDist = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	vec3 maxDist = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int i = 0; i < modelVerts.size(); i++) {
		expandBoundingBox(modelVerts[i].startPos, minDist, maxDist);
	}
	for (int i = 0; i < modelFaceVerts.size(); i++) {
		expandBoundingBox(modelFaceVerts[i].startPos, minDist, maxDist);
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

	// scale planes
	for (int i = 0; i < modelVerts.size(); i++) {
		vec3 stretchFactor = (modelVerts[i].startPos - scaleFromDist) / distRange;
		modelVerts[i].pos = modelVerts[i].startPos + dir * stretchFactor;
		if (gridSnappingEnabled) {
			modelVerts[i].pos = snapToGrid(modelVerts[i].pos);
		}
	}

	// scale visible faces
	for (int i = 0; i < modelFaceVerts.size(); i++) {
		vec3 stretchFactor = (modelFaceVerts[i].startPos - scaleFromDist) / distRange;
		modelFaceVerts[i].pos = modelFaceVerts[i].startPos + dir * stretchFactor;
		if (gridSnappingEnabled) {
			modelFaceVerts[i].pos = snapToGrid(modelFaceVerts[i].pos);
		}
		if (modelFaceVerts[i].ptr) {
			*modelFaceVerts[i].ptr = modelFaceVerts[i].pos;
		}
	}

	// update planes for picking
	invalidSolid = !pickInfo.getMap()->vertex_manipulation_sync(pickInfo.getModelIndex(), modelVerts, false, false);

	updateSelectionSize();

	//
	// TODO: I have no idea what I'm doing but this code scales axis-aligned texture coord axes correctly.
	//       Rewrite all of this after understanding texture axes.
	//

	if (!textureLock)
		return;

	minDist = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	maxDist = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	
	for (int i = 0; i < modelFaceVerts.size(); i++) {
		expandBoundingBox(modelFaceVerts[i].pos, minDist, maxDist);
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

void Editor::moveSelectedVerts(vec3 delta) {
	for (int i = 0; i < modelVerts.size(); i++) {
		if (modelVerts[i].selected) {
			modelVerts[i].pos = modelVerts[i].startPos + delta;
			if (gridSnappingEnabled)
				modelVerts[i].pos = snapToGrid(modelVerts[i].pos);
			if (modelVerts[i].ptr)
				*modelVerts[i].ptr = modelVerts[i].pos;
		}
	}

	invalidSolid = !pickInfo.getMap()->vertex_manipulation_sync(pickInfo.getModelIndex(), modelVerts, true, false);
	mapRenderer->refreshModel(pickInfo.getModelIndex());
}

void Editor::splitFace() {
	Bsp* map = pickInfo.getMap();

	// find the pseudo-edge to split with
	vector<int> selectedEdges;
	for (int i = 0; i < modelEdges.size(); i++) {
		if (modelEdges[i].selected) {
			selectedEdges.push_back(i);
		}
	}

	if (selectedEdges.size() != 2) {
		logf("Exactly 2 edges must be selected before splitting a face\n");
		return;
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
		return;
	}

	BSPPLANE& splitPlane = pickInfo.getMap()->planes[commonPlane];
	vec3 splitPoints[2] = {
		getEdgeControlPoint(modelVerts, edge1),
		getEdgeControlPoint(modelVerts, edge2)
	};

	vector<int> modelPlanes;
	BSPMODEL& model = map->models[pickInfo.getModelIndex()];
	pickInfo.getMap()->getNodePlanes(model.iHeadnodes[0], modelPlanes);

	// find the plane being split
	int commonPlaneIdx = -1;
	for (int i = 0; i < modelPlanes.size(); i++) {
		if (modelPlanes[i] == commonPlane) {
			commonPlaneIdx = i;
			break;
		}
	}
	if (commonPlaneIdx == -1) {
		logf("Failed to find splitting plane");
		return;
	}

	// extrude split points so that the new planes aren't coplanar
	{
		int i0 = edge1.verts[0];
		int i1 = edge1.verts[1];
		int i2 = edge2.verts[0];
		if (i2 == i1 || i2 == i0)
			i2 = edge2.verts[1];

		vec3 v0 = modelVerts[i0].pos;
		vec3 v1 = modelVerts[i1].pos;
		vec3 v2 = modelVerts[i2].pos;

		vec3 e1 = (v1 - v0).normalize();
		vec3 e2 = (v2 - v0).normalize();
		vec3 normal = crossProduct(e1, e2).normalize();

		vec3 centroid = getCentroid(modelVerts);
		vec3 faceDir = (centroid - v0).normalize();
		if (dotProduct(faceDir, normal) > 0) {
			normal *= -1;
		}

		for (int i = 0; i < 2; i++)
			splitPoints[i] += normal*4;
	}

	// replace split plane with 2 new slightly-angled planes
	{
		vec3 planeVerts[2][3] = {
			{
				splitPoints[0],
				modelVerts[edge1.verts[1]].pos,
				splitPoints[1]
			},
			{
				splitPoints[0],
				splitPoints[1],
				modelVerts[edge1.verts[0]].pos
			}
		};

		modelPlanes.erase(modelPlanes.begin() + commonPlaneIdx);
		for (int i = 0; i < 2; i++) {
			vec3 e1 = (planeVerts[i][1] - planeVerts[i][0]).normalize();
			vec3 e2 = (planeVerts[i][2] - planeVerts[i][0]).normalize();
			vec3 normal = crossProduct(e1, e2).normalize();

			int newPlaneIdx = map->create_plane();
			BSPPLANE& plane = map->planes[newPlaneIdx];
			plane.update(normal, getDistAlongAxis(normal, planeVerts[i][0]));
			modelPlanes.push_back(newPlaneIdx);
		}
	}

	// create a new model from the new set of planes
	vector<TransformVert> newHullVerts;
	if (!map->getModelPlaneIntersectVerts(pickInfo.getModelIndex(), modelPlanes, newHullVerts)) {
		logf("Can't split here because the model would not be convex\n");
		return;
	}

	Solid newSolid;
	if (!getModelSolid(newHullVerts, pickInfo.getMap(), newSolid)) {
		logf("Splitting here would invalidate the solid\n");
		return;
	}

	// test that all planes have at least 3 verts
	{
		std::unordered_map<int, vector<vec3>> planeVerts;
		for (int i = 0; i < newHullVerts.size(); i++) {
			for (int k = 0; k < newHullVerts[i].iPlanes.size(); k++) {
				int iPlane = newHullVerts[i].iPlanes[k];
				planeVerts[iPlane].push_back(newHullVerts[i].pos);
			}
		}
		for (auto it = planeVerts.begin(); it != planeVerts.end(); ++it) {
			vector<vec3>& verts = it->second;

			if (verts.size() < 3) {
				logf("Can't split here because a face with less than 3 verts would be created\n");
				return;
			}
		}
	}

	// copy textures/UVs from the old model
	{
		BSPMODEL& oldModel = map->models[pickInfo.getModelIndex()];
		for (int i = 0; i < newSolid.faces.size(); i++) {
			Face& solidFace = newSolid.faces[i];
			BSPFACE* bestMatch = NULL;
			float bestdot = -FLT_MAX;
			for (int k = 0; k < oldModel.nFaces; k++) {
				BSPFACE& bspface = map->faces[oldModel.iFirstFace + k];
				BSPPLANE& plane = map->planes[bspface.iPlane];
				vec3 bspFaceNormal = bspface.nPlaneSide ? plane.vNormal.invert() : plane.vNormal;
				vec3 solidFaceNormal = solidFace.planeSide ? solidFace.plane.vNormal.invert() : solidFace.plane.vNormal;
				float dot = dotProduct(bspFaceNormal, solidFaceNormal);
				if (dot > bestdot) {
					bestdot = dot;
					bestMatch = &bspface;
				}
			}
			if (bestMatch != NULL) {
				solidFace.iTextureInfo = bestMatch->iTextureInfo;
			}
		}
	}

	int modelIdx = map->create_solid(newSolid, pickInfo.getModelIndex());

	for (int i = 0; i < modelVerts.size(); i++) {
		modelVerts[i].selected = false;
	}
	for (int i = 0; i < modelEdges.size(); i++) {
		modelEdges[i].selected = false;
	}

	pushModelUndoState("Split Face", EDIT_MODEL_LUMPS);

	mapRenderer->updateLightmapInfos();
	mapRenderer->calcFaceMaths();
	mapRenderer->refreshModel(modelIdx);
	updateModelVerts();

	gui->reloadLimits();
}

void Editor::scaleSelectedVerts(float x, float y, float z) {

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);
	vec3 fromOrigin = activeAxes.origin;

	vec3 min(FLT_MAX, FLT_MAX, FLT_MAX);
	vec3 max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
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

	invalidSolid = !pickInfo.getMap()->vertex_manipulation_sync(pickInfo.getModelIndex(), modelVerts, true, false);
	mapRenderer->refreshModel(pickInfo.getModelIndex());
	updateSelectionSize();
}

vec3 Editor::getEdgeControlPoint(vector<TransformVert>& hullVerts, HullEdge& edge) {
	vec3 v0 = hullVerts[edge.verts[0]].pos;
	vec3 v1 = hullVerts[edge.verts[1]].pos;
	return v0 + (v1 - v0) * 0.5f;
}

vec3 Editor::getCentroid(vector<TransformVert>& hullVerts) {
	vec3 centroid;
	for (int i = 0; i < hullVerts.size(); i++) {
		centroid += hullVerts[i].pos;
	}
	return centroid / (float)hullVerts.size();
}

vec3 Editor::snapToGrid(vec3 pos) {
	float snapSize = pow(2.0, gridSnapLevel);
	float halfSnap = snapSize * 0.5f;
	
	int x = round((pos.x) / snapSize) * snapSize;
	int y = round((pos.y) / snapSize) * snapSize;
	int z = round((pos.z) / snapSize) * snapSize;

	return vec3(x, y, z);
}

void Editor::hideSelectedLeaves() {
	for (int idx : pickInfo.leaves) {
		hiddenLeaves.insert(idx);
	}

	mapRenderer->highlightPickedFaces(false);
	pickInfo.deselect();
	updateTextureAxes();

	mapRenderer->hideLeaves(true);
}

void Editor::unhideLeaves() {
	hiddenLeaves.clear();
	mapRenderer->hideLeaves(false);
	mapRenderer->highlightPickedFaces(false);
	mapRenderer->highlightPickedLeaves(false);
	pickInfo.deselect();
}

void Editor::hideSelectedFaces() {
	for (int idx : pickInfo.faces) {
		hiddenFaces.insert(idx);
	}

	mapRenderer->highlightPickedFaces(false);
	mapRenderer->hideFaces(true);
	pickInfo.deselect();
	updateTextureAxes();

	mapRenderer->hideLeaves(true);
}

void Editor::unhideFaces() {
	mapRenderer->hideFaces(false);
	hiddenFaces.clear();
	mapRenderer->highlightPickedFaces(false);
	pickInfo.deselect();
	updateTextureAxes();
}

void Editor::deselectFaces() {
	mapRenderer->highlightPickedFaces(false);
	pickInfo.deselect();
}

void Editor::goToCoords(float x, float y, float z)
{
	cameraOrigin.x = x;
	cameraOrigin.y = y;
	cameraOrigin.z = z;
}

void Editor::goToEnt(Bsp* map, int entIdx) {
	Entity* ent = map->ents[entIdx];

	vec3 size;
	if (ent->isBspModel()) {
		BSPMODEL& model = map->models[ent->getBspModelIdx()];
		size = (model.nMaxs - model.nMins) * 0.5f;
	}
	else {
		EntCube* cube = pointEntRenderer->getEntCube(ent);
		size = cube->maxs - cube->mins * 0.5f;
	}

	cameraOrigin = getEntOrigin(map, ent) - cameraForward * (size.length() + 64.0f);
}

void Editor::goToFace(Bsp* map, int faceIdx) {

	int modelIdx = 0;
	for (int i = 0; i < map->modelCount; i++) {
		BSPMODEL& model = map->models[i];
		if (model.iFirstFace <= faceIdx && model.iFirstFace + model.nFaces > faceIdx) {
			modelIdx = i;
			break;
		}
	}

	vec3 offset = mapRenderer->mapOffset;
	for (int i = 0; i < map->ents.size(); i++) {
		if (map->ents[i]->getBspModelIdx() == modelIdx) {
			offset += map->ents[i]->getOrigin();
		}
	}

	BSPFACE& face = map->faces[faceIdx];

	vec3 mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	vec3 maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = map->edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

		expandBoundingBox(map->verts[vertIdx], mins, maxs);
	}
	vec3 size = maxs - mins;
	vec3 center = (mins + maxs) * 0.5f;

	cameraOrigin = (offset + center) - cameraForward * (size.length() + 64.0f);
}

void Editor::saveLumpState(Bsp* map, int targetLumps, bool deleteOldState) {
	if (deleteOldState) {
		for (int i = 0; i < HEADER_LUMPS; i++) {
			if (undoLumpState.lumps[i])
				delete[] undoLumpState.lumps[i];
		}
	}

	undoLumpState = map->duplicate_lumps(targetLumps);
}

void Editor::pushModelUndoState(string actionDesc, int targetLumps) {
	if (!pickInfo.getEnt() || pickInfo.getModelIndex() <= 0) {
		return;
	}
	
	LumpState newLumps = pickInfo.getMap()->duplicate_lumps(targetLumps);

	bool differences[HEADER_LUMPS] = { false };

	bool anyDifference = false;
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (newLumps.lumps[i] && undoLumpState.lumps[i]) {
			if (newLumps.lumpLen[i] != undoLumpState.lumpLen[i] || memcmp(newLumps.lumps[i], undoLumpState.lumps[i], newLumps.lumpLen[i]) != 0) {
				anyDifference = true;
				differences[i] = true;
			}
		}
	}
	
	if (!anyDifference) {
		logf("No differences detected\n");
		return;
	}

	// delete lumps that have no differences to save space
	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (!differences[i]) {
			delete[] undoLumpState.lumps[i];
			delete[] newLumps.lumps[i];
			undoLumpState.lumps[i] = newLumps.lumps[i] = NULL;
			undoLumpState.lumpLen[i] = newLumps.lumpLen[i] = 0;
		}
	}

	EditBspModelCommand* editCommand = new EditBspModelCommand(actionDesc, pickInfo, undoLumpState, newLumps, undoEntOrigin);
	pushUndoCommand(editCommand);
	saveLumpState(pickInfo.getMap(), 0xffffffff, false);

	// entity origin edits also update the ent origin (TODO: this breaks when moving + scaling something)
	updateEntityUndoState();
	g_model_edits++;
}

void Editor::pushUndoCommand(Command* cmd) {
	undoHistory.push_back(cmd);
	clearRedoCommands();

	while (!undoHistory.empty() && undoHistory.size() > undoLevels) {
		delete undoHistory[0];
		undoHistory.erase(undoHistory.begin());
	}

	calcUndoMemoryUsage();
}

void Editor::undo() {
	if (undoHistory.empty()) {
		return;
	}

	Command* undoCommand = undoHistory[undoHistory.size() - 1];
	if (!undoCommand->allowedDuringLoad && isLoading) {
		logf("Can't undo %s while map is loading!\n", undoCommand->desc.c_str());
		return;
	}

	undoCommand->undo();
	undoHistory.pop_back();
	redoHistory.push_back(undoCommand);
}

void Editor::redo() {
	if (redoHistory.empty()) {
		return;
	}

	Command* redoCommand = redoHistory[redoHistory.size() - 1];
	if (!redoCommand->allowedDuringLoad && isLoading) {
		logf("Can't redo %s while map is loading!\n", redoCommand->desc.c_str());
		return;
	}

	redoCommand->execute();
	redoHistory.pop_back();
	undoHistory.push_back(redoCommand);
}

void Editor::clearUndoCommands() {
	for (int i = 0; i < undoHistory.size(); i++) {
		delete undoHistory[i];
		undoHistory[i] = NULL;
	}

	undoHistory.clear();
	calcUndoMemoryUsage();
}

void Editor::clearRedoCommands() {
	for (int i = 0; i < redoHistory.size(); i++) {
		delete redoHistory[i];
		redoHistory[i] = NULL;
	}

	redoHistory.clear();
	calcUndoMemoryUsage();
}

void Editor::calcUndoMemoryUsage() {
	undoMemoryUsage = (undoHistory.size() + redoHistory.size()) * sizeof(Command*);

	for (int i = 0; i < undoHistory.size(); i++) {
		undoMemoryUsage += undoHistory[i]->memoryUsage();
	}
	for (int i = 0; i < redoHistory.size(); i++) {
		undoMemoryUsage += redoHistory[i]->memoryUsage();
	}

	undoMemoryUsage += sizeof(LumpState)*2;
	for (int i = 0; i < HEADER_LUMPS; i++) {
		undoMemoryUsage += undoLumpState.lumpLen[i];
	}
}

void Editor::merge(string fpath) {
	// don't save world offset from GUI in the undo state
	vec3 worldOrigin = mapRenderer->map->ents[0]->getOrigin();
	mapRenderer->map->ents[0]->setOrAddKeyvalue("origin", "0 0 0");

	LumpReplaceCommand* command = new LumpReplaceCommand("Merge Map");

	mapRenderer->map->ents[0]->setOrAddKeyvalue("origin", worldOrigin.toKeyvalueString());

	Bsp* thismap = g_app->mapRenderer->map;
	thismap->update_ent_lump();

	Bsp* map2 = new Bsp(fpath);
	Bsp* thisCopy = new Bsp(*thismap);

	if (!map2->valid) {
		delete map2;
		logf("Merge aborted because the BSP load failed.\n");
		return;
	}
	
	vector<Bsp*> maps;
	
	maps.push_back(thisCopy);
	maps.push_back(map2);

	logf("Cleaning %s\n", thisCopy->name.c_str());
	thisCopy->remove_unused_model_structures().print_delete_stats(2);

	logf("Cleaning %s\n", map2->name.c_str());
	map2->remove_unused_model_structures().print_delete_stats(2);

	BspMerger merger;
	mergeResult = merger.merge(maps, vec3(), thismap->name, true, true, true, false, g_settings.mapsize_max);

	if (!mergeResult.map || !mergeResult.map->valid) {
		delete map2;
		if (mergeResult.map)
			delete mergeResult.map;

		mergeResult.map = NULL;
		delete command;
		gui->showWidget(WIDGET_MERGE_OVERLAP, true);
		return;
	}

	if (mergeResult.overflow) {
		delete command;
		gui->showWidget(WIDGET_MERGE_FAILED, true);
		return; // map deleted later in gui modal, after displaying limit overflows
	}
	
	LumpState mergedLumps = mergeResult.map->duplicate_lumps(0xffffffff);
	mapRenderer->map->replace_lumps(mergedLumps);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		delete[] mergedLumps.lumps[i];
	}
	logf("Merged maps!\n");

	command->pushUndoState();
}

void Editor::mergeMultiple(vector<string> fpaths, bool optimizeMerge, bool forceNohull2, int ripentmode) {
	openMapAfterMergeCancel = mapRenderer->map->path;
	mergeOptimize = optimizeMerge;
	mergeNohull2 = forceNohull2;
	mergeRipentMode = ripentmode;
	clearMapData();

	mapArrangeMode = true;
	pickMode = PICK_OBJECT;

	Bsp* mergeMap = new Bsp();

	Entity* worldspawn = new Entity();
	worldspawn->setOrAddKeyvalue("classname", "worldspawn");
	mergeMap->ents.push_back(worldspawn);

	float lastMapMax = -g_settings.mapsize_max;
	for (string path : fpaths) {
		Bsp* map = new Bsp(path);

		vec3 mins, maxs;
		map->get_bounding_box(mins, maxs);

		float offsetX = lastMapMax - mins.x;

		vec3 offset(offsetX, 0, 0);

		Entity* mapEnt = new Entity();
		mapEnt->setOrAddKeyvalue("origin", offset.toKeyvalueString());
		mapEnt->setOrAddKeyvalue("classname", "map");
		mapEnt->setOrAddKeyvalue("targetname", map->name);		

		lastMapMax = (offset.x + maxs.x) + 16;

		BspRenderer* mapRenderer = new BspRenderer(map, pointEntRenderer);
		mapRenderer->mapOffset = offset;
		arrangeBsps.push_back(mapRenderer);
		mergeMap->ents.push_back(mapEnt);
	}

	addMap(mergeMap);

	gui->refresh();
	updateCullBox();

}

bool Editor::confirmMapExit() {
	if (emptyMapLoaded || mapArrangeMode)
		return true;

	if (g_settings.confirm_exit) {
		Bsp* map = mapRenderer->map;

		if (map->did_lumps_change(false)) {
			string msg = "Save changes to " + map->name + "?";
			int ret = Alert("save", msg.c_str(), "yesnocancel", "warning", 0);

			if (ret == 0) { // cancel
				glfwSetWindowShouldClose(window, GLFW_FALSE);
				return false;
			}
			else if (ret == 1) { // yes
				map->update_ent_lump();
				map->write(map->path);
				return true;
			}
			else { // no
				return true;
			}
		}
		else {
			debugf("lumps not changed\n");
		}
	}

	return true;
}

vec3 Editor::worldToScreen(const vec3& P) {
	vec3 forward, right, up;
	vec3 angles = vec3(cameraAngles.x, -(cameraAngles.z - 90), cameraAngles.y);

	AngleVectors(angles, (float*)&forward, (float*)&right, (float*)&up);

	vec3 rel = P - cameraOrigin;

	float x = dotProduct(rel, right);
	float y = dotProduct(rel, up);
	float z = dotProduct(rel, forward);

	float aspect = (float)windowWidth / (float)windowHeight;
	float f = fov * (PI / 180.0f);

	float ndcX = (x * f / aspect) / z;
	float ndcY = (y * f) / z;

	float screenX = (ndcX + 1.0f) * 0.5f * windowWidth;
	float screenY = (1.0f - ndcY) * 0.5f * windowHeight;

	return { screenX, screenY, z };
}

Frustum Editor::getCameraFrustum() {
	float aspect = (float)windowWidth / (float)windowHeight;
	return getViewFrustum(cameraOrigin - mapRenderer->mapOffset, cameraAngles, aspect, zNear, zFar, fov);
}

void Editor::exit() {
	if (!confirmMapExit()) {
		return;
	}

	if (g_settings.fullscreen) {
		g_settings.windowWidth = oldWindowW;
		g_settings.windowHeight = oldWindowH;
		g_settings.windowX = oldWindowX;
		g_settings.windowY = oldWindowY;
	}

	g_settings.save();
	logf("adios\n");
}

bool Editor::autoSelectEngine(Bsp* map, bool reloadIfChanged) {
	if (!map || !g_settings.auto_engine_select && !g_settings.ripent_safe_mode)
		return false;

	int oldEngine = g_settings.engine;
	int autoFormat = map->lastSaveFormat != -1 ? map->lastSaveFormat : map->lastLoadformat;

	switch (autoFormat) {
	case BSP_QUAKE1:
		g_settings.engine = ENGINE_QUAKE_1;
		break;
	case BSP_QUAKE1_BSP2:
	case BSP_QUAKE1_2PSB:
		g_settings.engine = ENGINE_QUAKE_1_BSP2;
		break;
	case BSP_HALFLIFE:
	default:
		g_settings.engine = ENGINE_HALF_LIFE;

		g_limits = g_engine_limits[g_settings.engine];
		if (!map->isWritable() || map->has_bad_extents()) {
			g_settings.engine = ENGINE_SVEN_COOP;
		}
		break;
	case BSP_BLUESHIFT:
		g_settings.engine = ENGINE_BLUE_SHIFT;
		break;
	}
	
	bool changed = g_settings.engine != oldEngine;

	if (g_settings.engine != oldEngine) {
		logf("Auto selected engine: %s\n", g_engine_names[g_settings.engine]);

		if (g_settings.mapsize_auto) {
			if (g_settings.engine == ENGINE_SVEN_COOP) {
				g_settings.mapsize_min = -32768;
				g_settings.mapsize_max = 32768;
			}
			else {
				g_settings.mapsize_min = -4096;
				g_settings.mapsize_max = 4096;
			}
		}

		if (reloadIfChanged)
			mapRenderer->reload();
		gui->reloadLimits();
	}

	g_limits = g_engine_limits[g_settings.engine];

	return changed;
}