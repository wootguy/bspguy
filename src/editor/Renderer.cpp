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
#include "LeafNavMesh.h"
#include "LeafNavMeshGenerator.h"
#include <algorithm>
#include "BspMerger.h"
#include "MdlRenderer.h"
#include "SprRenderer.h"
#include <unordered_set>

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

	g_app->handleResize(width, height);
}

void window_pos_callback(GLFWwindow* window, int x, int y)
{
	g_settings.windowX = x;
	g_settings.windowY = y;
}

void window_maximize_callback(GLFWwindow* window, int maximized)
{
	g_settings.maximized = maximized == GLFW_TRUE;

	int width, height;
	glfwGetWindowSize(window, &width, &height);
	g_app->handleResize(width, height);
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

void window_focus_callback(GLFWwindow* window, int focused)
{
	g_app->isFocused = focused;
}

void cursor_enter_callback(GLFWwindow* window, int entered)
{
	g_app->isHovered = entered;
}

void window_iconify_callback(GLFWwindow* window, int iconified)
{
	g_app->isIconified = iconified;
}

void file_drop_callback(GLFWwindow* window, int count, const char** paths) {
	g_app->openMap(paths[0]);
}

GLFWmonitor* GetMonitorForWindow(GLFWwindow* window) {
	int winX, winY, winWidth, winHeight;
	glfwGetWindowPos(window, &winX, &winY);
	glfwGetWindowSize(window, &winWidth, &winHeight);

	int monitorCount;
	GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
	GLFWmonitor* bestMonitor = nullptr;
	int bestOverlap = 0;

	for (int i = 0; i < monitorCount; i++) {
		int monX, monY, monWidth, monHeight;
		glfwGetMonitorWorkarea(monitors[i], &monX, &monY, &monWidth, &monHeight);

		int overlapWidth = max(0, min(winX + winWidth, monX + monWidth) - max(winX, monX));
		int overlapHeight = max(0, min(winY + winHeight, monY + monHeight) - max(winY, monY));
		int overlapArea = overlapWidth * overlapHeight;

		if (overlapArea > bestOverlap) {
			bestMonitor = monitors[i];
			bestOverlap = overlapArea;
		}
	}

	return bestMonitor;
}

Renderer::Renderer() {
	programStartTime = glfwGetTime();
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
	
	glfwSetWindowSizeLimits(window, 640, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);

	if (g_settings.valid) {
		glfwSetWindowPos(window, g_settings.windowX, g_settings.windowY);
		
		// setting size again to fix issue where window is too small because it was
		// moved to a monitor with a different DPI than the one it was created for
		glfwSetWindowSize(window, g_settings.windowWidth, g_settings.windowHeight);
		if (g_settings.maximized) {
			glfwMaximizeWindow(window);
		}

		// don't let the window load off-screen
		int left, top, right, bottom;
		int monX, monY, monWidth, monHeight;
		GLFWmonitor* monitor = GetMonitorForWindow(window);
		glfwGetWindowFrameSize(window, &left, &top, &right, &bottom);

		if (!monitor) {
			g_settings.windowX = left;
			g_settings.windowY = top;
			glfwSetWindowPos(window, g_settings.windowX, g_settings.windowY);
		}
		else {
			glfwGetMonitorWorkarea(monitor, &monX, &monY, &monWidth, &monHeight);
			if (g_settings.windowX + left < monX || g_settings.windowY + top < monY) {

				g_settings.windowX = max(g_settings.windowX, monX + left);
				g_settings.windowY = max(g_settings.windowY, monY + top);
				glfwSetWindowPos(window, g_settings.windowX, g_settings.windowY);
			}
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
	glfwSetWindowFocusCallback(window, window_focus_callback);
	glfwSetCursorEnterCallback(window, cursor_enter_callback);
	glfwSetWindowIconifyCallback(window, window_iconify_callback);
	glfwSetDropCallback(window, file_drop_callback);

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
	colorShader->setVertexAttributeNames("vPosition", "vColor", NULL, NULL);

	mdlShader = new ShaderProgram(g_shader_mdl_vertex, g_shader_mdl_fragment);
	mdlShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	mdlShader->setMatrixNames(NULL, "modelViewProjection");
	mdlShader->setVertexAttributeNames("vPosition", NULL, "vTex", "vNormal");

	sprShader = new ShaderProgram(g_shader_spr_vertex, g_shader_spr_fragment);
	sprShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	sprShader->setMatrixNames(NULL, "modelViewProjection");
	sprShader->setVertexAttributeNames("vPosition", NULL, "vTex", NULL);

	vec3Shader = new ShaderProgram(g_shader_vec3_vertex, g_shader_vec3_fragment);
	vec3Shader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	vec3Shader->setMatrixNames(NULL, "modelViewProjection");
	vec3Shader->setVertexAttributeNames("vPosition", NULL, NULL, NULL);

	colorShader->bind();
	u_colorMultId = glGetUniformLocation(colorShader->ID, "colorMult");
	glUniform4f(u_colorMultId, 1, 1, 1, 1);

	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

	g_app = this;

	g_progress.simpleMode = true;

	pointEntRenderer = new PointEntRenderer(NULL, vector<Fgd*>(), colorShader);

	loadSettings();

	reloading = true;
	fgdFuture = async(launch::async, &Renderer::loadFgds, this);

	memset(&undoLumpState, 0, sizeof(LumpState));

	//cameraOrigin = vec3(0, 0, 0);
	//cameraAngles = vec3(0, 0, 0);
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
		moveAxes.dimColor[1] = { 0, 160, 0, 255 };
		moveAxes.dimColor[2] = { 0, 0, 220, 255 };
		moveAxes.dimColor[3] = { 160, 160, 160, 255 };

		moveAxes.hoverColor[0] = { 128, 64, 255, 255 };
		moveAxes.hoverColor[1] = { 64, 255, 64, 255 };
		moveAxes.hoverColor[2] = { 64, 64, 255, 255 };
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
	while (!glfwWindowShouldClose(window))
	{
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

		isLoading = reloading;
		
		// draw opaque world/entity faces
		mapRenderer->render(pickInfo.ents, transformTarget == TRANSFORM_VERTEX, clipnodeRenderHull, false);
		// studio models have transparent boxes that need to draw over the world but behind transparent
		// brushes like a trigger_once which is rendered using the clipnode model
		drawModelsAndSprites();
		
		// draw transparent entity faces
		mapRenderer->render(pickInfo.ents, transformTarget == TRANSFORM_VERTEX, clipnodeRenderHull, true);

		if (!mapRenderer->isFinishedLoading()) {
			isLoading = true;
		}

		model.loadIdentity();
		colorShader->bind();

		colorShader->bind();
		drawEntDirectionVectors(); // draws over world faces
		drawEntConnections();
		drawTextureAxes();

		int modelIdx = pickInfo.getModelIndex();

		if (true) {
			if (debugClipnodes && modelIdx > 0) {
				BSPMODEL* pickModel = pickInfo.getModel();
				glDisable(GL_CULL_FACE);
				int currentPlane = 0;
				drawClipnodes(pickInfo.getMap(), pickModel->iHeadnodes[1], currentPlane, debugInt);
				debugIntMax = currentPlane-1;
				glEnable(GL_CULL_FACE);
			}

			if (debugNodes && modelIdx > 0) {
				BSPMODEL* pickModel = pickInfo.getModel();
				glDisable(GL_CULL_FACE);
				int currentPlane = 0;
				drawNodes(pickInfo.getMap(), pickModel->iHeadnodes[0], currentPlane, debugNode);
				debugNodeMax = currentPlane - 1;
				glEnable(GL_CULL_FACE);
			}

			if ((g_render_flags & (RENDER_ORIGIN | RENDER_MAP_BOUNDARY)) || hasCullbox) {
				colorShader->bind();
				model.loadIdentity();
				colorShader->pushMatrix(MAT_MODEL);
				if (pickInfo.getEnt()) {
					vec3 offset = mapRenderer->mapOffset.flip();
					model.translate(offset.x, offset.y, offset.z);
				}
				colorShader->updateMatrixes();
				glDisable(GL_CULL_FACE);

				if (g_render_flags & RENDER_ORIGIN) {
					drawLine(debugPoint - vec3(32, 0, 0), debugPoint + vec3(32, 0, 0), { 128, 128, 255, 255 });
					drawLine(debugPoint - vec3(0, 32, 0), debugPoint + vec3(0, 32, 0), { 0, 255, 0, 255 });
					drawLine(debugPoint - vec3(0, 0, 32), debugPoint + vec3(0, 0, 32), { 0, 0, 255, 255 });
				}
				
				if (g_render_flags & RENDER_MAP_BOUNDARY) {
					drawBox(mapRenderer->map->ents[0]->getOrigin() * -1, g_limits.max_mapboundary * 2, COLOR4(0, 255, 0, 64));
				}

				if (hasCullbox) {
					drawBox(cullMins, cullMaxs, COLOR4(255, 0, 0, 64));
				}

				glEnable(GL_CULL_FACE);
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
		bool isTransformingWorld = pickInfo.getEntIndex() == 0 && transformTarget != TRANSFORM_OBJECT;
		if (showDragAxes && !movingEnt && !isTransformingWorld && pickInfo.getEntIndex() >= 0 && (isTransformingValid || isMovingOrigin)) {
			drawTransformAxes();
		}

		if (modelIdx > 0 && pickMode == PICK_OBJECT) {
			if (transformTarget == TRANSFORM_VERTEX && isTransformableSolid) {
				drawModelVerts();
			}
			if (transformTarget == TRANSFORM_ORIGIN) {
				drawModelOrigin();
			}
		}

		if (g_app->debugPoly.isValid)
			drawPolygon3D(g_app->debugPoly, COLOR4(0, 255, 255, 150));
		if (g_app->debugLine0 != g_app->debugLine1) {
			drawLine(debugLine0, debugLine1, { 128, 0, 255, 255 });
			drawLine(debugLine2, debugLine3, { 0, 255, 0, 255 });
			drawLine(debugLine4, debugLine5, { 255, 128, 0, 255 });
		}

		const bool navmeshwipcode = false;
		if (navmeshwipcode) {
			colorShader->bind();
			model.loadIdentity();
			colorShader->updateMatrixes();
			glDisable(GL_CULL_FACE);

			glLineWidth(128.0f);
			drawLine(debugLine0, debugLine1, { 255, 0, 0, 255 });
			
			drawLine(debugTraceStart, debugTrace.vecEndPos, COLOR4(255, 0, 0, 255));
			
			Bsp* map = mapRenderer->map;

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
						Bsp* map = mapRenderer->map;
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

			if (!debugLeafNavMesh && !isLoading) {
				LeafNavMesh* navMesh = LeafNavMeshGenerator().generate(map);
				debugLeafNavMesh = navMesh;
			}

			if (debugLeafNavMesh && !isLoading) {
				glLineWidth(1);

				debugLeafNavMesh->refreshNodes(map);

				glEnable(GL_DEPTH_TEST);
				glEnable(GL_CULL_FACE);
				
				int leafNavIdx = debugLeafNavMesh->getNodeIdx(map, cameraOrigin);

				// draw split leaves
				for (int i = 0; i < debugLeafNavMesh->nodes.size(); i++) {
					LeafNode& node = debugLeafNavMesh->nodes[i];

					if (node.childIdx != NAV_INVALID_IDX) {
						continue;
					}

					if (!node.face_buffer) {
						mapRenderer->generateSingleLeafNavMeshBuffer(&node);

						if (!node.face_buffer) {
							continue;
						}
					}
						
					node.face_buffer->draw(GL_TRIANGLES);
					node.wireframe_buffer->draw(GL_LINES);
				}

				glDisable(GL_CULL_FACE);
				glDisable(GL_DEPTH_TEST);

				if (leafNavIdx >= 0 && leafNavIdx < debugLeafNavMesh->nodes.size()) {

					if (pickInfo.getEnt() && pickInfo.getEntIndex() != 0) {
						glDisable(GL_DEPTH_TEST);
						
						int endNode = debugLeafNavMesh->getNodeIdx(map, pickInfo.getEnt());
						//vector<int> route = debugLeafNavMesh->AStarRoute(leafNavIdx, endNode);
						vector<int> route = debugLeafNavMesh->dijkstraRoute(leafNavIdx, endNode);

						if (route.size()) {
							LeafNode* lastNode = &debugLeafNavMesh->nodes[route[0]];

							vec3 lastPos = lastNode->origin;
							drawBox(lastNode->origin, 2, COLOR4(0, 255, 255, 255));

							for (int i = 1; i < route.size(); i++) {
								LeafNode& node = debugLeafNavMesh->nodes[route[i]];

								vec3 nodeCenter = node.origin;

								for (int k = 0; k < lastNode->links.size(); k++) {
									LeafLink& link = lastNode->links[k];

									if (link.node == route[i]) {
										vec3 linkPoint = link.pos;

										if (link.baseCost > 16000) {
											drawLine(lastPos, linkPoint, COLOR4(255, 0, 0, 255));
											drawLine(linkPoint, node.origin, COLOR4(255, 0, 0, 255));
										}
										else if (link.baseCost > 0) {
											drawLine(lastPos, linkPoint, COLOR4(255, 64, 0, 255));
											drawLine(linkPoint, node.origin, COLOR4(255, 64, 0, 255));
										}
										else if (link.costMultiplier > 99.0f) {
											drawLine(lastPos, linkPoint, COLOR4(255, 255, 0, 255));
											drawLine(linkPoint, node.origin, COLOR4(255, 255, 0, 255));
										}
										else if (link.costMultiplier > 9.0f) {
											drawLine(lastPos, linkPoint, COLOR4(255, 0, 255, 255));
											drawLine(linkPoint, node.origin, COLOR4(255, 0, 255, 255));
										}
										else if (link.costMultiplier > 1.9f) {
											drawLine(lastPos, linkPoint, COLOR4(64, 255, 0, 255));
											drawLine(linkPoint, node.origin, COLOR4(64, 255, 0, 255));
										}
										else {
											drawLine(lastPos, linkPoint, COLOR4(0, 255, 255, 255));
											drawLine(linkPoint, node.origin, COLOR4(0, 255, 255, 255));
										}
										drawBox(nodeCenter, 2, COLOR4(0, 255, 255, 255));
										lastPos = nodeCenter;
										break;
									}
								}

								lastNode = &node;
							}

							drawLine(lastPos, pickInfo.getEnt()->getHullOrigin(map), COLOR4(0, 255, 255, 255));
						}
					}
					else {
						LeafNode& node = debugLeafNavMesh->nodes[leafNavIdx];

						drawBox(node.origin, 2, COLOR4(0, 255, 0, 255));

						std::string linkStr;

						for (int i = 0; i < node.links.size(); i++) {
							LeafLink& link = node.links[i];
							if (link.node == -1) {
								break;
							}
							LeafNode& linkLeaf = debugLeafNavMesh->nodes[link.node];
							if (linkLeaf.childIdx != NAV_INVALID_IDX) {
								continue;
							}

							Polygon3D& linkArea = link.linkArea;

							if (link.baseCost > 16000) {
								drawLine(node.origin, link.pos, COLOR4(255, 0, 0, 255));
								drawLine(link.pos, linkLeaf.origin, COLOR4(255, 0, 0, 255));
							}
							else if (link.baseCost > 0) {
								drawLine(node.origin, link.pos, COLOR4(255, 128, 0, 255));
								drawLine(link.pos, linkLeaf.origin, COLOR4(255, 128, 0, 255));
							}
							else if (link.costMultiplier > 99.0f) {
								drawLine(node.origin, link.pos, COLOR4(255, 255, 0, 255));
								drawLine(link.pos, linkLeaf.origin, COLOR4(255, 255, 0, 255));
							}
							else if (link.costMultiplier > 9.0f) {
								drawLine(node.origin, link.pos, COLOR4(255, 0, 255, 255));
								drawLine(link.pos, linkLeaf.origin, COLOR4(255, 0, 255, 255));
							}
							else if (link.costMultiplier > 1.9f) {
								drawLine(node.origin, link.pos, COLOR4(64, 255, 0, 255));
								drawLine(link.pos, linkLeaf.origin, COLOR4(64, 255, 0, 255));
							}
							else {
								drawLine(node.origin, link.pos, COLOR4(0, 255, 255, 255));
								drawLine(link.pos, linkLeaf.origin, COLOR4(0, 255, 255, 255));
							}

							for (int k = 0; k < linkArea.verts.size(); k++) {
								//drawBox(linkArea.verts[k], 1, COLOR4(255, 255, 0, 255));
							}
							drawBox(link.pos, 1, COLOR4(0, 255, 0, 255));
							drawBox(linkLeaf.origin, 2, COLOR4(0, 255, 255, 255));
							linkStr += to_string(link.node) + " (" + to_string(linkArea.verts.size()) + "v), ";
						
							/*
							for (int k = 0; k < node.links.size(); k++) {
								if (i == k)
									continue;
								drawLine(link.pos, node.links[k].pos, COLOR4(64, 0, 255, 255));
							}
							*/
						}

						//logf("Leaf node idx: %d, links: %s\n", leafNavIdx, linkStr.c_str());
					}

				}
				if (false) {
					// special case: touching on a single edge point
					//Polygon3D poly1({ vec3(213.996979, 202.000000, 362.000000), vec3(213.996979, 202.000000, 198.000824), vec3(213.996979, 105.996414, 198.000824), vec3(213.996979, 105.996414, 362.000000), });
					//Polygon3D poly2({ vec3(80.000969, -496.000000, 266.002014), vec3(310.000000, -496.000000, 266.002014), vec3(310.000000, 106.003876, 266.002014), vec3(80.000999, 106.003876, 266.002014), });

					Polygon3D poly1({ vec3(310.000000, 330.000000, 294.000000), vec3(213.996979, 330.000000, 294.000000), vec3(213.996979, 330.000000, 362.001282), vec3(310.000000, 330.000000, 362.001282), });
					Polygon3D poly2({ vec3(496.000000, -496.000000, 294.000000), vec3(496.000000, 431.998474, 294.000000), vec3(80.002045, 431.998474, 294.000000), vec3(80.002045, -496.000000, 294.000000), });

					vec3 start, end;
					poly1.planeIntersectionLine(poly2, start, end);

					vec3 ipos;
					COLOR4 c1 = poly1.intersect2D(start, end, ipos) ? COLOR4(255, 0, 0, 100) : COLOR4(0, 255, 255, 100);
					COLOR4 c2 = poly2.intersect2D(start, end, ipos) ? COLOR4(255, 0, 0, 100) : COLOR4(0, 255, 255, 100);
					COLOR4 c3 = poly1.intersects(poly2) ? COLOR4(255, 0, 0, 100) : COLOR4(0, 255, 255, 100);

					//drawPolygon3D(Polygon3D(poly1), c3);
					//drawPolygon3D(Polygon3D(poly2), c3);
					//drawLine(start, end, COLOR4(100, 0, 255, 255));

					//drawPolygon3D(g_app->debugPoly, COLOR4(255, 255, 255, 150));
				}

				{
					Polygon3D poly1({ vec3(0,0,-50), vec3(0,100,-50), vec3(0,100,100), vec3(0,0,100) });
					Polygon3D poly2({ vec3(-100,0,0), vec3(-100,100,0), vec3(100,100,0), vec3(100,0,0) });

					static float test = 0;

					float a = cos(test) * 100;
					float b = sin(test) * 200;

					poly1.verts[0] += vec3(b, 0, a);
					poly1.verts[1] += vec3(b, 0, a);

					test += 0.01f;
					poly1 = Polygon3D(poly1.verts);

					vec3 start, end;
					poly1.planeIntersectionLine(poly2, start, end);

					vec3 ipos;
					COLOR4 c1 = poly1.intersect2D(start, end, ipos) ? COLOR4(255, 0, 0, 100) : COLOR4(0, 255, 255, 100);
					COLOR4 c2 = poly2.intersect2D(start, end, ipos) ? COLOR4(255, 0, 0, 100) : COLOR4(0, 255, 255, 100);

					//drawPolygon3D(Polygon3D(poly1), c1);
					//drawPolygon3D(Polygon3D(poly2), c2);
					//drawLine(start, end, COLOR4(100, 0, 255, 255));
				}
				//g_app->debugPoly.print();
				
				/*
				colorShader->pushMatrix(MAT_PROJECTION);
				colorShader->pushMatrix(MAT_VIEW);
				projection.ortho(0, windowWidth, windowHeight, 0, -1.0f, 1.0f);
				view.loadIdentity();
				colorShader->updateMatrixes();

				drawPolygon2D(debugPoly, vec2(800, 100), vec2(500, 500), COLOR4(255, 0, 0, 255));

				colorShader->popMatrix(MAT_PROJECTION);
				colorShader->popMatrix(MAT_VIEW);
				*/
			}

			if (pickInfo.getFace()) {
				BSPFACE& face = *pickInfo.getFace();
				BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];

				vector<vec3> faceVerts;
				for (int e = 0; e < face.nEdges; e++) {
					int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
					BSPEDGE& edge = map->edges[abs(edgeIdx)];
					int vertIdx = edgeIdx >= 0 ? edge.iVertex[0] : edge.iVertex[1];

					faceVerts.push_back(map->verts[vertIdx]);
				}

				Polygon3D poly(faceVerts);
				//vec3 center = poly.center + pickInfo.ent->getOrigin();
				vec3 center = poly.center - poly.plane_z;

				drawLine(center, center + info.vS * -10, { 128, 0, 255, 255 });
				drawLine(center, center + info.vT * -10, { 0, 255, 0, 255 });
				drawLine(center, center + poly.plane_z * -10, { 255, 255, 255, 255 });
			}

			glLineWidth(1);
		}

		vec3 forward, right, up;
		makeVectors(cameraAngles, forward, right, up);
		//logf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

		if (!g_app->hideGui)
			gui->draw();

		if (!isLoading && openMapAfterLoad.size()) {
			openMap(openMapAfterLoad.c_str());
		}

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

		if (!isFocused && !isHovered) {
			sleepms(50);
		}

		if (programStartTime >= 0) {
			debugf("Startup finished in %.2fs\n", glfwGetTime() - programStartTime);
			programStartTime = -1;
		}
	}

	glfwTerminate();
}

void Renderer::postLoadFgds()
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

	for (int i = 0; i < mapRenderer->map->ents.size(); i++) {
		Entity* ent = mapRenderer->map->ents[i];
		if (ent->hasCachedMdl && ent->cachedMdl == NULL) {
			ent->hasCachedMdl = false; // try loading a model from the FGDs
		}
	}

	swapPointEntRenderer = NULL;

	gui->entityReportFilterNeeded = true;
}

void Renderer::postLoadFgdsAndTextures() {
	if (reloading) {
		logf("Previous reload not finished. Aborting reload.");
		return;
	}
	reloading = reloadingGameDir = true;
	fgdFuture = async(launch::async, &Renderer::loadFgds, this);
}

void Renderer::clearMapData() {
	clearUndoCommands();
	clearRedoCommands();

	if (copiedEnts.size()) {
		for (Entity* ent : copiedEnts) {
			if (ent)
				delete ent;
		}
		copiedEnts.clear();
	}

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

void Renderer::reloadMaps() {
	string reloadPath = mapRenderer->map->path;

	clearMapData();
	addMap(new Bsp(reloadPath));

	updateCullBox();

	logf("Reloaded maps\n");
}

void Renderer::updateWindowTitle() {
	string map = mapRenderer->map->path;
	string title = map.empty() ? "bspguy" : getAbsolutePath(map) + " - bspguy";
	glfwSetWindowTitle(window, title.c_str());
}

void Renderer::openMap(const char* fpath) {
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

	clearMapData();
	addMap(map);

	gui->refresh();
	updateCullBox();

	logf("Loaded map: %s\n", map->path.c_str());
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
	zFarMdl = g_settings.zFarMdl;
	fov = g_settings.fov;
	g_render_flags = g_settings.render_flags;
	gui->fontSize = g_settings.fontSize;
	undoLevels = g_settings.undoLevels;
	rotationSpeed = g_settings.rotSpeed;
	moveSpeed = g_settings.moveSpeed;

	gui->shouldReloadFonts = true;

	if (!showDragAxes) {
		transformMode = TRANSFORM_NONE;
	}

	glfwSwapInterval(gui->vsync ? 1 : 0);
}

void Renderer::loadFgds() {
	Fgd* mergedFgd = NULL;

	vector<Fgd*> fgds;

	for (int i = 0; i < g_settings.fgdPaths.size(); i++) {
		string path = g_settings.fgdPaths[i];

		g_parsed_fgds.clear();
		g_parsed_fgds.insert(path);

		string loadPath = findAsset(path);
		if (loadPath.empty()) {
			logf("Missing FGD: %s\n", path.c_str());
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

	swapPointEntRenderer = new PointEntRenderer(mergedFgd, fgds, colorShader);
}

void Renderer::drawModelVerts() {
	if (modelVertBuff == NULL || modelVerts.size() == 0)
		return;
	glClear(GL_DEPTH_BUFFER_BIT);

	Bsp* map = mapRenderer->map;
	Entity* ent = pickInfo.getEnt();
	vec3 mapOffset = mapRenderer->mapOffset;
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

	Bsp* map = mapRenderer->map;
	vec3 mapOffset = mapRenderer->mapOffset;
	Entity* ent = pickInfo.getEnt();

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
		float offset = (g_render_flags & RENDER_ENT_DIRECTIONS) ? 64 : 0;
		model.translate(ori.x, ori.z + offset, -ori.y);
		colorShader->updateMatrixes();
		moveAxes.buffer->draw(GL_TRIANGLES);
	}
}

int glGetErrorDebug() {
	return glGetError();
}

void Renderer::drawEntConnections() {
	if (entConnections && (g_render_flags & RENDER_ENT_CONNECTIONS)) {
		model.loadIdentity();
		colorShader->updateMatrixes();
		entConnections->draw(GL_LINES);
	}
}

void Renderer::updateEntDirectionVectors() {
	if (entDirectionVectors) {
		delete entDirectionVectors;
		entDirectionVectors = NULL;
	}
	
	if (!(g_render_flags & RENDER_ENT_DIRECTIONS)) {
		return;
	}

	vector<Entity*> pickEnts = pickInfo.getEnts();

	if (pickEnts.empty()) {
		return;
	}

	vector<Entity*> directEnts;

	for (Entity* ent : pickEnts) {
		// don't show vectors for point entities or solids that can rotate normally
		// don't show for sprites either unless force rotation is on (the vector makes no sense)
		if ((!ent->isBspModel() || !ent->canRotate()) && (!ent->isSprite() || g_app->forceAngleRotation)) {
			string cname = ent->getClassname();
			FgdClass* clazz = mergedFgd->getFgdClass(cname);
			// show if the FGD says the ent uses angles, or if the fgd is missing and the ent has angles,
			// or if force angles are on
			bool classUsesAngle = clazz ? (clazz->hasKey("angles") || clazz->hasKey("angle")) : false;
			if (classUsesAngle || (!clazz && (ent->hasKey("angles") || ent->hasKey("angle"))) || g_app->forceAngleRotation)
				directEnts.push_back(ent);
		}
	}

	if (directEnts.empty())
		return;

	struct cArrow {
		cCube up;
		cCube right;
		cCube shaft; // minor todo: one face can be omitted. make a new struct
		cPyramid tip;
	};
	int arrowVerts = 6*6*3 + (6 + 3*4);

	int numPointers = directEnts.size();
	cArrow* arrows = new cArrow[numPointers];

	for (int i = 0; i < numPointers; i++) {
		Entity* ent = directEnts[i];
		vec3 ori = getEntOrigin(mapRenderer->map, ent).flip();
		vec3 angles = ent->getAngles() * (PI / 180.0f);

		// i swear every use of entity angles needs a matrix with its own unique order/inversions
		// this is the combo used so far
		mat4x4 rotMat;
		rotMat.loadIdentity();
		rotMat.rotateX(-angles.z);
		rotMat.rotateZ(-angles.x);
		rotMat.rotateY(-angles.y);

		arrows[i].shaft = cCube(vec3(-1, -1, -1), vec3(40, 1, 1), COLOR4(0, 255, 0, 255));
		arrows[i].right = cCube(vec3(-0.5f, -0.5f, -0.5f), vec3(0.5f, 0.5f, 24), COLOR4(128, 0, 255, 255));
		arrows[i].up = cCube(vec3(-0.5f, -0.5f, -0.5f), vec3(0.5f, 24, 0.5f), COLOR4(0, 128, 255, 255));
		arrows[i].tip = cPyramid(vec3(40, 0, 0), 4, 16, COLOR4(0, 255, 0, 255));

		cVert* rawVerts = (cVert*)&arrows[i];
		for (int k = 0; k < arrowVerts; k++) {
			vec3* pos = (vec3*)&rawVerts[k].x;
			*pos = (rotMat * vec4(*pos, 1)).xyz() + ori;
		}
		for (int k = 0; k < arrowVerts; k += 3) {

		}
	}

	entDirectionVectors = new VertexBuffer(colorShader, COLOR_4B | POS_3F, arrows, numPointers * arrowVerts);
	entDirectionVectors->ownData = true;
}

void Renderer::drawEntDirectionVectors() {
	if (!entDirectionVectors) {
		return;
	}

	glCullFace(GL_FRONT);
	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);

	colorShader->bind();
	model.loadIdentity();
	colorShader->updateMatrixes();
	entDirectionVectors->draw(GL_TRIANGLES);

	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
	glCullFace(GL_BACK);
}

void Renderer::updateTextureAxes() {
	if (allTextureAxes) {
		delete allTextureAxes;
		allTextureAxes = NULL;
	}

	if (pickInfo.faces.empty()) {
		return;
	}

	int numVerts = pickInfo.faces.size() * 6;
	cVert* verts = new cVert[numVerts];
	Bsp* map = mapRenderer->map;
	const float len = 16;

	int vidx = 0;
	for (int i = 0; i < pickInfo.faces.size(); i++) {
		int faceidx = pickInfo.faces[i];
		BSPFACE& face = map->faces[faceidx];
		BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
		vec3 center = map->get_face_center(faceidx).flip();
		vec3 norm = crossProduct(info.vT, info.vS).normalize();
		
		verts[vidx++] = cVert(center, COLOR4(255, 255, 0, 255));
		verts[vidx++] = cVert(center + info.vS.flip().normalize(len), COLOR4(255, 255, 0, 255));
		verts[vidx++] = cVert(center, COLOR4(0, 255, 0, 255));
		verts[vidx++] = cVert(center + info.vT.flip().normalize(len), COLOR4(0, 255, 0, 255));
		verts[vidx++] = cVert(center, COLOR4(0, 64, 255, 255));
		verts[vidx++] = cVert(center + norm.flip().normalize(len), COLOR4(0, 64, 255, 255));
	}

	allTextureAxes = new VertexBuffer(colorShader, COLOR_4B | POS_3F, verts, numVerts);
	allTextureAxes->ownData = true;
}

void Renderer::drawTextureAxes() {
	if (!allTextureAxes) {
		return;
	}

	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);

	colorShader->bind();
	model.loadIdentity();
	colorShader->updateMatrixes();
	allTextureAxes->draw(GL_LINES);

	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
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
	
	moveGrabbedEnts();

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
			if (debugLeafNavMesh && pickMode == PICK_OBJECT && !isLoading) {
				Bsp* map = mapRenderer->map;
				debugLeafNavMesh->refreshNodes(map);
				debugInt++;
			}
		}
		else
		{
			gui->showEntityReport = !gui->showEntityReport;
		}
	}

	if (debugLeafNavMesh) {
		if (pressed[GLFW_KEY_G] && !oldPressed[GLFW_KEY_G]) {
			debugInt -= 2;
			Bsp* map = mapRenderer->map;
			debugLeafNavMesh->refreshNodes(map);
			debugInt++;
		}

		if (pressed[GLFW_KEY_H] && !oldPressed[GLFW_KEY_H]) {
			debugInt = 269;
			Bsp* map = mapRenderer->map;
			debugLeafNavMesh->refreshNodes(map);
			debugInt++;
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
				invalidSolid = !pickInfo.getMap()->vertex_manipulation_sync(pickInfo.getModelIndex(), modelVerts, false, true);
				gui->reloadLimits();

				int modelIdx = pickInfo.getModelIndex();
				if (modelIdx >= 0)
					mapRenderer->refreshModel(modelIdx);
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
			pushEntityUndoState("Move Entity");
		}
	}
}

void Renderer::applyTransform(bool forceUpdate) {
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

void Renderer::cameraRotationControls(vec2 mousePos) {
	static double lastTime = 0;
	double now = glfwGetTime();
	double deltaTime = now - lastTime;
	lastTime = now;

	if (pressed[GLFW_KEY_DOWN]) {
		cameraAngles.x += rotationSpeed * deltaTime * 50;
		cameraAngles.x = clamp(cameraAngles.x, -90.0f, 90.0f);
	}
	if (pressed[GLFW_KEY_UP]) {
		cameraAngles.x -= rotationSpeed * deltaTime * 50;
		cameraAngles.x = clamp(cameraAngles.x, -90.0f, 90.0f);
	}
	if (pressed[GLFW_KEY_LEFT]) {
		cameraAngles.z -= rotationSpeed * deltaTime * 50;
	}
	if (pressed[GLFW_KEY_RIGHT]) {
		cameraAngles.z += rotationSpeed * deltaTime * 50;
	}

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

		vec3 offset = (g_render_flags & RENDER_ENT_DIRECTIONS) ? vec3(0, 0, 64) : vec3();
		pickStart -= offset;

		Bsp* map = mapRenderer->map;
		vec3 origin = activeAxes.origin;

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

void Renderer::cameraContextMenus() {
	// context menus
	bool wasTurning = cameraIsRotating && totalMouseDrag.length() >= 1;
	if (draggingAxis == -1 && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE && oldRightMouse != GLFW_RELEASE && !wasTurning) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);

		int entIdx, faceIdx;
		mapRenderer->pickPoly(pickStart, pickDir, clipnodeRenderHull, entIdx, faceIdx);

		if (entIdx != 0 && pickInfo.isEntSelected(entIdx)) {
			gui->openContextMenu(pickInfo.getEntIndex());
		}
		else {
			gui->openContextMenu(-1);
		}
	}
}

void Renderer::moveGrabbedEnts() {
	// grabbing
	if (movingEnt && pickInfo.getEntIndex() > 0) {
		if (g_scroll != oldScroll) {
			float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 4.0f : 2.0f;
			if (pressed[GLFW_KEY_LEFT_CONTROL])
				moveScale = 1.0f;
			if (g_scroll < oldScroll)
				moveScale *= -1;

			grabDist += 16 * moveScale;
		}

		Bsp* map = mapRenderer->map;
		vec3 mapOffset = mapRenderer->mapOffset;
		vec3 delta = ((cameraOrigin - mapOffset) + cameraForward * grabDist) - grabStartOrigin;

		for (int i = 0; i < pickInfo.ents.size(); i++ ) {
			int entidx = pickInfo.ents[i];
			Entity* ent = map->ents[entidx];
			vec3 oldOrigin = grabStartEntOrigin[i];
			vec3 newOrigin = (oldOrigin + delta);
			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

			transformedOrigin = this->oldOrigin = rounded;

			ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
			mapRenderer->refreshEnt(entidx);
		}
		updateEntConnectionPositions();
	}
	else {
		ungrabEnts();
	}
}

void Renderer::shortcutControls() {
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
		if (anyCtrlPressed && pressed[GLFW_KEY_C] && !oldPressed[GLFW_KEY_C]) {
			copyEnts();
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_X] && !oldPressed[GLFW_KEY_X]) {
			cutEnts();
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_V] && !oldPressed[GLFW_KEY_V]) {
			pasteEnts(false);
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_M] && !oldPressed[GLFW_KEY_M]) {
			gui->showTransformWidget = !gui->showTransformWidget;
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_O] && !oldPressed[GLFW_KEY_O]) {
			openMap(NULL);
		}
		if (anyCtrlPressed && anyAltPressed && pressed[GLFW_KEY_S] && !oldPressed[GLFW_KEY_S]) {
			gui->saveAs();
		}
		if (anyAltPressed && anyEnterPressed) {
			gui->showKeyvalueWidget = !gui->showKeyvalueWidget;
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

	bool multiselect = anyCtrlPressed;

	if (!multiselect) {
		// deselect old faces
		for (int faceIdx : pickInfo.faces) {
			mapRenderer->highlightFace(faceIdx, false);
		}

		// update deselected point ents
		for (int entIdx : pickInfo.ents) {
			Entity* ent = pickInfo.getMap()->ents[entIdx];
			if (!ent->isBspModel()) {
				mapRenderer->refreshPointEnt(entIdx);
			}
		}
	}
	
	int oldEntIdx = pickInfo.getEntIndex();
	int clickedEnt, clickedFace;
	mapRenderer->pickPoly(pickStart, pickDir, clipnodeRenderHull, clickedEnt, clickedFace);

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
			if (pickInfo.isEntSelected(clickedEnt)) {
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

			if (clickedEnt != -1) {
				pickInfo.selectEnt(clickedEnt);
			}
		}
		//logf("%d selected ents\n", pickInfo.ents.size());		

		postSelectEnt();

		if (pickInfo.getEnt()) {
			updateModelVerts();
			if (pickInfo.getEnt() && pickInfo.getEnt()->isBspModel())
				saveLumpState(pickInfo.getMap(), 0xffffffff, true);
			pickCount++; // force transform window update
		}

		isTransformableSolid = pickInfo.ents.size() == 1;
		if (isTransformableSolid && pickInfo.getModelIndex() > 0) {
			isTransformableSolid = pickInfo.getMap()->is_convex(pickInfo.getModelIndex());
		}
	}
	else if (pickMode == PICK_FACE) {
		if (multiselect) {
			if (pickInfo.isFaceSelected(clickedFace)) {
				mapRenderer->highlightFace(clickedFace, false);
				pickInfo.deselectFace(clickedFace);
			}
			else if (clickedFace != -1) {
				mapRenderer->highlightFace(clickedFace, true);
				pickInfo.selectFace(clickedFace);
			}
		}
		else {
			pickInfo.deselect();

			if (clickedFace != -1) {
				mapRenderer->highlightFace(clickedFace, true);
				pickInfo.selectFace(clickedFace);
			}
		}
		//logf("%d selected faces\n", pickInfo.faces.size());
		
		gui->showLightmapEditorUpdate = true;
	}

	pickClickHeld = true;

	updateEntConnections();
}

bool Renderer::transformAxisControls() {

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	if (!canTransform || pickClickHeld || pickInfo.getEntIndex() < 0) {
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

vec3 Renderer::getMoveDir()
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

void Renderer::setupView() {
	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	glViewport(0, 0, windowWidth, windowHeight);

	projection.perspective(fov, (float)windowWidth / (float)windowHeight, zNear, zFar);

	view.loadIdentity();
	view.rotateZ(PI * cameraAngles.y / 180.0f);
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}

void Renderer::addMap(Bsp* map) {
	g_settings.addRecentFile(map->path);
	g_settings.save(); // in case the program crashes
	
	if (debugLeafNavMesh) {
		delete debugLeafNavMesh;
		debugLeafNavMesh = NULL;
	}

	mapRenderer = new BspRenderer(map, bspShader, fullBrightBspShader, colorShader, pointEntRenderer);

	gui->checkValidHulls();

	// Pick default map
	//if (!pickInfo.map) 
	{
		pickInfo.deselect();
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
	saveLumpState(map, 0xffffffff, false); // set up initial undo state

	updateWindowTitle();
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

void Renderer::drawBox(vec3 mins, vec3 maxs, COLOR4 color) {
	mins = vec3(mins.x, mins.z, -mins.y);
	maxs = vec3(maxs.x, maxs.z, -maxs.y);

	cCube cube(mins, maxs, color);

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &cube, 6 * 6);
	buffer.draw(GL_TRIANGLES);
}

void Renderer::drawPolygon3D(Polygon3D& poly, COLOR4 color) {
	colorShader->bind();
	model.loadIdentity();
	colorShader->updateMatrixes();
	glDisable(GL_CULL_FACE);

	static cVert verts[64];

	for (int i = 0; i < poly.verts.size() && i < 64; i++) {
		vec3 pos = poly.verts[i];
		verts[i].x = pos.x;
		verts[i].y = pos.z;
		verts[i].z = -pos.y;
		verts[i].c = color;
	}

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, verts, poly.verts.size());
	buffer.draw(GL_TRIANGLE_FAN);
}

float Renderer::drawPolygon2D(Polygon3D poly, vec2 pos, vec2 maxSz, COLOR4 color) {
	vec2 sz = poly.localMaxs - poly.localMins;
	float scale = min(maxSz.y / sz.y, maxSz.x / sz.x);

	vec2 offset = poly.localMins * -scale + pos;

	for (int i = 0; i < poly.verts.size(); i++) {
		vec2 v1 = poly.localVerts[i];
		vec2 v2 = poly.localVerts[(i + 1) % poly.verts.size()];
		drawLine2D(offset + v1*scale, offset + v2 * scale, color);
		if (i == 0) {
			drawLine2D(offset + v1 * scale, offset + (v1 + (v2-v1)*0.5f) * scale, COLOR4(0,255,0,255));
		}
	}

	// draw camera origin in the same coordinate space
	{
		vec2 cam = poly.project(cameraOrigin);
		drawBox2D(offset + cam * scale, 16, poly.isInside(cam) ? COLOR4(0, 255, 0, 255) : COLOR4(255, 32, 0, 255));
	}


	return scale;
}

void Renderer::drawBox2D(vec2 center, float width, COLOR4 color) {
	vec2 pos = vec2(center.x, center.y) - vec2(width*0.5f, width *0.5f);
	cQuad cube(pos.x, pos.y, width, width, color);

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &cube, 6);
	buffer.draw(GL_TRIANGLES);
}

void Renderer::drawPlane(BSPPLANE& plane, COLOR4 color, float sz) {

	vec3 ori = plane.vNormal * plane.fDist;
	vec3 crossDir = fabs(plane.vNormal.z) > 0.9f ? vec3(1, 0, 0) : vec3(0, 0, 1);
	vec3 right = crossProduct(plane.vNormal, crossDir);
	vec3 up = crossProduct(right, plane.vNormal);

	vec3 topLeft = vec3(ori + right * -sz + up * sz).flip();
	vec3 topRight = vec3(ori + right * sz + up * sz).flip();
	vec3 bottomLeft = vec3(ori + right * -sz + up * -sz).flip();
	vec3 bottomRight = vec3(ori + right * sz + up * -sz).flip();

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

BaseRenderer* Renderer::loadModel(Entity* ent) {
	if (ent->hasCachedMdl) {
		return ent->cachedMdl;
	}

	struct ModelKey {
		string name;
		bool isClassname;
	};

	static vector<ModelKey> tryModelKeys = {
		{"model", false},
		{"classname", true},
		{"monstertype", true},
	};

	string model;
	string lowerModel;
	bool foundModelKey = false;
	bool isMdlNotSpr = true;
	for (int i = 0; i < tryModelKeys.size(); i++) {
		ModelKey key = tryModelKeys[i];
		model = ent->getKeyvalue(key.name);

		if (tryModelKeys[i].isClassname) {
			if (g_app->mergedFgd) {
				FgdClass* fgd = g_app->mergedFgd->getFgdClass(ent->getKeyvalue(key.name));
				model = fgd ? fgd->model : "";
				lowerModel = toLowerCase(model);
			}
			else {
				continue;
			}
		}
		else {
			lowerModel = toLowerCase(model);
		}

		bool hasMdlExt = lowerModel.size() > 4 && lowerModel.find(".mdl") == lowerModel.size() - 4;
		bool hasSprExt = lowerModel.size() > 4 && lowerModel.find(".spr") == lowerModel.size() - 4;
		if (hasSprExt || hasMdlExt) {
			foundModelKey = true;
			ent->cachedMdlCname = key.isClassname ? ent->getKeyvalue(key.name) : ent->getClassname();
			isMdlNotSpr = hasMdlExt;
			break;
		}
	}

	if (!foundModelKey) {
		//logf("No model key found for '%s' (%s): %s\n", ent->getKeyvalue("targetname"].c_str(), ent->getKeyvalue("classname"].c_str(), model.c_str());
		ent->hasCachedMdl = true;
		return NULL; // no MDL found
	}

	auto cache = studioModelPaths.find(lowerModel);
	if (cache == studioModelPaths.end()) {
		string findPath = findAsset(model);
		studioModelPaths[lowerModel] = findPath;
		if (!findPath.size()) {
			debugf("Failed to find model for entity '%s' (%s): %s\n",
				ent->getTargetname().c_str(), ent->getClassname().c_str(),
				model.c_str());
			ent->hasCachedMdl = true;
			return NULL;
		}
	}

	string modelPath = studioModelPaths[lowerModel];
	if (!modelPath.size()) {
		//logf("Empty string for model path in entity '%s' (%s): %s\n", ent->getKeyvalue("targetname"].c_str(), ent->getKeyvalue("classname"].c_str(), model.c_str());
		ent->hasCachedMdl = true;
		return NULL;
	}

	auto mdl = studioModels.find(modelPath);
	if (mdl == studioModels.end()) {
		BaseRenderer* newModel = NULL;
		if (isMdlNotSpr) {
			newModel = new MdlRenderer(g_app->mdlShader, modelPath);
		}
		else {
			newModel = new SprRenderer(g_app->sprShader, g_app->vec3Shader, modelPath);
		}
		
		studioModels[modelPath] = newModel;
		ent->cachedMdl = newModel;
		ent->hasCachedMdl = true;
		//logf("Begin load model for entity '%s' (%s): %s\n", ent->getKeyvalue("targetname"].c_str(), ent->getKeyvalue("classname"].c_str(), model.c_str());
		return newModel;
	}

	ent->cachedMdl = mdl->second;
	ent->hasCachedMdl = true;
	return mdl->second;
}

void Renderer::drawModelsAndSprites() {
	if (mapRenderer->map->ents.empty()) {
		return;
	}

	vec3 worldOffset = mapRenderer->map->ents[0]->getOrigin();
	
	colorShader->bind();
	glUniform4f(u_colorMultId, 1.0f, 1.0f, 1.0f, 1.0f);

	if (!(g_render_flags & (RENDER_STUDIO_MDL | RENDER_SPRITES)))
		return

	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);

	int drawCount = 0;

	unordered_set<int> selectedEnts;
	for (int idx : pickInfo.ents) {
		selectedEnts.insert(idx);
	}

	vec3 renderOffset = mapRenderer->mapOffset.flip();

	vec3 camForward, camRight, camUp;
	makeVectors(cameraAngles, camForward, camRight, camUp);

	struct DepthSortedEnt {
		Entity* ent;
		int idx;
		vec3 origin;
		vec3 angles;
		BaseRenderer* mdl;
		float dist; // distance from camera
	};

	
	float aspect = (float)windowWidth / (float)windowHeight;
	Frustum frustum = getViewFrustum(cameraOrigin, cameraAngles, aspect, zNear, zFar, fov);

	vector<DepthSortedEnt> depthSortedMdlEnts;
	for (int i = 0; i < mapRenderer->map->ents.size(); i++) {
		Entity* ent = mapRenderer->map->ents[i];
		DepthSortedEnt sent;
		sent.ent = ent;
		sent.mdl = loadModel(sent.ent);
		sent.ent->didStudioDraw = false;

		if (sent.mdl && sent.mdl->loadState != MDL_LOAD_INITIAL) {
			if (!sent.mdl->valid) {
				logf("Failed to load model: %s\n", sent.mdl->fpath.c_str());
				studioModels[ent->cachedMdl->fpath] = NULL;
				delete sent.mdl;
				ent->cachedMdl = sent.mdl = NULL;
			}
			else if (sent.mdl->loadState == MDL_LOAD_UPLOAD) {
				sent.mdl->upload();
				const char* typ = sent.mdl->isSprite() ? "SPR" : "MDL";
				debugf("Loaded %s: %s\n", typ, sent.mdl->fpath.c_str());
			}
		}

		if (sent.mdl && sent.mdl->loadState == MDL_LOAD_DONE && sent.mdl->valid) {
			if (!ent->drawCached) {
				ent->drawOrigin = ent->getOrigin();
				ent->drawAngles = ent->getAngles();
				ent->drawSequence = atoi(ent->getKeyvalue("sequence").c_str());
				EntRenderOpts opts = ent->getRenderOpts();

				if (sent.mdl->isStudioModel()) {
					vec3 mins, maxs;
					((MdlRenderer*)sent.mdl)->getModelBoundingBox(ent->drawAngles, ent->drawSequence, mins, maxs);
					ent->drawMin = mins + sent.origin;
					ent->drawMax = maxs + sent.origin;
				}
				else {
					vec3 mins, maxs;
					((SprRenderer*)sent.mdl)->getBoundingBox(mins, maxs, opts.scale);
					ent->drawMin = mins + sent.origin;
					ent->drawMax = maxs + sent.origin;
				}
			}

			sent.idx = i;
			sent.origin = ent->drawOrigin;
			sent.dist = dotProduct(sent.origin - cameraOrigin, camForward);

			if (sent.mdl->lastDrawCall == 0) {
				// need to draw at least once to know mins/maxs
				depthSortedMdlEnts.push_back(sent);
				continue;
			}

			if (!sent.ent->drawCached) {
				if (sent.mdl->isStudioModel()) {
					vec3 mins, maxs;
					((MdlRenderer*)sent.mdl)->getModelBoundingBox(ent->drawAngles, ent->drawSequence, mins, maxs);
					ent->drawMin = mins + sent.origin;
					ent->drawMax = maxs + sent.origin;
				}
				else {
					EntRenderOpts opts = ent->getRenderOpts();
					vec3 mins, maxs;
					((SprRenderer*)sent.mdl)->getBoundingBox(mins, maxs, opts.scale);
					ent->drawMin = mins + sent.origin;
					ent->drawMax = maxs + sent.origin;
				}
				ent->drawCached = true;
			}

			if (isBoxInView(ent->drawMin, ent->drawMax, frustum, zFarMdl))
				depthSortedMdlEnts.push_back(sent);
		}
	}
	sort(depthSortedMdlEnts.begin(), depthSortedMdlEnts.end(), [](const DepthSortedEnt& a, const DepthSortedEnt& b) {
		return a.dist > b.dist;
	});

	for (int i = 0; i < depthSortedMdlEnts.size(); i++) {
		Entity* ent = depthSortedMdlEnts[i].ent;
		BaseRenderer* mdl = depthSortedMdlEnts[i].mdl;
		int entidx = depthSortedMdlEnts[i].idx;

		bool isSelected = selectedEnts.count(entidx);

		bool skipRender = mdl->isStudioModel() && !(g_render_flags & RENDER_STUDIO_MDL)
			|| mdl->isSprite() && !(g_render_flags & RENDER_SPRITES);

		if (skipRender)
			continue;

		ent->drawFrame = mdl->drawFrame;

		{ // draw the colored transparent cube
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			//EntCube* entcube = mapRenderer->pointEntRenderer->getEntCube(ent);
			EntCube* entcube = mapRenderer->renderEnts[depthSortedMdlEnts[i].idx].pointEntCube;
			colorShader->bind();
			colorShader->pushMatrix(MAT_MODEL);
			*colorShader->modelMat = mapRenderer->renderEnts[entidx].modelMat;
			colorShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
			colorShader->updateMatrixes();

			if (isSelected) {
				//glDepthFunc(GL_ALWAYS); // ignore depth testing for the world but not for the model
				glUniform4f(u_colorMultId, 1.0f, 1.0f, 1.0f, 1.0f);
				entcube->wireframeBuffer->draw(GL_LINES);
				//glDepthFunc(GL_LESS);

				glDepthMask(GL_FALSE); // let model draw over this
				glUniform4f(u_colorMultId, 1.0f, 1.0f, 1.0f, 0.5f);
				entcube->selectBuffer->draw(GL_TRIANGLES);
				glDepthMask(GL_TRUE);
			}
			else {
				glDepthMask(GL_FALSE);
				glUniform4f(u_colorMultId, 1.0f, 1.0f, 1.0f, 0.5f);
				entcube->buffer->draw(GL_TRIANGLES);
				glDepthMask(GL_TRUE);
				
				glUniform4f(u_colorMultId, 0.0f, 0.0f, 0.0f, 1.0f);
				entcube->wireframeBuffer->draw(GL_LINES);
			}

			colorShader->popMatrix(MAT_MODEL);
		}

		// draw the model
		ent->didStudioDraw = true;
		if (mdl->isStudioModel()) {
			((MdlRenderer*)mdl)->draw(ent->drawOrigin + worldOffset, ent->drawAngles, ent->drawSequence,
				g_app->cameraOrigin, g_app->cameraRight, isSelected ? vec3(1, 0, 0) : vec3(1, 1, 1));
		}
		else if (mdl->isSprite()) {
			EntRenderOpts renderOpts = ent->getRenderOpts();
			((SprRenderer*)mdl)->draw(ent->drawOrigin + worldOffset, ent->drawAngles, renderOpts, isSelected);
		}
		
		drawCount++;

		// debug the model verts bounding box
		if (false && mdl->isStudioModel()) {
			vec3 mins, maxs;
			((MdlRenderer*)mdl)->getModelBoundingBox(ent->drawAngles, ent->drawSequence, mins, maxs);
			mins += ent->drawOrigin;
			maxs += ent->drawOrigin;

			colorShader->bind();
			glUniform4f(u_colorMultId, 1.0f, 1.0f, 1.0f, 1.0f);
			drawBox(mins, maxs, COLOR4(255, 255, 0, 255));
		}
	}

	//logf("Draw %d models\n", drawCount);

	glCullFace(GL_BACK);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	colorShader->bind();
	glUniform4f(u_colorMultId, 1.0f, 1.0f, 1.0f, 1.0f);
}

vec3 Renderer::getEntOrigin(Bsp* map, Entity* ent) {
	return ent->getOrigin() + getEntOffset(map, ent);
}

vec3 Renderer::getEntOffset(Bsp* map, Entity* ent) {
	int modelIdx = ent->getBspModelIdx();
	if (modelIdx > 0 && modelIdx < map->modelCount) {
		BSPMODEL& model = map->models[modelIdx];
		vec3 modelCenter = model.nMins + (model.nMaxs - model.nMins) * 0.5f;

		if (ent->canRotate()) {
			modelCenter = (ent->getRotationMatrix(true) * vec4(modelCenter, 1)).xyz();
		}

		return modelCenter;
	}
	return vec3(0, 0, 0);
}

void Renderer::updateDragAxes() {
	Bsp* map = NULL;
	Entity* ent = NULL;
	vec3 mapOffset;

	if (pickInfo.getEnt()) {
		map = mapRenderer->map;
		ent = pickInfo.getEnt();
		mapOffset = mapRenderer->mapOffset;
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
				scaleAxes.origin += parseVector(ent->getKeyvalue("origin"));
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
		if (pickInfo.getEntIndex() == 0) {
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

	if (!pickInfo.getEnt() || pickInfo.getModelIndex() <= 0) {
		originSelected = false;
		modelUsesSharedStructures = false;
		updateSelectionSize();
		return;
	}

	Bsp* map = mapRenderer->map;
	int modelIdx = pickInfo.getModelIndex();

	if (modelOriginBuff) {
		delete modelOriginBuff;
	}

	if (pickInfo.getEnt()) {
		transformedOrigin = oldOrigin = pickInfo.getOrigin();
	}
	
	modelOriginBuff = new VertexBuffer(colorShader, COLOR_4B | POS_3F, &modelOriginCube, 6 * 6);

	updateSelectionSize();

	modelUsesSharedStructures = map->does_model_use_shared_structures(modelIdx);

	if (!map->is_convex(modelIdx)) {
		return;
	}

	scaleTexinfos = map->getScalableTexinfos(modelIdx);
	map->getModelPlaneIntersectVerts(pickInfo.getModelIndex(), modelVerts); // for vertex manipulation + scaling
	modelFaceVerts = map->getModelVerts(pickInfo.getModelIndex()); // for scaling only

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

void Renderer::updateEntConnections() {
	// todo: these shouldn't be here
	updateCullBox();
	updateEntDirectionVectors();
	updateTextureAxes();
	gui->entityReportReselectNeeded = true;
	
	if (entConnections) {
		delete entConnections;
		delete entConnectionPoints;
		entConnections = NULL;
		entConnectionPoints = NULL;
		entConnectionLinks.clear();
	}

	if (!(g_render_flags & RENDER_ENT_CONNECTIONS)) {
		return;
	}

	if (pickInfo.getMap() && pickInfo.getEnt()) {
		Bsp* map = pickInfo.getMap();

		const COLOR4 targetColor = { 255, 255, 0, 255 };
		const COLOR4 callerColor = { 0, 255, 255, 255 };
		const COLOR4 bothColor = { 0, 255, 0, 255 };

		for (int i = 0; i < pickInfo.ents.size(); i++) {
			int entindx = pickInfo.ents[i];
			Entity* self = map->ents[entindx];
			string selfName = self->getTargetname();

			for (int k = 0; k < map->ents.size(); k++) {
				Entity* ent = map->ents[k];

				if (k == entindx)
					continue;

				
				string tname = ent->getTargetname();
				bool isTarget = tname.size() && self->hasTarget(tname);
				bool isCaller = selfName.length() && ent->hasTarget(selfName);

				EntConnection link;
				memset(&link, 0, sizeof(EntConnection));
				link.self = self;
				link.target = ent;

				if (isTarget && isCaller) {
					link.color = bothColor;
					entConnectionLinks.push_back(link);
				}
				else if (isTarget) {
					link.color = targetColor;
					entConnectionLinks.push_back(link);
				}
				else if (isCaller) {
					link.color = callerColor;
					entConnectionLinks.push_back(link);
				}
			}
		}

		if (entConnectionLinks.empty()) {
			return;
		}

		int numVerts = entConnectionLinks.size() * 2;
		int numPoints = entConnectionLinks.size();
		cVert* lines = new cVert[numVerts];
		cCube* points = new cCube[numPoints];

		int idx = 0;
		int cidx = 0;
		float s = 1.5f;
		vec3 extent = vec3(s,s,s);

		for (int i = 0; i < entConnectionLinks.size(); i++) {
			EntConnection& link = entConnectionLinks[i];
			vec3 srcPos = getEntOrigin(map, link.self).flip();
			vec3 ori = getEntOrigin(map, link.target).flip();
			points[cidx++] = cCube(ori - extent, ori + extent, link.color);
			lines[idx++] = cVert(srcPos, link.color);
			lines[idx++] = cVert(ori, link.color);
		}

		entConnections = new VertexBuffer(colorShader, COLOR_4B | POS_3F, lines, numVerts);
		entConnectionPoints = new VertexBuffer(colorShader, COLOR_4B | POS_3F, points, numPoints * 6 * 6);
		entConnections->ownData = true;
		entConnectionPoints->ownData = true;
	}
}

void Renderer::updateEntConnectionPositions() {
	// todo: these shouldn't be here
	updateCullBox();
	updateEntDirectionVectors();
	updateTextureAxes();

	if (!entConnections) {
		return;
	}
	
	Bsp* map = pickInfo.getMap();

	cVert* lines = (cVert*)entConnections->data;
	cCube* points = (cCube*)entConnectionPoints->data;

	for (int k = 0; k < entConnectionLinks.size(); k++) {
		EntConnection& link = entConnectionLinks[k];
		vec3 srcPos = getEntOrigin(map, link.self).flip();
		vec3 dstPos = getEntOrigin(map, link.target).flip();

		int offset = k * 2;
		lines[k * 2].x = srcPos.x;
		lines[k * 2].y = srcPos.y;
		lines[k * 2].z = srcPos.z;
		lines[(k * 2)+1].x = dstPos.x;
		lines[(k * 2)+1].y = dstPos.y;
		lines[(k * 2)+1].z = dstPos.z;

		float s = 1.5f;
		vec3 extent = vec3(s, s, s);
		points[k] = cCube(dstPos - extent, dstPos + extent, link.color);
	}
}

void Renderer::updateCullBox() {
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

	invalidSolid = !pickInfo.getMap()->vertex_manipulation_sync(pickInfo.getModelIndex(), modelVerts, true, false);
	mapRenderer->refreshModel(pickInfo.getModelIndex());
}

void Renderer::splitFace() {
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

	invalidSolid = !pickInfo.getMap()->vertex_manipulation_sync(pickInfo.getModelIndex(), modelVerts, true, false);
	mapRenderer->refreshModel(pickInfo.getModelIndex());
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

void Renderer::grabEnts() {
	if (pickInfo.getEntIndex() <= 0)
		return;
	movingEnt = true;
	Bsp* map = mapRenderer->map;
	vec3 mapOffset = mapRenderer->mapOffset;
	vec3 localCamOrigin = cameraOrigin - mapOffset;
	grabDist = (getEntOrigin(map, map->ents[pickInfo.getEntIndex()]) - localCamOrigin).length();

	vec3 centroid;
	grabStartEntOrigin.clear();
	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* ent = map->ents[pickInfo.ents[i]];
		vec3 ori = getEntOrigin(map, ent);
		centroid += ori;
		grabStartEntOrigin.push_back(ent->getOrigin());
	}
	centroid /= (float)pickInfo.ents.size();

	grabStartOrigin = centroid;
}

void Renderer::cutEnts() {
	if (pickInfo.getEntIndex() <= 0)
		return;

	if (copiedEnts.size()) {
		for (Entity* ent : copiedEnts) {
			delete ent;
		}
		copiedEnts.clear();
	}

	Bsp* map = mapRenderer->map;

	string serialized = "";

	vector<int> indexes;

	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* copy = new Entity();
		*copy = *map->ents[pickInfo.ents[i]];
		copiedEnts.push_back(copy);
		serialized += copy->serialize();
		indexes.push_back(pickInfo.ents[i]);
	}
	
	DeleteEntitiesCommand* deleteCommand = new DeleteEntitiesCommand("Cut Entity", indexes);
	deleteCommand->execute();
	pushUndoCommand(deleteCommand);

	ImGui::SetClipboardText(serialized.c_str());
}

void Renderer::copyEnts() {
	if (pickInfo.getEntIndex() <= 0)
		return;

	if (copiedEnts.size()) {
		for (Entity* ent : copiedEnts) {
			delete ent;
		}
		copiedEnts.clear();
	}

	Bsp* map = mapRenderer->map;

	string serialized = "";

	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* copy = new Entity();
		*copy = *map->ents[pickInfo.ents[i]];
		copiedEnts.push_back(copy);
		serialized += copy->serialize();
	}

	ImGui::SetClipboardText(serialized.c_str());
}

bool Renderer::canPasteEnts() {
	if (!copiedEnts.empty())
		return true;

	const char* clipBoardText = ImGui::GetClipboardText();
	if (!clipBoardText) {
		return false;
	}

	CreateEntityFromTextCommand createCommand("", clipBoardText);
	return !createCommand.parse().empty();
}

void Renderer::pasteEnts(bool noModifyOrigin) {
	if (copiedEnts.empty()) {
		const char* clipBoardText = ImGui::GetClipboardText();
		if (clipBoardText)
			pasteEntsFromText(clipBoardText, noModifyOrigin);
		else
			logf("No entity data in clipboard\n");
		return;
	}

	Bsp* map = pickInfo.getMap();

	vector<Entity*> pasteEnts;

	// get the centroid so groups of entities are pasted at the same relative offsets to each other
	vec3 centroid;
	for (Entity* copy : copiedEnts) {
		centroid += getEntOrigin(map, copy);
	}
	centroid /= (float)copiedEnts.size();

	for (Entity* copy : copiedEnts) {
		Entity* insertEnt = new Entity();
		*insertEnt = *copy;

		if (!noModifyOrigin) {
			// can't just set camera origin directly because solid ents can have (0,0,0) origins
			vec3 oldOrigin = getEntOrigin(map, insertEnt);
			vec3 centroidOffset = oldOrigin - centroid;
			vec3 modelOffset = getEntOffset(map, insertEnt);
			vec3 mapOffset = mapRenderer->mapOffset;

			vec3 moveDist = (cameraOrigin + cameraForward * 100) - oldOrigin;
			vec3 newOri = (oldOrigin + moveDist + centroidOffset) - (modelOffset + mapOffset);
			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
			insertEnt->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
		}

		pasteEnts.push_back(insertEnt);
	}

	CreateEntitiesCommand* createCommand = new CreateEntitiesCommand("Paste Entity", pasteEnts);
	for (int i = 0; i < pasteEnts.size(); i++) {
		delete pasteEnts[i];
	}
	pasteEnts.clear();

	createCommand->execute();
	pushUndoCommand(createCommand);

	if (pickInfo.getEnt() && pickInfo.getEnt()->isBspModel())
		saveLumpState(pickInfo.getMap(), 0xffffffff, true);

	pickInfo.deselect();
	for (int i = 0; i < copiedEnts.size(); i++) {
		pickInfo.selectEnt(map->ents.size() - (1 + i));
	}

	updateSelectionSize();
	updateEntConnections();
	updateEntityUndoState();
	pickCount++; // force transform window update
}

void Renderer::pasteEntsFromText(string text, bool noModifyOrigin) {
	Bsp* map = pickInfo.getMap() ? pickInfo.getMap() : mapRenderer->map;

	CreateEntityFromTextCommand* createCommand = 
		new CreateEntityFromTextCommand("Paste entities from clipboard", text);
	createCommand->execute();

	if (createCommand->createdEnts == 0) {
		logf("No entity data in clipboard\n");
		return;
	}

	pushUndoCommand(createCommand);

	vec3 centroid;
	for (int i = 0; i < createCommand->createdEnts; i++) {
		Entity* ent = map->ents[map->ents.size() - (1 + i)];
		centroid += getEntOrigin(map, ent);
	}
	centroid /= (float)createCommand->createdEnts;

	pickInfo.deselect();

	for (int i = 0; i < createCommand->createdEnts; i++) {
		if (!noModifyOrigin) {
			Entity* ent = map->ents[map->ents.size() - (1 + i)];
			vec3 oldOrigin = getEntOrigin(map, ent);
			vec3 centroidOffset = oldOrigin - centroid;
			vec3 modelOffset = getEntOffset(map, ent);
			vec3 mapOffset = mapRenderer->mapOffset;

			vec3 moveDist = (cameraOrigin + cameraForward * 100) - oldOrigin;
			vec3 newOri = (oldOrigin + moveDist + centroidOffset) - (modelOffset + mapOffset);
			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
			ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
		}
		pickInfo.selectEnt(map->ents.size() - (1 + i));
	}

	if (createCommand->createdEnts)
		createCommand->refresh();
	postSelectEnt();
}

void Renderer::deleteEnts() {
	if (pickInfo.getEntIndex() <= 0)
		return;

	DeleteEntitiesCommand* deleteCommand = new DeleteEntitiesCommand("Delete Entity", pickInfo.ents);
	deleteCommand->execute();
	pushUndoCommand(deleteCommand);
}

void Renderer::deselectObject() {
	if (pickInfo.getEnt() && pickInfo.getEnt()->isBspModel())
		saveLumpState(pickInfo.getMap(), 0xffffffff, true);

	// update deselected point ents
	for (int entIdx : pickInfo.ents) {
		Entity* ent = pickInfo.getMap()->ents[entIdx];
		if (!ent->isBspModel()) {
			mapRenderer->refreshPointEnt(entIdx);
		}
	}

	pickInfo.deselect();
	isTransformableSolid = true;
	modelUsesSharedStructures = false;
	hoverVert = -1;
	hoverEdge = -1;
	hoverAxis = -1;
	updateEntConnections();
}

void Renderer::deselectFaces() {
	for (int i = 0; i < pickInfo.faces.size(); i++) {
		mapRenderer->highlightFace(pickInfo.faces[i], false);
	}
	pickInfo.deselect();
}

void Renderer::postSelectEnt() {
	updateSelectionSize();
	updateEntConnections();
	updateEntityUndoState();
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

void Renderer::goToFace(Bsp* map, int faceIdx) {

	int modelIdx = 0;
	for (int i = 0; i < map->modelCount; i++) {
		BSPMODEL& model = map->models[i];
		if (model.iFirstFace <= faceIdx && model.iFirstFace + model.nFaces > faceIdx) {
			modelIdx = i;
			break;
		}
	}

	vec3 offset;
	for (int i = 0; i < map->ents.size(); i++) {
		if (map->ents[i]->getBspModelIdx() == modelIdx) {
			offset = map->ents[i]->getOrigin();
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


void Renderer::ungrabEnts() {
	if (!movingEnt) {
		return;
	}

	movingEnt = false;

	pushEntityUndoState("Move Entity");
	pickCount++; // force transform window to recalc offsets
}

void Renderer::updateEntityUndoState() {
	//logf("Update entity undo state\n");
	for (int i = 0; i < undoEntityState.size(); i++)
		delete undoEntityState[i].ent;
	undoEntityState.clear();

	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* ent = pickInfo.getMap()->ents[pickInfo.ents[i]];

		EntityState state;
		state.ent = new Entity();
		*state.ent = *ent;
		state.index = pickInfo.ents[i];
		undoEntityState.push_back(state);
	}

	if (pickInfo.getEnt())
		undoEntOrigin = pickInfo.getEnt()->getOrigin();
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

void Renderer::updateEntityLumpUndoState(Bsp* map) {
	if (undoLumpState.lumps[LUMP_ENTITIES])
		delete[] undoLumpState.lumps[LUMP_ENTITIES];

	LumpState dupLump = map->duplicate_lumps(LUMP_ENTITIES);
	undoLumpState.lumps[LUMP_ENTITIES] = dupLump.lumps[LUMP_ENTITIES];
	undoLumpState.lumpLen[LUMP_ENTITIES] = dupLump.lumpLen[LUMP_ENTITIES];
}

bool Renderer::canPushEntityUndoState() {
	if (!undoEntityState.size()) {
		return false;
	}
	if (undoEntityState.size() != pickInfo.ents.size()) {
		return true;
	}

	Bsp* map = pickInfo.getMap();
	for (int i = 0; i < pickInfo.ents.size(); i++) {
		int currentIdx = undoEntityState[i].index;
		if (currentIdx >= map->ents.size() || currentIdx != pickInfo.ents[i]) {
			return true;
		}

		Entity* currentEnt = map->ents[currentIdx];
		Entity* undoEnt = undoEntityState[i].ent;
			
		if (undoEnt->keyOrder.size() == currentEnt->keyOrder.size()) {
			for (int i = 0; i < undoEnt->keyOrder.size(); i++) {
				string oldKey = undoEnt->keyOrder[i];
				string newKey = currentEnt->keyOrder[i];
				if (oldKey != newKey) {
					return true;
				}
				string oldVal = undoEnt->getKeyvalue(oldKey);
				string newVal = currentEnt->getKeyvalue(oldKey);
				if (oldVal != newVal) {
					return true;
				}
			}
		}
		else {
			return true;
		}
	}

	return false;
}

void Renderer::pushEntityUndoState(string actionDesc) {
	if (!canPushEntityUndoState()) {
		//logf("nothint to undo\n");
		return; // nothing to undo
	}

	if (g_app->pickInfo.ents.size() != undoEntityState.size()) {
		logf("Pushed undo state with bad size\n");
		return;
	}

	//logf("Push undo state: %s\n", actionDesc.c_str());
	pushUndoCommand(new EditEntitiesCommand(actionDesc, undoEntityState));
	updateEntityUndoState();
}

void Renderer::pushModelUndoState(string actionDesc, int targetLumps) {
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
		undoHistory[i] = NULL;
	}

	undoHistory.clear();
	calcUndoMemoryUsage();
}

void Renderer::clearRedoCommands() {
	for (int i = 0; i < redoHistory.size(); i++) {
		delete redoHistory[i];
		redoHistory[i] = NULL;
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

void Renderer::merge(string fpath) {
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

	BspMerger merger;
	mergeResult = merger.merge(maps, vec3(), thismap->name, true, true, true, g_engine_limits->max_mapboundary);

	if (!mergeResult.map || !mergeResult.map->valid) {
		delete map2;
		if (mergeResult.map)
			delete mergeResult.map;

		mergeResult.map = NULL;
		return;
	}

	if (mergeResult.overflow) {
		return; // map deleted later in gui modal, after displaying limit overflows
	}

	delete mapRenderer;
	mapRenderer = NULL;
	addMap(mergeResult.map);

	if (copiedEnts.size()) {
		for (Entity* ent : copiedEnts) {
			delete ent;
		}
		copiedEnts.clear();
	}

	clearUndoCommands();
	clearRedoCommands();
	gui->refresh();
	updateCullBox();

	logf("Merged maps!\n");
}

void Renderer::getWindowSize(int& width, int& height) {
	glfwGetWindowSize(window, &width, &height);
}

void Renderer::handleResize(int width, int height) {
	gui->windowResized(width, height);
}

bool Renderer::entityHasFgd(string cname) {
	return mergedFgd ? mergedFgd->getFgdClass(cname) != NULL : false;
}