#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Gui.h"
#include "Polygon3D.h"
#include "PointEntRenderer.h"
#include "Bsp.h"
#include "Command.h"
#include "Fgd.h"
#include "Entity.h"
#include "util.h"
#include <fstream>
#include "globals.h"
#include "NavMesh.h"
#include <algorithm>

// everything except VIS, ENTITIES, MARKSURFS
#define EDIT_MODEL_LUMPS (PLANES | TEXTURES | VERTICES | NODES | TEXINFO | FACES | LIGHTING | CLIPNODES | LEAVES | EDGES | SURFEDGES | MODELS)

future<void> Renderer::fgdFuture;

void error_callback(int error, const char* description)
{
	logf("GLFW Error: %s\n", description);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		g_app->hideGui = !g_app->hideGui;
	}
}

void window_size_callback(GLFWwindow* window, int width, int height)
{
	if (g_settings.maximized || width == 0 || height == 0) {
		return; // ignore size change when maximized, or else iconifying doesn't change size at all
	}
	g_settings.windowWidth = width;
	g_settings.windowHeight = height;
}

void window_pos_callback(GLFWwindow* window, int x, int y)
{
	g_settings.windowX = x;
	g_settings.windowY = y;
}

void window_maximize_callback(GLFWwindow* window, int maximized)
{
	g_settings.maximized = maximized == GLFW_TRUE;
}

void window_close_callback(GLFWwindow* window)
{
	g_settings.save();
	logf("adios\n");
}

int g_scroll = 0;

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	g_scroll += round(yoffset);
}

Renderer::Renderer() {
	g_settings.loadDefault();
	g_settings.load();

	if (!glfwInit())
	{
		logf("GLFW initialization failed\n");
		return;
	}

	glfwSetErrorCallback(error_callback);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	window = glfwCreateWindow(g_settings.windowWidth, g_settings.windowHeight, "bspguy", NULL, NULL);
	
	if (g_settings.valid) {
		glfwSetWindowPos(window, g_settings.windowX, g_settings.windowY);
		
		// setting size again to fix issue where window is too small because it was
		// moved to a monitor with a different DPI than the one it was created for
		glfwSetWindowSize(window, g_settings.windowWidth, g_settings.windowHeight);
		if (g_settings.maximized) {
			glfwMaximizeWindow(window);
		}
	}

	if (!window)
	{
		logf("Window creation failed. Maybe your PC doesn't support OpenGL 3.0\n");
		return;
	}

	glfwMakeContextCurrent(window);
	glfwSetKeyCallback(window, key_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetWindowSizeCallback(window, window_size_callback);
	glfwSetWindowPosCallback(window, window_pos_callback);
	glfwSetWindowCloseCallback(window, window_close_callback);
	glfwSetWindowMaximizeCallback(window, window_maximize_callback);

	glewInit();

	// init to black screen instead of white
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glfwSwapBuffers(window);
	glfwSwapInterval(1);

	gui = new Gui(this);

	bspShader = new ShaderProgram(g_shader_multitexture_vertex, g_shader_multitexture_fragment);
	bspShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	bspShader->setMatrixNames(NULL, "modelViewProjection");

	fullBrightBspShader = new ShaderProgram(g_shader_fullbright_vertex, g_shader_fullbright_fragment);
	fullBrightBspShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	fullBrightBspShader->setMatrixNames(NULL, "modelViewProjection");

	colorShader = new ShaderProgram(g_shader_cVert_vertex, g_shader_cVert_fragment);
	colorShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	colorShader->setMatrixNames(NULL, "modelViewProjection");
	colorShader->setVertexAttributeNames("vPosition", "vColor", NULL);

	colorShader->bind();
	uint colorMultId = glGetUniformLocation(colorShader->ID, "colorMult");
	glUniform4f(colorMultId, 1, 1, 1, 1);

	
	pickInfo.valid = false;


	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

	g_app = this;

	g_progress.simpleMode = true;

	pointEntRenderer = new PointEntRenderer(NULL, colorShader);

	loadSettings();

	reloading = true;
	fgdFuture = async(launch::async, &Renderer::loadFgds, this);

	memset(&undoLumpState, 0, sizeof(LumpState));

	//cameraOrigin = vec3(51, 427, 234);
	//cameraAngles = vec3(41, 0, -170);
}

Renderer::~Renderer() {
	glfwTerminate();
}

void Renderer::renderLoop() {
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	{
		moveAxes.dimColor[0] = { 110, 0, 160, 255 };
		moveAxes.dimColor[1] = { 0, 0, 220, 255 };
		moveAxes.dimColor[2] = { 0, 160, 0, 255 };
		moveAxes.dimColor[3] = { 160, 160, 160, 255 };

		moveAxes.hoverColor[0] = { 128, 64, 255, 255 };
		moveAxes.hoverColor[1] = { 64, 64, 255, 255 };
		moveAxes.hoverColor[2] = { 64, 255, 64, 255 };
		moveAxes.hoverColor[3] = { 255, 255, 255, 255 };

		// flipped for HL coords
		moveAxes.model = new cCube[4];
		moveAxes.buffer = new VertexBuffer(colorShader, COLOR_4B | POS_3F, moveAxes.model, 6 * 6 * 4);
		moveAxes.numAxes = 4;
	}

	{
		scaleAxes.dimColor[0] = { 110, 0, 160, 255 };
		scaleAxes.dimColor[1] = { 0, 0, 220, 255 };
		scaleAxes.dimColor[2] = { 0, 160, 0, 255 };

		scaleAxes.dimColor[3] = { 110, 0, 160, 255 };
		scaleAxes.dimColor[4] = { 0, 0, 220, 255 };
		scaleAxes.dimColor[5] = { 0, 160, 0, 255 };

		scaleAxes.hoverColor[0] = { 128, 64, 255, 255 };
		scaleAxes.hoverColor[1] = { 64, 64, 255, 255 };
		scaleAxes.hoverColor[2] = { 64, 255, 64, 255 };

		scaleAxes.hoverColor[3] = { 128, 64, 255, 255 };
		scaleAxes.hoverColor[4] = { 64, 64, 255, 255 };
		scaleAxes.hoverColor[5] = { 64, 255, 64, 255 };

		// flipped for HL coords
		scaleAxes.model = new cCube[6];
		scaleAxes.buffer = new VertexBuffer(colorShader, COLOR_4B | POS_3F, scaleAxes.model, 6 * 6 * 6);
		scaleAxes.numAxes = 6;
	}

	updateDragAxes();

	float s = 1.0f;
	cCube vertCube(vec3(-s, -s, -s), vec3(s, s, s), { 0, 128, 255, 255 });
	VertexBuffer vertCubeBuffer(colorShader, COLOR_4B | POS_3F, &vertCube, 6 * 6);

	float lastFrameTime = glfwGetTime();
	float lastTitleTime = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		if (glfwGetTime( ) - lastTitleTime > 0.1)
		{
			lastTitleTime = glfwGetTime( );
			glfwSetWindowTitle(window, (getMapContainingCamera()->map->path + std::string(std::string(" - bspguy"))).c_str());
		}
		glfwPollEvents();

		float frameDelta = glfwGetTime() - lastFrameTime;
		frameTimeScale = 0.05f / frameDelta;
		float fps = 1.0f / frameDelta;
		
		//FIXME : frameTimeScale = 0.05f / frameDelta ???
		frameTimeScale = 144.0f / fps;

		lastFrameTime = glfwGetTime();

		float spin = glfwGetTime() * 2;
		model.loadIdentity();
		model.rotateZ(spin);
		model.rotateX(spin);
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		setupView();
		glEnable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);

		drawEntConnections();

		isLoading = reloading;
		for (int i = 0; i < mapRenderers.size(); i++) {
			int highlightEnt = -1;
			if (pickInfo.valid && pickInfo.mapIdx == i && pickMode == PICK_OBJECT) {
				highlightEnt = pickInfo.entIdx;
			}
			mapRenderers[i]->render(highlightEnt, transformTarget == TRANSFORM_VERTEX, clipnodeRenderHull);

			if (!mapRenderers[i]->isFinishedLoading()) {
				isLoading = true;
			}
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

			if (debugNodes && pickInfo.valid && pickInfo.modelIdx > 0) {
				BSPMODEL& pickModel = pickInfo.map->models[pickInfo.modelIdx];
				glDisable(GL_CULL_FACE);
				int currentPlane = 0;
				drawNodes(pickInfo.map, pickModel.iHeadnodes[0], currentPlane, debugNode);
				debugNodeMax = currentPlane - 1;
				glEnable(GL_CULL_FACE);
			}

			if (g_render_flags & (RENDER_ORIGIN | RENDER_MAP_BOUNDARY)) {
				colorShader->bind();
				model.loadIdentity();
				colorShader->pushMatrix(MAT_MODEL);
				if (pickInfo.valid) {
					vec3 offset = mapRenderers[pickInfo.mapIdx]->mapOffset.flip();
					model.translate(offset.x, offset.y, offset.z);
				}
				colorShader->updateMatrixes();

				if (g_render_flags & RENDER_ORIGIN) {
					drawLine(debugPoint - vec3(32, 0, 0), debugPoint + vec3(32, 0, 0), { 128, 128, 255, 255 });
					drawLine(debugPoint - vec3(0, 32, 0), debugPoint + vec3(0, 32, 0), { 0, 255, 0, 255 });
					drawLine(debugPoint - vec3(0, 0, 32), debugPoint + vec3(0, 0, 32), { 0, 0, 255, 255 });
				}
				
				if (g_render_flags & RENDER_MAP_BOUNDARY) {
					glDisable(GL_CULL_FACE);
					drawBox(mapRenderers[0]->map->ents[0]->getOrigin() * -1, g_limits.max_mapboundary * 2, COLOR4(0, 255, 0, 64));
					glEnable(GL_CULL_FACE);
				}

				colorShader->popMatrix(MAT_MODEL);
			}
		}

		if (entConnectionPoints && (g_render_flags & RENDER_ENT_CONNECTIONS)) {
			model.loadIdentity();
			colorShader->updateMatrixes();
			glDisable(GL_DEPTH_TEST);
			entConnectionPoints->draw(GL_TRIANGLES);
			glEnable(GL_DEPTH_TEST);
		}

		bool isScalingObject = transformMode == TRANSFORM_SCALE && transformTarget == TRANSFORM_OBJECT;
		bool isMovingOrigin = transformMode == TRANSFORM_MOVE && transformTarget == TRANSFORM_ORIGIN && originSelected;
		bool isTransformingValid = ((isTransformableSolid && !modelUsesSharedStructures) || !isScalingObject) && transformTarget != TRANSFORM_ORIGIN;
		bool isTransformingWorld = pickInfo.entIdx == 0 && transformTarget != TRANSFORM_OBJECT;
		if (showDragAxes && !movingEnt && !isTransformingWorld && pickInfo.entIdx >= 0 && pickInfo.valid && (isTransformingValid || isMovingOrigin)) {
			drawTransformAxes();
		}

		if (pickInfo.valid && pickInfo.modelIdx > 0 && pickMode == PICK_OBJECT) {
			if (transformTarget == TRANSFORM_VERTEX && isTransformableSolid) {
				drawModelVerts();
			}
			if (transformTarget == TRANSFORM_ORIGIN) {
				drawModelOrigin();
			}
		}

		{
			colorShader->bind();
			model.loadIdentity();
			colorShader->updateMatrixes();
			glDisable(GL_CULL_FACE);

			glLineWidth(128.0f);
			drawLine(debugLine0, debugLine1, { 255, 0, 0, 255 });
			
			drawLine(debugTraceStart, debugTrace.vecEndPos, COLOR4(255, 0, 0, 255));
			
			if (debugNavMesh && debugNavPoly != -1) {
				glLineWidth(1);
				NavNode& node = debugNavMesh->nodes[debugNavPoly];
				Polygon3D& poly = debugNavMesh->polys[debugNavPoly];

				for (int i = 0; i < MAX_NAV_LINKS; i++) {
					NavLink& link = node.links[i];
					if (link.node == -1) {
						break;
					}
					Polygon3D& linkPoly = debugNavMesh->polys[link.node];

					vec3 srcMid, dstMid;
					debugNavMesh->getLinkMidPoints(debugNavPoly, i, srcMid, dstMid);

					glDisable(GL_DEPTH_TEST);
					drawLine(poly.center, srcMid, COLOR4(0, 255, 255, 255));
					drawLine(srcMid, dstMid, COLOR4(0, 255, 255, 255));
					drawLine(dstMid, linkPoly.center, COLOR4(0, 255, 255, 255));

					if (fabs(link.zDist) > NAV_STEP_HEIGHT) {
						Bsp* map = mapRenderers[0]->map;
						int i = link.srcEdge;
						int k = link.dstEdge;
						int inext = (i + 1) % poly.verts.size();
						int knext = (k + 1) % linkPoly.verts.size();

						Line2D thisEdge(poly.topdownVerts[i], poly.topdownVerts[inext]);
						Line2D otherEdge(linkPoly.topdownVerts[k], linkPoly.topdownVerts[knext]);

						float t0, t1, t2, t3;
						float overlapDist = thisEdge.getOverlapRanges(otherEdge, t0, t1, t2, t3);
						
						vec3 delta1 = poly.verts[inext] - poly.verts[i];
						vec3 delta2 = linkPoly.verts[knext] - linkPoly.verts[k];
						vec3 e1 = poly.verts[i] + delta1 * t0;
						vec3 e2 = poly.verts[i] + delta1 * t1;
						vec3 e3 = linkPoly.verts[k] + delta2 * t2;
						vec3 e4 = linkPoly.verts[k] + delta2 * t3;

						bool isBelow = link.zDist > 0;
						delta1 = e2 - e1;
						delta2 = e4 - e3;
						vec3 mid1 = e1 + delta1 * 0.5f;
						vec3 mid2 = e3 + delta2 * 0.5f;
						vec3 inwardDir = crossProduct(poly.plane_z, delta1.normalize());
						vec3 testOffset = (isBelow ? inwardDir : inwardDir * -1) + vec3(0, 0, 1.0f);

						float flatLen = (e2.xy() - e1.xy()).length();
						float stepUnits = 1.0f;
						float step = stepUnits / flatLen;
						TraceResult tr;
						bool isBlocked = true;
						for (float f = 0; f < 0.5f; f += step) {
							vec3 test1 = mid1 + (delta1 * f) + testOffset;
							vec3 test2 = mid2 + (delta2 * f) + testOffset;
							vec3 test3 = mid1 + (delta1 * -f) + testOffset;
							vec3 test4 = mid2 + (delta2 * -f) + testOffset;

							map->traceHull(test1, test2, 3, &tr);
							if (!tr.fAllSolid && !tr.fStartSolid && tr.flFraction > 0.99f) {
								drawLine(test1, test2, COLOR4(255, 255, 0, 255));
							}
							else {
								drawLine(test1, test2, COLOR4(255, 0, 0, 255));
							}

							map->traceHull(test3, test4, 3, &tr);
							if (!tr.fAllSolid && !tr.fStartSolid && tr.flFraction > 0.99f) {
								drawLine(test3, test4, COLOR4(255, 255, 0, 255));
							}
							else {
								drawLine(test3, test4, COLOR4(255, 0, 0, 255));
							}
						}

						//if (isBlocked) {
						//	continue;
						//}
					}

					glEnable(GL_DEPTH_TEST);
					drawBox(linkPoly.center, 4, COLOR4(0, 255, 255, 255));
				}
			}

			if (debugPoly.isValid) {
				if (debugPoly.verts.size() > 1) {
					vec3 v1 = debugPoly.verts[0];
					vec3 v2 = debugPoly.verts[1];
					drawLine(v1, v1 + (v2 - v1) * 0.5f, COLOR4(0, 255, 0, 255));
				}
				for (int i = 0; i < debugPoly.verts.size(); i++) {
					drawBox(debugPoly.verts[i], 4, COLOR4(128, 128, 0, 255));
				}

				glLineWidth(1);
				vec3 xaxis = debugPoly.plane_x * 16;
				vec3 yaxis = debugPoly.plane_y * 16;
				vec3 zaxis = debugPoly.plane_z * 16;
				vec3 center = getCenter(debugPoly.verts) + debugPoly.plane_z*8;
				drawLine(center, center + xaxis, COLOR4(255, 0, 0, 255));
				drawLine(center, center + yaxis, COLOR4(255, 255, 0, 255));
				drawLine(center, center + zaxis, COLOR4(0, 255, 0, 255));				

				glDisable(GL_DEPTH_TEST);

				colorShader->pushMatrix(MAT_PROJECTION);
				colorShader->pushMatrix(MAT_VIEW);
				projection.ortho(0, windowWidth, windowHeight, 0, -1.0f, 1.0f);
				view.loadIdentity();
				float sz = min(windowWidth*0.8f, windowHeight*0.8f);
				model.translate((windowWidth- sz)*0.5f, (windowHeight- sz)*0.5f, 0);
				colorShader->updateMatrixes();

				float scale = drawPolygon2D(debugPoly, vec2(0, 0), vec2(sz, sz), COLOR4(255, 255, 0, 255));

				colorShader->popMatrix(MAT_PROJECTION);
				colorShader->popMatrix(MAT_VIEW);
			}

			glLineWidth(1);
		}

		vec3 forward, right, up;
		makeVectors(cameraAngles, forward, right, up);
		//logf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

		if (!g_app->hideGui)
			gui->draw();

		controls();

		glfwSwapBuffers(window);

		if (reloading && fgdFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
			postLoadFgds();
			reloading = reloadingGameDir = false;
		}

		int glerror = glGetError();
		if (glerror != GL_NO_ERROR) {
			logf("Got OpenGL Error: %d\n", glerror);
		}
	}

	glfwTerminate();
}

void Renderer::postLoadFgds()
{
	delete pointEntRenderer;
	delete fgd;

	pointEntRenderer = (PointEntRenderer*)swapPointEntRenderer;
	fgd = pointEntRenderer->fgd;

	for (int i = 0; i < mapRenderers.size(); i++) {
		mapRenderers[i]->pointEntRenderer = pointEntRenderer;
		mapRenderers[i]->preRenderEnts();
		if (reloadingGameDir) {
			mapRenderers[i]->reloadTextures();
		}
	}

	swapPointEntRenderer = NULL;
}

void Renderer::postLoadFgdsAndTextures() {
	if (reloading) {
		logf("Previous reload not finished. Aborting reload.");
		return;
	}
	reloading = reloadingGameDir = true;
	fgdFuture = async(launch::async, &Renderer::loadFgds, this);
}

void Renderer::reloadMaps() {
	vector<string> reloadPaths;
	for (int i = 0; i < mapRenderers.size(); i++) {
		reloadPaths.push_back(mapRenderers[i]->map->path);
		delete mapRenderers[i];
	}
	mapRenderers.clear();
	pickInfo.valid = false;
	for (int i = 0; i < reloadPaths.size(); i++) {
		addMap(new Bsp(reloadPaths[i]));
	}
	
	clearUndoCommands();
	clearRedoCommands();

	logf("Reloaded maps\n");
}

void Renderer::openMap(const char* fpath) {
	if (!fpath) {
		return;
	}
	if (!fileExists(fpath)) {
		logf("File does not exist: %s\n", fpath);
		return;
	}

	mapRenderers.clear();
	pickInfo.valid = false;
	addMap(new Bsp(fpath));

	clearUndoCommands();
	clearRedoCommands();

	logf("Loaded map: %s\n", fpath);
}

void Renderer::saveSettings() {
	g_settings.debug_open = gui->showDebugWidget;
	g_settings.keyvalue_open = gui->showKeyvalueWidget;
	g_settings.transform_open = gui->showTransformWidget;
	g_settings.log_open = gui->showLogWidget;
	g_settings.settings_open = gui->showSettingsWidget;
	g_settings.limits_open = gui->showLimitsWidget;
	g_settings.entreport_open = gui->showEntityReport;
	g_settings.settings_tab = gui->settingsTab;
	g_settings.vsync = gui->vsync;
	g_settings.show_transform_axes = showDragAxes;
	g_settings.verboseLogs = g_verbose;
	g_settings.zfar = zFar;
	g_settings.fov = fov;
	g_settings.render_flags = g_render_flags;
	g_settings.fontSize = gui->fontSize;
	g_settings.undoLevels = undoLevels;
	g_settings.moveSpeed = moveSpeed;
	g_settings.rotSpeed = rotationSpeed;
}

void Renderer::loadSettings() {
	gui->showDebugWidget = g_settings.debug_open;
	gui->showKeyvalueWidget = g_settings.keyvalue_open;
	gui->showTransformWidget = g_settings.transform_open;
	gui->showLogWidget = g_settings.log_open;
	gui->showSettingsWidget = g_settings.settings_open;
	gui->showLimitsWidget = g_settings.limits_open;
	gui->showEntityReport = g_settings.entreport_open;

	gui->settingsTab = g_settings.settings_tab;
	gui->openSavedTabs = true;

	gui->vsync = g_settings.vsync;
	showDragAxes = g_settings.show_transform_axes;
	g_verbose = g_settings.verboseLogs;
	zFar = g_settings.zfar;
	fov = g_settings.fov;
	g_render_flags = g_settings.render_flags;
	gui->fontSize = g_settings.fontSize;
	undoLevels = g_settings.undoLevels;
	rotationSpeed = g_settings.rotSpeed;
	moveSpeed = g_settings.moveSpeed;

	gui->shouldReloadFonts = true;

	glfwSwapInterval(gui->vsync ? 1 : 0);
}

void Renderer::loadFgds() {
	Fgd* mergedFgd = NULL;

	for (int i = 0; i < g_settings.fgdPaths.size(); i++) {
		string path = g_settings.fgdPaths[i];

		g_parsed_fgds.clear();
		g_parsed_fgds.insert(path);

		Fgd* tmp = new Fgd(g_settings.fgdPaths[i]);
		if (!tmp->parse())
		{
			tmp->path = g_settings.gamedir + g_settings.fgdPaths[i];
			if (!tmp->parse())
			{
				continue;
			}
		}

		if (i == 0 || mergedFgd == NULL) {
			mergedFgd = tmp;
		}
		else {
			mergedFgd->merge(tmp);
			delete tmp;
		}
	}

	swapPointEntRenderer = new PointEntRenderer(mergedFgd, colorShader);
}

void Renderer::drawModelVerts() {
	if (modelVertBuff == NULL || modelVerts.size() == 0)
		return;
	glClear(GL_DEPTH_BUFFER_BIT);

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	Entity* ent = map->ents[pickInfo.entIdx];	
	vec3 mapOffset = mapRenderers[pickInfo.mapIdx]->mapOffset;
	vec3 renderOffset = mapOffset.flip();
	vec3 localCameraOrigin = cameraOrigin - mapOffset;

	COLOR4 vertDimColor = { 200, 200, 200, 255 };
	COLOR4 vertHoverColor = { 255, 255, 255, 255 };
	COLOR4 edgeDimColor = { 255, 128, 0, 255 };
	COLOR4 edgeHoverColor = { 255, 255, 0, 255 };
	COLOR4 selectColor = { 0, 128, 255, 255 };
	COLOR4 hoverSelectColor = { 96, 200, 255, 255 };
	vec3 entOrigin = ent->getOrigin();

	if (modelUsesSharedStructures) {
		vertDimColor = { 32, 32, 32, 255 };
		edgeDimColor = { 64, 64, 32, 255 };
	}

	int cubeIdx = 0;
	for (int i = 0; i < modelVerts.size(); i++) {
		vec3 ori = modelVerts[i].pos + entOrigin;
		float s = (ori - localCameraOrigin).length() * vertExtentFactor;
		ori = ori.flip();

		if (anyEdgeSelected) {
			s = 0; // can't select certs when edges are selected
		}

		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		COLOR4 color;
		if (modelVerts[i].selected) {
			color = i == hoverVert ? hoverSelectColor : selectColor;
		}
		else {
			color = i == hoverVert ? vertHoverColor : vertDimColor;
		}
		modelVertCubes[cubeIdx++] = cCube(min, max, color);
	}

	for (int i = 0; i < modelEdges.size(); i++) {
		vec3 ori = getEdgeControlPoint(modelVerts, modelEdges[i]) + entOrigin;
		float s = (ori - localCameraOrigin).length() * vertExtentFactor;
		ori = ori.flip();

		if (anyVertSelected && !anyEdgeSelected) {
			s = 0; // can't select edges when verts are selected
		}

		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		COLOR4 color;
		if (modelEdges[i].selected) {
			color = i == hoverEdge ? hoverSelectColor : selectColor;
		}
		else {
			color = i == hoverEdge ? edgeHoverColor : edgeDimColor;
		}
		modelVertCubes[cubeIdx++] = cCube(min, max, color);
	}

	model.loadIdentity();
	model.translate(renderOffset.x, renderOffset.y, renderOffset.z);
	colorShader->updateMatrixes();
	modelVertBuff->draw(GL_TRIANGLES);
}

void Renderer::drawModelOrigin() {
	if (modelOriginBuff == NULL)
		return;

	glClear(GL_DEPTH_BUFFER_BIT);

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	vec3 mapOffset = mapRenderers[pickInfo.mapIdx]->mapOffset;
	Entity* ent = map->ents[pickInfo.entIdx];

	COLOR4 vertDimColor = { 0, 200, 0, 255 };
	COLOR4 vertHoverColor = { 128, 255, 128, 255 };
	COLOR4 selectColor = { 0, 128, 255, 255 };
	COLOR4 hoverSelectColor = { 96, 200, 255, 255 };

	if (modelUsesSharedStructures) {
		vertDimColor = { 32, 32, 32, 255 };
	}

	vec3 ori = transformedOrigin + mapOffset;
	float s = (ori - cameraOrigin).length() * vertExtentFactor;
	ori = ori.flip();

	vec3 min = vec3(-s, -s, -s) + ori;
	vec3 max = vec3(s, s, s) + ori;
	COLOR4 color;
	if (originSelected) {
		color = originHovered ? hoverSelectColor : selectColor;
	}
	else {
		color = originHovered ? vertHoverColor : vertDimColor;
	}
	modelOriginCube = cCube(min, max, color);

	model.loadIdentity();
	colorShader->updateMatrixes();
	modelOriginBuff->draw(GL_TRIANGLES);
}

void Renderer::drawTransformAxes() {
	if (!canTransform) {
		return;
	}

	glClear(GL_DEPTH_BUFFER_BIT);

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

void Renderer::drawEntConnections() {
	if (entConnections && (g_render_flags & RENDER_ENT_CONNECTIONS)) {
		model.loadIdentity();
		colorShader->updateMatrixes();
		entConnections->draw(GL_LINES);
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

	static bool oldWantTextInput = false;

	if (!io.WantTextInput && oldWantTextInput) {
		pushEntityUndoState("Edit Keyvalues");
	}

	oldWantTextInput = io.WantTextInput;

	if (!io.WantTextInput)
		globalShortcutControls();

	if (!io.WantCaptureMouse) {
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
	}

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
		canTransform = (transformTarget == TRANSFORM_OBJECT || transformTarget == TRANSFORM_ORIGIN) && transformMode == TRANSFORM_MOVE;
	}

	if (pressed[GLFW_KEY_F] && !oldPressed[GLFW_KEY_F])
	{
		if (!anyCtrlPressed)
		{
			splitFace();
		}
		else
		{
			gui->showEntityReport = true;
		}
	}
}

void Renderer::cameraPickingControls() {
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		bool transforming = transformAxisControls();

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

		// object picking
		if (!transforming && oldLeftMouse != GLFW_PRESS) {
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
				invalidSolid = !pickInfo.map->vertex_manipulation_sync(pickInfo.modelIdx, modelVerts, false, true);
				gui->reloadLimits();

				int modelIdx = pickInfo.ent->getBspModelIdx();
				if (modelIdx >= 0)
					mapRenderers[pickInfo.mapIdx]->refreshModel(modelIdx);
			}
			
			pickObject();
			pickCount++;
		}
	}
	else { // left mouse not pressed
		pickClickHeld = false;
		if (draggingAxis != -1) {
			draggingAxis = -1;
			applyTransform();

			if (pickInfo.valid && pickInfo.ent && undoEntityState->getOrigin() != pickInfo.ent->getOrigin()) {
				pushEntityUndoState("Move Entity");
			}
		}
	}
}

void Renderer::applyTransform(bool forceUpdate) {
	if (!isTransformableSolid || modelUsesSharedStructures) {
		return;
	}

	if (pickInfo.valid && pickInfo.modelIdx > 0 && pickMode == PICK_OBJECT) {
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

			invalidSolid = !pickInfo.map->vertex_manipulation_sync(pickInfo.modelIdx, modelVerts, false, true);
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
					BSPTEXTUREINFO& info = pickInfo.map->texinfos[scaleTexinfos[i].texinfoIdx];
					scaleTexinfos[i].oldShiftS = info.shiftS;
					scaleTexinfos[i].oldShiftT = info.shiftT;
					scaleTexinfos[i].oldS = info.vS;
					scaleTexinfos[i].oldT = info.vT;
				}
			}

			actionIsUndoable = !invalidSolid;
		}

		if (movingOrigin && pickInfo.valid && pickInfo.modelIdx >= 0) {
			if (oldOrigin != transformedOrigin) {
				vec3 delta = transformedOrigin - oldOrigin;

				g_progress.hide = true;
				pickInfo.map->move(delta*-1, pickInfo.modelIdx);
				g_progress.hide = false;

				oldOrigin = transformedOrigin;
				mapRenderers[pickInfo.mapIdx]->refreshModel(pickInfo.modelIdx);

				for (int i = 0; i < pickInfo.map->ents.size(); i++) {
					Entity* ent = pickInfo.map->ents[i];
					if (ent->getBspModelIdx() == pickInfo.modelIdx) {
						ent->setOrAddKeyvalue("origin", (ent->getOrigin() + delta).toKeyvalueString());
						mapRenderers[pickInfo.mapIdx]->refreshEnt(i);
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
			cameraAngles.z += drag.x * rotationSpeed*0.1f;
			cameraAngles.x += drag.y * rotationSpeed*0.1f;

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
	originHovered = false;

	if (modelUsesSharedStructures && (transformTarget != TRANSFORM_OBJECT || transformMode != TRANSFORM_MOVE))
		return;

	vec3 mapOffset;
	if (pickInfo.valid)
		mapOffset = mapRenderers[pickInfo.mapIdx]->mapOffset;

	if (transformTarget == TRANSFORM_VERTEX && pickInfo.valid && pickInfo.entIdx > 0) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		PickInfo vertPick;
		memset(&vertPick, 0, sizeof(PickInfo));
		vertPick.bestDist = FLT_MAX;

		vec3 entOrigin = pickInfo.ent->getOrigin();
		
		hoverEdge = -1;
		if (!(anyVertSelected && !anyEdgeSelected)) {
			for (int i = 0; i < modelEdges.size(); i++) {
				vec3 ori = getEdgeControlPoint(modelVerts, modelEdges[i]) + entOrigin + mapOffset;
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
				vec3 ori = entOrigin + modelVerts[i].pos + mapOffset;
				float s = (ori - cameraOrigin).length() * vertExtentFactor * 2.0f;
				vec3 min = vec3(-s, -s, -s) + ori;
				vec3 max = vec3(s, s, s) + ori;
				if (pickAABB(pickStart, pickDir, min, max, vertPick.bestDist)) {
					hoverVert = i;
				}
			}
		}
	}

	if (transformTarget == TRANSFORM_ORIGIN && pickInfo.valid && pickInfo.modelIdx > 0) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		PickInfo vertPick;
		memset(&vertPick, 0, sizeof(PickInfo));
		vertPick.bestDist = FLT_MAX;

		vec3 ori = transformedOrigin + mapOffset;
		float s = (ori - cameraOrigin).length() * vertExtentFactor * 2.0f;
		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		originHovered = pickAABB(pickStart, pickDir, min, max, vertPick.bestDist);
	}

	if (transformTarget == TRANSFORM_VERTEX && transformMode == TRANSFORM_SCALE)
		return; // 3D scaling disabled in vertex edit mode

	// axis handle hovering
	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);
	hoverAxis = -1;
	if (showDragAxes && !movingEnt && pickInfo.valid && hoverVert == -1 && hoverEdge == -1) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		PickInfo axisPick;
		memset(&axisPick, 0, sizeof(PickInfo));
		axisPick.bestDist = FLT_MAX;

		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		vec3 origin = activeAxes.origin;

		int axisChecks = transformMode == TRANSFORM_SCALE ? activeAxes.numAxes : 3;
		for (int i = 0; i < axisChecks; i++) {
			if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[i], origin + activeAxes.maxs[i], axisPick.bestDist)) {
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

void Renderer::cameraContextMenus() {
	// context menus
	bool wasTurning = cameraIsRotating && totalMouseDrag.length() >= 1;
	if (draggingAxis == -1 && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE && oldRightMouse != GLFW_RELEASE && !wasTurning) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);

		PickInfo tempPick;
		memset(&tempPick, 0, sizeof(PickInfo));
		tempPick.bestDist = FLT_MAX;
		for (int i = 0; i < mapRenderers.size(); i++) {
			if (mapRenderers[i]->pickPoly(pickStart, pickDir, clipnodeRenderHull, tempPick)) {
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
	if (pickInfo.valid && movingEnt && pickInfo.ent) {
		if (g_scroll != oldScroll) {
			float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 4.0f : 2.0f;
			if (pressed[GLFW_KEY_LEFT_CONTROL])
				moveScale = 1.0f;
			if (g_scroll < oldScroll)
				moveScale *= -1;

			grabDist += 16 * moveScale;
		}

		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		vec3 mapOffset = mapRenderers[pickInfo.mapIdx]->mapOffset;
		vec3 delta = ((cameraOrigin - mapOffset) + cameraForward * grabDist) - grabStartOrigin;
		Entity* ent = map->ents[pickInfo.entIdx];

		vec3 oldOrigin = grabStartEntOrigin;
		vec3 offset = getEntOffset(map, ent);
		vec3 newOrigin = (oldOrigin + delta) - offset;
		vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

		transformedOrigin = this->oldOrigin = rounded;
		
		ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
		mapRenderers[pickInfo.mapIdx]->refreshEnt(pickInfo.entIdx);
		updateEntConnectionPositions();
	}
	else {
		ungrabEnt();
	}
}

void Renderer::shortcutControls() {
	if (pickMode == PICK_OBJECT) {
		bool anyEnterPressed = (pressed[GLFW_KEY_ENTER] && !oldPressed[GLFW_KEY_ENTER]) ||
			(pressed[GLFW_KEY_KP_ENTER] && !oldPressed[GLFW_KEY_KP_ENTER]);

		if (pressed[GLFW_KEY_G] == GLFW_PRESS && oldPressed[GLFW_KEY_G] != GLFW_PRESS) {
			if (!movingEnt)
				grabEnt();
			else {
				ungrabEnt();
			}
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
		if (anyCtrlPressed && pressed[GLFW_KEY_G] && !oldPressed[GLFW_KEY_G]) {
			gui->showGOTOWidget = !gui->showGOTOWidget;
			gui->showGOTOWidget_update = true;
		}
		if (anyAltPressed && anyEnterPressed) {
			gui->showKeyvalueWidget = !gui->showKeyvalueWidget;
		}
		if (pressed[GLFW_KEY_DELETE] && !oldPressed[GLFW_KEY_DELETE]) {
			deleteEnt();
		}
	}
	else if (pickMode == PICK_FACE) {
		if (anyCtrlPressed && pressed[GLFW_KEY_C] && !oldPressed[GLFW_KEY_C]) {
			gui->copyTexture();
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_V] && !oldPressed[GLFW_KEY_V]) {
			gui->pasteTexture();
		}
	}
}

void Renderer::globalShortcutControls() {
	if (anyCtrlPressed && pressed[GLFW_KEY_Z] && !oldPressed[GLFW_KEY_Z]) {
		undo();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_Y] && !oldPressed[GLFW_KEY_Y]) {
		redo();
	}
}

void Renderer::pickObject() {
	bool pointEntWasSelected = pickInfo.valid && pickInfo.ent && !pickInfo.ent->isBspModel();
	int oldSelectedEntIdx = pickInfo.entIdx;

	vec3 pickStart, pickDir;
	getPickRay(pickStart, pickDir);

	/*
	TraceResult& tr = debugTrace;
	mapRenderers[0]->map->traceHull(pickStart, pickStart + pickDir*512, 1, &tr);
	logf("Fraction=%.1f, StartSolid=%d, AllSolid=%d, InOpen=%d, PlaneDist=%.1f\nStart=(%.1f,%.1f,%.1f) End=(%.1f,%.1f,%.1f) PlaneNormal=(%.1f,%.1f,%.1f)\n", 
		tr.flFraction, tr.fStartSolid, tr.fAllSolid, tr.fInOpen, tr.flPlaneDist,
		pickStart.x, pickStart.y, pickStart.z,
		tr.vecEndPos.x, tr.vecEndPos.y, tr.vecEndPos.z,
		tr.vecPlaneNormal.x, tr.vecPlaneNormal.y, tr.vecPlaneNormal.z);
	debugTraceStart = pickStart;
	*/

	int oldEntIdx = pickInfo.entIdx;
	memset(&pickInfo, 0, sizeof(PickInfo));
	pickInfo.bestDist = FLT_MAX;
	for (int i = 0; i < mapRenderers.size(); i++) {
		if (mapRenderers[i]->pickPoly(pickStart, pickDir, clipnodeRenderHull, pickInfo)) {
			pickInfo.mapIdx = i;
		}
	}

	if (movingEnt && oldEntIdx != pickInfo.entIdx) {
		ungrabEnt();
	}

	if (pickInfo.modelIdx >= 0) {
		//pickInfo.map->print_model_hull(pickInfo.modelIdx, 0);
	}
	else {
		if (transformMode == TRANSFORM_SCALE)
			transformMode = TRANSFORM_MOVE;
		transformTarget = TRANSFORM_OBJECT;
	}

	if ((pickMode == PICK_OBJECT || !anyCtrlPressed) && selectMapIdx != -1) {
		deselectFaces();
	}

	if (pickMode == PICK_OBJECT) {
		updateModelVerts();

		isTransformableSolid = true;
		if (pickInfo.modelIdx > 0) {
			isTransformableSolid = pickInfo.map->is_convex(pickInfo.modelIdx);
		}
	}
	else if (pickMode == PICK_FACE) {
		pickInfo.ent = NULL;
		pickInfo.entIdx = -1;
		gui->showLightmapEditorUpdate = true;

		if (pickInfo.modelIdx >= 0 && pickInfo.faceIdx >= 0) {			
			if (selectedFaces.size() && selectMapIdx != pickInfo.mapIdx) {
				logf("Can't select faces across multiple maps\n");
			}
			else {
				selectMapIdx = pickInfo.mapIdx;
				bool select = true;
				for (int i = 0; i < selectedFaces.size(); i++) {
					if (selectedFaces[i] == pickInfo.faceIdx) {
						select = false;
						selectedFaces.erase(selectedFaces.begin() + i);
						break;
					}
				}

				mapRenderers[pickInfo.mapIdx]->highlightFace(pickInfo.faceIdx, select);

				if (select)
					selectedFaces.push_back(pickInfo.faceIdx);
			}
		}
	}

	if (pointEntWasSelected) {
		for (int i = 0; i < mapRenderers.size(); i++) {
			mapRenderers[i]->refreshPointEnt(oldSelectedEntIdx);
		}
	}

	pickClickHeld = true;

	updateEntConnections();

	if (pickInfo.valid && pickInfo.map && pickInfo.ent) {
		selectEnt(pickInfo.map, pickInfo.entIdx);
	}
}

bool Renderer::transformAxisControls() {

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	if (!canTransform || pickClickHeld || pickInfo.entIdx < 0) {
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
			else if (transformTarget == TRANSFORM_OBJECT) {
				vec3 offset = getEntOffset(map, ent);
				vec3 newOrigin = (axisDragEntOriginStart + delta) - offset;
				vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

				ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
				mapRenderers[pickInfo.mapIdx]->refreshEnt(pickInfo.entIdx);
				updateEntConnectionPositions();
			}
			else if (transformTarget == TRANSFORM_ORIGIN) {
				transformedOrigin = (oldOrigin + delta);
				transformedOrigin = gridSnappingEnabled ? snapToGrid(transformedOrigin) : transformedOrigin;

				//mapRenderers[pickInfo.mapIdx]->refreshEnt(pickInfo.entIdx);
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
	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	glViewport(0, 0, windowWidth, windowHeight);

	projection.perspective(fov, (float)windowWidth / (float)windowHeight, zNear, zFar);

	view.loadIdentity();
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}

void Renderer::addMap(Bsp* map) {
	BspRenderer* mapRenderer = new BspRenderer(map, bspShader, fullBrightBspShader, colorShader, pointEntRenderer);

	mapRenderers.push_back(mapRenderer);

	gui->checkValidHulls();

	// Pick default map
	//if (!pickInfo.map) 
	{
		pickInfo.modelIdx = 0;
		pickInfo.faceIdx = -1;
		pickInfo.ent = map->ents[0];
		pickInfo.entIdx = 0;
		pickInfo.mapIdx = 0;
		pickInfo.map = map;
		pickInfo.valid = true;
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
}

void Renderer::drawLine(vec3 start, vec3 end, COLOR4 color) {
	cVert verts[2];

	verts[0].x = start.x;
	verts[0].y = start.z;
	verts[0].z = -start.y;
	verts[0].c = color;

	verts[1].x = end.x;
	verts[1].y = end.z;
	verts[1].z = -end.y;
	verts[1].c = color;

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &verts[0], 2);
	buffer.draw(GL_LINES);
}

void Renderer::drawLine2D(vec2 start, vec2 end, COLOR4 color) {
	cVert verts[2];

	verts[0].x = start.x;
	verts[0].y = start.y;
	verts[0].z = 0;
	verts[0].c = color;

	verts[1].x = end.x;
	verts[1].y = end.y;
	verts[1].z = 0;
	verts[1].c = color;

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &verts[0], 2);
	buffer.draw(GL_LINES);
}

void Renderer::drawBox(vec3 center, float width, COLOR4 color) {
	width *= 0.5f;
	vec3 sz = vec3(width, width, width);
	vec3 pos = vec3(center.x, center.z, -center.y);
	cCube cube(pos - sz, pos + sz, color);

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &cube, 6 * 6);
	buffer.draw(GL_TRIANGLES);
}

float Renderer::drawPolygon2D(Polygon3D poly, vec2 pos, vec2 maxSz, COLOR4 color) {
	vec2 sz = poly.localMaxs - poly.localMins;
	float scale = min(maxSz.y / sz.y, maxSz.x / sz.x);

	vec2 offset = poly.localMins * -scale;

	for (int i = 0; i < poly.verts.size(); i++) {
		vec2 v1 = poly.localVerts[i];
		vec2 v2 = poly.localVerts[(i + 1) % debugPoly.verts.size()];
		drawLine2D(offset + v1*scale, offset + v2 * scale, color);
	}

	{
		vec2 cam = debugPoly.project(cameraOrigin);
		drawBox2D(offset + cam*scale, 16, poly.isInside(cam) ? COLOR4(0, 255, 0, 255) : COLOR4(255, 32, 0, 255));
	}
	
	drawLine2D(offset + debugCut.start * scale, offset + debugCut.end * scale, color);

	return scale;
}

void Renderer::drawBox2D(vec2 center, float width, COLOR4 color) {
	vec2 pos = vec2(center.x, center.y) - vec2(width*0.5f, width *0.5f);
	cQuad cube(pos.x, pos.y, width, width, color);

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &cube, 6);
	buffer.draw(GL_TRIANGLES);
}

void Renderer::drawPlane(BSPPLANE& plane, COLOR4 color) {

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

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &quad, 6);
	buffer.draw(GL_TRIANGLES);
}

void Renderer::drawClipnodes(Bsp* map, int iNode, int& currentPlane, int activePlane) {
	if (iNode == -1)
		return;
	BSPCLIPNODE& node = map->clipnodes[iNode];

	if (currentPlane == activePlane)
		drawPlane(map->planes[node.iPlane], { 255, 255, 255, 255 });
	currentPlane++;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			drawClipnodes(map, node.iChildren[i], currentPlane, activePlane);
		}
	}
}

void Renderer::drawNodes(Bsp* map, int iNode, int& currentPlane, int activePlane) {
	if (iNode == -1)
		return;
	BSPNODE& node = map->nodes[iNode];

	if (currentPlane == activePlane)
		drawPlane(map->planes[node.iPlane], { 255, 128, 128, 255 });
	currentPlane++;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			drawNodes(map, node.iChildren[i], currentPlane, activePlane);
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
	vec3 mapOffset;

	if (pickInfo.valid && 
		pickInfo.mapIdx >= 0 && pickInfo.mapIdx < mapRenderers.size() &&
		pickInfo.entIdx >= 0 && pickInfo.entIdx < mapRenderers[pickInfo.mapIdx]->map->ents.size()) {
		map = mapRenderers[pickInfo.mapIdx]->map;
		ent = map->ents[pickInfo.entIdx];
		mapOffset = mapRenderers[pickInfo.mapIdx]->mapOffset;
	}
	else
	{
		return;
	}

	vec3 localCameraOrigin = cameraOrigin - mapOffset;

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
			if (transformTarget == TRANSFORM_ORIGIN) {
				moveAxes.origin = transformedOrigin;
				debugVec0 = transformedOrigin;
			}
			else {
				moveAxes.origin = getEntOrigin(map, ent);
			}
		}
		if (pickInfo.entIdx == 0) {
			moveAxes.origin -= mapOffset;
		}

		if (transformTarget == TRANSFORM_VERTEX) {
			vec3 entOrigin = ent ? ent->getOrigin() : vec3();
			vec3 min(FLT_MAX, FLT_MAX, FLT_MAX);
			vec3 max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
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

	float baseScale = (activeAxes.origin - localCameraOrigin).length() * 0.005f;
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

	activeAxes.origin += mapOffset;
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

	if (modelVertBuff) {
		delete modelVertBuff;
		delete[] modelVertCubes;
		modelVertBuff = NULL;
		modelVertCubes = NULL;
		modelOriginBuff = NULL;
		scaleTexinfos.clear();
		modelEdges.clear();
		modelVerts.clear();
		modelFaceVerts.clear();
	}

	if (!pickInfo.valid || pickInfo.modelIdx <= 0) {
		originSelected = false;
		modelUsesSharedStructures = false;
		updateSelectionSize();
		return;
	}

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	int modelIdx = map->ents[pickInfo.entIdx]->getBspModelIdx();

	if (modelOriginBuff) {
		delete modelOriginBuff;
	}

	if (pickInfo.ent) {
		transformedOrigin = oldOrigin = pickInfo.ent->getOrigin();
	}
	
	modelOriginBuff = new VertexBuffer(colorShader, COLOR_4B | POS_3F, &modelOriginCube, 6 * 6);

	updateSelectionSize();

	modelUsesSharedStructures = map->does_model_use_shared_structures(modelIdx);

	if (!map->is_convex(modelIdx)) {
		return;
	}

	scaleTexinfos = map->getScalableTexinfos(modelIdx);
	map->getModelPlaneIntersectVerts(pickInfo.modelIdx, modelVerts); // for vertex manipulation + scaling
	modelFaceVerts = map->getModelVerts(pickInfo.modelIdx); // for scaling only

	Solid modelSolid;
	if (!getModelSolid(modelVerts, map, modelSolid)) {
		modelVerts.clear();
		modelFaceVerts.clear();
		scaleTexinfos.clear();
		return;
	};
	modelEdges = modelSolid.hullEdges;

	int numCubes = modelVerts.size() + modelEdges.size();
	modelVertCubes = new cCube[numCubes];
	modelVertBuff = new VertexBuffer(colorShader, COLOR_4B | POS_3F, modelVertCubes, 6 * 6 * numCubes);
	//logf("%d intersection points\n", modelVerts.size());
}

void Renderer::updateSelectionSize() {
	selectionSize = vec3();

	if (!pickInfo.valid || !pickInfo.map) {
		return;
	}
	
	if (pickInfo.modelIdx == 0) {
		vec3 mins, maxs;
		pickInfo.map->get_bounding_box(mins, maxs);
		selectionSize = maxs - mins;
	}
	else if (pickInfo.modelIdx > 0) {
		vec3 mins, maxs;
		if (pickInfo.map->models[pickInfo.modelIdx].nFaces == 0) {
			mins = pickInfo.map->models[pickInfo.modelIdx].nMins;
			maxs = pickInfo.map->models[pickInfo.modelIdx].nMaxs;
		}
		else {
			pickInfo.map->get_model_vertex_bounds(pickInfo.modelIdx, mins, maxs);
		}
		selectionSize = maxs - mins;
	}
	else if (pickInfo.ent) {
		EntCube* cube = pointEntRenderer->getEntCube(pickInfo.ent);
		if (cube)
			selectionSize = cube->maxs - cube->mins;
	}
}

void Renderer::updateEntConnections() {
	if (entConnections) {
		delete entConnections;
		delete entConnectionPoints;
		entConnections = NULL;
		entConnectionPoints = NULL;
	}

	if (!(g_render_flags & RENDER_ENT_CONNECTIONS)) {
		return;
	}

	if (pickInfo.valid && pickInfo.map && pickInfo.ent) {
		Bsp* map = pickInfo.map;
		vector<string> targetNames = pickInfo.ent->getTargets();
		vector<Entity*> targets;
		vector<Entity*> callers;
		vector<Entity*> callerAndTarget; // both a target and a caller
		string thisName;
		if (pickInfo.ent->hasKey("targetname")) {
			thisName = pickInfo.ent->keyvalues["targetname"];
		}

		for (int k = 0; k < map->ents.size(); k++) {
			Entity* ent = map->ents[k];

			if (k == pickInfo.entIdx)
				continue;
			
			bool isTarget = false;
			if (ent->hasKey("targetname")) {
				string tname = ent->keyvalues["targetname"];
				for (int i = 0; i < targetNames.size(); i++) {
					if (tname == targetNames[i]) {
						isTarget = true;
						break;
					}
				}
			}

			bool isCaller = thisName.length() && ent->hasTarget(thisName);

			if (isTarget && isCaller) {
				callerAndTarget.push_back(ent);
			}
			else if (isTarget) {
				targets.push_back(ent);
			}
			else if (isCaller) {
				callers.push_back(ent);
			}			
		}

		if (targets.empty() && callers.empty() && callerAndTarget.empty()) {
			return;
		}

		int numVerts = targets.size() * 2 + callers.size() * 2 + callerAndTarget.size() * 2;
		int numPoints = callers.size() + targets.size() + callerAndTarget.size();
		cVert* lines = new cVert[numVerts];
		cCube* points = new cCube[numPoints];

		const COLOR4 targetColor = { 255, 255, 0, 255 };
		const COLOR4 callerColor = { 0, 255, 255, 255 };
		const COLOR4 bothColor = { 0, 255, 0, 255 };

		vec3 srcPos = getEntOrigin(map, pickInfo.ent).flip();
		int idx = 0;
		int cidx = 0;
		float s = 1.5f;
		vec3 extent = vec3(s,s,s);

		for (int i = 0; i < targets.size(); i++) {
			vec3 ori = getEntOrigin(map, targets[i]).flip();
			points[cidx++] = cCube(ori - extent, ori + extent, targetColor);
			lines[idx++] = cVert(srcPos, targetColor);
			lines[idx++] = cVert(ori, targetColor);
		}
		for (int i = 0; i < callers.size(); i++) {
			vec3 ori = getEntOrigin(map, callers[i]).flip();
			points[cidx++] = cCube(ori - extent, ori + extent, callerColor);
			lines[idx++] = cVert(srcPos, callerColor);
			lines[idx++] = cVert(ori, callerColor);
		}
		for (int i = 0; i < callerAndTarget.size(); i++) {
			vec3 ori = getEntOrigin(map, callerAndTarget[i]).flip();
			points[cidx++] = cCube(ori - extent, ori + extent, bothColor);
			lines[idx++] = cVert(srcPos, bothColor);
			lines[idx++] = cVert(ori, bothColor);
		}

		entConnections = new VertexBuffer(colorShader, COLOR_4B | POS_3F, lines, numVerts);
		entConnectionPoints = new VertexBuffer(colorShader, COLOR_4B | POS_3F, points, numPoints * 6 * 6);
		entConnections->ownData = true;
		entConnectionPoints->ownData = true;
	}
}

void Renderer::updateEntConnectionPositions() {
	if (entConnections && pickInfo.valid && pickInfo.ent) {
		vec3 pos = getEntOrigin(pickInfo.map, pickInfo.ent).flip();

		cVert* verts = (cVert*)entConnections->data;
		for (int i = 0; i < entConnections->numVerts; i += 2) {
			verts[i].x = pos.x;
			verts[i].y = pos.y;
			verts[i].z = pos.z;
		}
	}
}

bool Renderer::getModelSolid(vector<TransformVert>& hullVerts, Bsp* map, Solid& outSolid) {
	outSolid.faces.clear();
	outSolid.hullEdges.clear();
	outSolid.hullVerts.clear();
	outSolid.hullVerts = hullVerts;

	// get verts for each plane
	std::map<int, vector<int>> planeVerts;
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

		vec3 orderedVertsNormal = getNormalFromVerts(tempVerts);

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
				logf("ERROR: Edge connected to %d planes!\n", planeCount);
				return false;
			}

			outSolid.hullEdges.push_back(edge);
		}
	}

	return true;
}

void Renderer::scaleSelectedObject(float x, float y, float z) {
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

void Renderer::scaleSelectedObject(vec3 dir, vec3 fromDir) {
	if (!pickInfo.valid || pickInfo.modelIdx <= 0)
		return;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;

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
	invalidSolid = !pickInfo.map->vertex_manipulation_sync(pickInfo.modelIdx, modelVerts, false, false);

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

	invalidSolid = !pickInfo.map->vertex_manipulation_sync(pickInfo.modelIdx, modelVerts, true, false);
	mapRenderers[pickInfo.mapIdx]->refreshModel(pickInfo.ent->getBspModelIdx());
}

void Renderer::splitFace() {
	BspRenderer* mapRenderer = mapRenderers[pickInfo.mapIdx];
	Bsp* map = pickInfo.map;

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

	BSPPLANE& splitPlane = pickInfo.map->planes[commonPlane];
	vec3 splitPoints[2] = {
		getEdgeControlPoint(modelVerts, edge1),
		getEdgeControlPoint(modelVerts, edge2)
	};

	vector<int> modelPlanes;
	BSPMODEL& model = map->models[pickInfo.ent->getBspModelIdx()];
	pickInfo.map->getNodePlanes(model.iHeadnodes[0], modelPlanes);

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
	if (!map->getModelPlaneIntersectVerts(pickInfo.ent->getBspModelIdx(), modelPlanes, newHullVerts)) {
		logf("Can't split here because the model would not be convex\n");
		return;
	}

	Solid newSolid;
	if (!getModelSolid(newHullVerts, pickInfo.map, newSolid)) {
		logf("Splitting here would invalidate the solid\n");
		return;
	}

	// test that all planes have at least 3 verts
	{
		std::map<int, vector<vec3>> planeVerts;
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
		BSPMODEL& oldModel = map->models[pickInfo.ent->getBspModelIdx()];
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

	int modelIdx = map->create_solid(newSolid, pickInfo.ent->getBspModelIdx());

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

void Renderer::scaleSelectedVerts(float x, float y, float z) {

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

	invalidSolid = !pickInfo.map->vertex_manipulation_sync(pickInfo.modelIdx, modelVerts, true, false);
	mapRenderers[pickInfo.mapIdx]->refreshModel(pickInfo.ent->getBspModelIdx());
	updateSelectionSize();
}

vec3 Renderer::getEdgeControlPoint(vector<TransformVert>& hullVerts, HullEdge& edge) {
	vec3 v0 = hullVerts[edge.verts[0]].pos;
	vec3 v1 = hullVerts[edge.verts[1]].pos;
	return v0 + (v1 - v0) * 0.5f;
}

vec3 Renderer::getCentroid(vector<TransformVert>& hullVerts) {
	vec3 centroid;
	for (int i = 0; i < hullVerts.size(); i++) {
		centroid += hullVerts[i].pos;
	}
	return centroid / (float)hullVerts.size();
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
	movingEnt = true;
	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	vec3 mapOffset = mapRenderers[pickInfo.mapIdx]->mapOffset;
	vec3 localCamOrigin = cameraOrigin - mapOffset;
	grabDist = (getEntOrigin(map, map->ents[pickInfo.entIdx]) - localCamOrigin).length();
	grabStartOrigin = localCamOrigin + cameraForward * grabDist;
	grabStartEntOrigin = localCamOrigin + cameraForward * grabDist;
}

void Renderer::cutEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;

	if (copiedEnt != NULL)
		delete copiedEnt;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	copiedEnt = new Entity();
	*copiedEnt = *map->ents[pickInfo.entIdx];
	
	DeleteEntityCommand* deleteCommand = new DeleteEntityCommand("Cut Entity", pickInfo);
	deleteCommand->execute();
	pushUndoCommand(deleteCommand);
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

	if (pickInfo.map == NULL) {
		logf("Select a map before pasting an ent\n");
		return;
	}

	Bsp* map = pickInfo.map;

	Entity* insertEnt = new Entity();
	*insertEnt = *copiedEnt;

	if (!noModifyOrigin) {
		// can't just set camera origin directly because solid ents can have (0,0,0) origins
		vec3 oldOrigin = getEntOrigin(map, insertEnt);
		vec3 modelOffset = getEntOffset(map, insertEnt);
		vec3 mapOffset = mapRenderers[pickInfo.mapIdx]->mapOffset;

		vec3 moveDist = (cameraOrigin + cameraForward * 100) - oldOrigin;
		vec3 newOri = (oldOrigin + moveDist) - (modelOffset + mapOffset);
		vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
		insertEnt->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
	}

	CreateEntityCommand* createCommand = new CreateEntityCommand("Paste Entity", pickInfo.mapIdx, insertEnt);
	delete insertEnt;
	createCommand->execute();
	pushUndoCommand(createCommand);

	pickInfo.valid = true;
	selectEnt(map, map->ents.size() - 1);
}

void Renderer::deleteEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;

	DeleteEntityCommand* deleteCommand = new DeleteEntityCommand("Delete Entity", pickInfo);
	deleteCommand->execute();
	pushUndoCommand(deleteCommand);
}

void Renderer::deselectObject() {
	pickInfo.ent = NULL;
	pickInfo.entIdx = -1;
	pickInfo.faceIdx = -1;
	pickInfo.modelIdx = -1;
	isTransformableSolid = true;
	modelUsesSharedStructures = false;
	hoverVert = -1;
	hoverEdge = -1;
	hoverAxis = -1;
	updateEntConnections();
}

void Renderer::deselectFaces() {
	for (int i = 0; i < selectedFaces.size(); i++) {
		mapRenderers[selectMapIdx]->highlightFace(selectedFaces[i], false);
	}
	selectedFaces.clear();
}

void Renderer::selectEnt(Bsp* map, int entIdx) {
	pickInfo.entIdx = entIdx;
	pickInfo.ent = map->ents[entIdx];
	pickInfo.modelIdx = entIdx == 0 ? 0 : pickInfo.ent->getBspModelIdx();
	updateSelectionSize();
	updateEntConnections();
	updateEntityState(pickInfo.ent);
	if (pickInfo.ent->isBspModel())
		saveLumpState(pickInfo.map, 0xffffffff, true);
	pickCount++; // force transform window update
}

void Renderer::goToCoords(float x, float y, float z)
{
	cameraOrigin.x = x;
	cameraOrigin.y = y;
	cameraOrigin.z = z;
}

void Renderer::goToEnt(Bsp* map, int entIdx) {
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

void Renderer::ungrabEnt() {
	if (!movingEnt) {
		return;
	}

	movingEnt = false;

	pushEntityUndoState("Move Entity");
}

void Renderer::updateEntityState(Entity* ent) {
	if (!undoEntityState) {
		undoEntityState = new Entity();
	}
	*undoEntityState = *ent;
	undoEntOrigin = ent->getOrigin();
}

void Renderer::saveLumpState(Bsp* map, int targetLumps, bool deleteOldState) {
	if (deleteOldState) {
		for (int i = 0; i < HEADER_LUMPS; i++) {
			if (undoLumpState.lumps[i])
				delete[] undoLumpState.lumps[i];
		}
	}

	undoLumpState = map->duplicate_lumps(targetLumps);
}

void Renderer::pushEntityUndoState(string actionDesc) {
	if (!pickInfo.valid || !pickInfo.ent || !undoEntityState) {
		logf("Invalid entity undo state push\n");
		return;
	}

	bool anythingToUndo = true;
	if (undoEntityState->keyOrder.size() == pickInfo.ent->keyOrder.size()) {
		bool keyvaluesDifferent = false;
		for (int i = 0; i < undoEntityState->keyOrder.size(); i++) {
			string oldKey = undoEntityState->keyOrder[i];
			string newKey = pickInfo.ent->keyOrder[i];
			if (oldKey != newKey) {
				keyvaluesDifferent = true;
				break;
			}
			string oldVal = undoEntityState->keyvalues[oldKey];
			string newVal = pickInfo.ent->keyvalues[oldKey];
			if (oldVal != newVal) {
				keyvaluesDifferent = true;
				break;
			}
		}

		anythingToUndo = keyvaluesDifferent;
	}

	if (!anythingToUndo) {
		return; // nothing to undo
	}

	pushUndoCommand(new EditEntityCommand(actionDesc, pickInfo, undoEntityState, pickInfo.ent));
	updateEntityState(pickInfo.ent);
}

void Renderer::pushModelUndoState(string actionDesc, int targetLumps) {
	if (!pickInfo.valid || !pickInfo.ent || pickInfo.modelIdx <= 0) {
		return;
	}
	
	LumpState newLumps = pickInfo.map->duplicate_lumps(targetLumps);

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
	saveLumpState(pickInfo.map, 0xffffffff, false);

	// entity origin edits also update the ent origin (TODO: this breaks when moving + scaling something)
	updateEntityState(pickInfo.ent);
}

void Renderer::pushUndoCommand(Command* cmd) {
	undoHistory.push_back(cmd);
	clearRedoCommands();

	while (!undoHistory.empty() && undoHistory.size() > undoLevels) {
		delete undoHistory[0];
		undoHistory.erase(undoHistory.begin());
	}

	calcUndoMemoryUsage();
}

void Renderer::undo() {
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

void Renderer::redo() {
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

void Renderer::clearUndoCommands() {
	for (int i = 0; i < undoHistory.size(); i++) {
		delete undoHistory[i];
	}

	undoHistory.clear();
	calcUndoMemoryUsage();
}

void Renderer::clearRedoCommands() {
	for (int i = 0; i < redoHistory.size(); i++) {
		delete redoHistory[i];
	}

	redoHistory.clear();
	calcUndoMemoryUsage();
}

void Renderer::calcUndoMemoryUsage() {
	undoMemoryUsage = (undoHistory.size() + redoHistory.size()) * sizeof(Command*);

	for (int i = 0; i < undoHistory.size(); i++) {
		undoMemoryUsage += undoHistory[i]->memoryUsage();
	}
	for (int i = 0; i < redoHistory.size(); i++) {
		undoMemoryUsage += redoHistory[i]->memoryUsage();
	}
}
