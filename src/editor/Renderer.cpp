#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
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
#include "tinyfiledialogs.h"
#include <lodepng.h>
#include "embedded_shaders.h"

#include "icons/app.h"
#include "icons/app2.h"

// everything except VIS, ENTITIES, MARKSURFS
#define EDIT_MODEL_LUMPS (PLANES | TEXTURES | VERTICES | NODES | TEXINFO | FACES | LIGHTING | CLIPNODES | LEAVES | EDGES | SURFEDGES | MODELS)

future<void> Renderer::fgdFuture;

int glGetErrorDebug() {
	return glGetError();
}

void glCheckError(const char* checkMessage) {
	// error checking is very expensive
#ifdef DEBUG_MODE
	static int lastError = 0;
	int glerror = glGetError();
	if (glerror != GL_NO_ERROR) {
		if (lastError != glerror)
			logf("Got OpenGL Error %d after %s\n", glerror, checkMessage);
		else
			debugf("Got OpenGL Error %d after %s\n", glerror, checkMessage);
		lastError = glerror;
	}
#endif
}

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
	if (!g_app->confirmMapExit()) {
		return;
	}

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
	g_settings.renderer = clamp(g_settings.renderer, 0, RENDERER_COUNT - 1);
	memset(lightStylesEnabled, true, sizeof(bool) * MAXLIGHTMAPS);

	if (!createWindow()) {
		logf("Window creation failed. Does your graphics driver support OpenGL 2.1?\n");
		return;
	}

	glCheckError("window creation");

	GLint texImageUnits;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_max_texture_size);
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texImageUnits);
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &g_max_texture_array_layers);
	glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &g_max_vtf_units);
	const char* openglExts = (const char*)glGetString(GL_EXTENSIONS);

	logf("OpenGL Version: %s\n", (char*)glGetString(GL_VERSION));
	logf("    Max Texture size: %dx%d\n", g_max_texture_size, g_max_texture_size);
	logf("    Texture Units: %d / 5\n", texImageUnits);
	logf("    Texture Array Layers: %d\n", g_max_texture_array_layers);
	logf("    Vertex Texture Fetch Units: %d\n", g_max_vtf_units);
	debugf("OpenGL Extensions:\n%s\n", openglExts);

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

	compileShaders();

	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

	g_app = this;

	g_progress.simpleMode = true;

	pointEntRenderer = new PointEntRenderer(NULL, vector<Fgd*>(), colorShader);

	loadSettings();

	reloading = true;
	fgdFuture = async(launch::async, &Renderer::loadFgds, this);

	memset(&undoLumpState, 0, sizeof(LumpState));
	memset(&initialLumpState, 0, sizeof(LumpState));

	glCheckError("Initializing context");

	//cameraOrigin = vec3(0, 0, 0);
	//cameraAngles = vec3(0, 0, 0);
}

bool Renderer::createWindow() {
	if (!glfwInit())
	{
		logf("GLFW initialization failed\n");
		return false;
	}

	glfwSetErrorCallback(error_callback);

	//glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

	window = glfwCreateWindow(g_settings.windowWidth, g_settings.windowHeight, "bspguy", NULL, NULL);

	if (!window) {
		return false;
	}

	byte* icon_data = NULL;
	uint w, h;
	lodepng_decode32(&icon_data, &w, &h, app_icon, sizeof(app_icon));

	byte* icon_data2 = NULL;
	uint w2, h2;
	lodepng_decode32(&icon_data2, &w2, &h2, app_icon2, sizeof(app_icon2));

	GLFWimage images[2];
	images[0].pixels = icon_data;
	images[0].width = w;
	images[0].height = h;
	images[1].pixels = icon_data2;
	images[1].width = w2;
	images[1].height = h2;
	glfwSetWindowIcon(window, 2, images);

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

	return true;
}

void Renderer::compileShaders() {
	const char* openglExts = (const char*)glGetString(GL_EXTENSIONS);

	const char* bspFragShader = bsp_legacy_frag_glsl;
	const char* mdl_vert = mdl_vert_glsl;

	g_opengl_texture_array_support = false;
	g_opengl_3d_texture_support = false;

	if (g_settings.renderer == RENDERER_OPENGL_21_LEGACY) {
		logf("Legacy renderer selected. Not checking extension support.\n");
		mdl_vert = mdl_legacy_vert_glsl;
	}
	else if (strstr(openglExts, "GL_EXT_texture_array")) {
		g_opengl_texture_array_support = true;
		bspFragShader = bsp_arraytex_frag_glsl;
	}
	else if (strstr(openglExts, "GL_EXT_texture3D")) {
		logf("Texture arrays not supported. 3D textures without filtering will be used instead\n");
		g_opengl_3d_texture_support = true;
		bspFragShader = bsp_3dtex_frag_glsl;
	}
	else {
		logf("Neither texture arrays nor 3D textures are supported. Map rendering will be slow.\n");
	}

	if (!bspShader) {
		bspShader = new ShaderProgram("BSP");
		colorShader = new ShaderProgram("Color");
		mdlShader = new ShaderProgram("MDL");
		sprShader = new ShaderProgram("SPR");
		vec3Shader = new ShaderProgram("vec3");
		sprOutlineShader = new ShaderProgram("SPR outline");
	}
	else {
		logf("Recompiling shaders\n");
	}

	bspShader->compile(bsp_vert_glsl, bspFragShader);
	bspShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	bspShader->setMatrixNames(NULL, "modelViewProjection");
	bspShader->addUniform("sTex", UNIFORM_INT);
	bspShader->addUniform("colorMult", UNIFORM_VEC4);
	bspShader->addUniform("alphaTest", UNIFORM_FLOAT);
	bspShader->addUniform("gamma", UNIFORM_FLOAT);
	bspShader->setUniform("sTex", 0);
	for (int s = 0; s < MAXLIGHTMAPS; s++) {
		string name = "sLightmapTex" + to_string(s);
		bspShader->addUniform(name, UNIFORM_INT);
		bspShader->setUniform(name, s + 1);
	}
	
	colorShader->compile(cvert_vert_glsl, cvert_frag_glsl);
	colorShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	colorShader->setMatrixNames(NULL, "modelViewProjection");
	colorShader->setVertexAttributeNames("vPosition", "vColor", NULL, NULL);
	colorShader->addUniform("colorMult", UNIFORM_VEC4);
	colorShader->setUniform("colorMult", vec4(1, 1, 1, 1));
	
	mdlShader->compile(mdl_vert, mdl_frag_glsl);
	mdlShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	mdlShader->setMatrixNames(NULL, "modelViewProjection");
	mdlShader->setVertexAttributeNames("vPosition", NULL, "vTex", "vNormal");
	mdlShader->addUniform("sTex", UNIFORM_INT);
	mdlShader->addUniform("elights", UNIFORM_INT);
	mdlShader->addUniform("ambient", UNIFORM_VEC3);
	mdlShader->addUniform("lights", UNIFORM_MAT3);
	mdlShader->addUniform("additiveEnable", UNIFORM_INT);
	mdlShader->addUniform("chromeEnable", UNIFORM_INT);
	mdlShader->addUniform("flatshadeEnable", UNIFORM_INT);
	mdlShader->addUniform("colorMult", UNIFORM_VEC4);
	mdlShader->addUniform("viewerOrigin", UNIFORM_VEC3);
	mdlShader->addUniform("viewerRight", UNIFORM_VEC3);
	if (g_settings.renderer != RENDERER_OPENGL_21_LEGACY) {
		mdlShader->addUniform("boneMatrixTexture", UNIFORM_INT);
	}

	sprShader->compile(spr_vert_glsl, spr_frag_glsl);
	sprShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	sprShader->setMatrixNames(NULL, "modelViewProjection");
	sprShader->setVertexAttributeNames("vPosition", NULL, "vTex", NULL);
	sprShader->addUniform("color", UNIFORM_VEC4);

	vec3Shader->compile(vec3_vert_glsl, vec3_frag_glsl);
	vec3Shader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	vec3Shader->setMatrixNames(NULL, "modelViewProjection");
	vec3Shader->setVertexAttributeNames("vPosition", NULL, NULL, NULL);
	vec3Shader->addUniform("color", UNIFORM_VEC4);
	vec3Shader->setUniform("color", vec4(1, 1, 1, 1));

	sprOutlineShader->compile(vec3_vert_glsl, vec3_depth_frag_glsl);
	sprOutlineShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	sprOutlineShader->setMatrixNames(NULL, "modelViewProjection");
	sprOutlineShader->setVertexAttributeNames("vPosition", NULL, NULL, NULL);
	sprOutlineShader->addUniform("color", UNIFORM_VEC4);

	glCheckError("compiling shaders");
}

Renderer::~Renderer() {
	glfwTerminate();
}

void Renderer::renderLoop() {
	glCheckError("entering renderloop");

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glCheckError("renderloop state enable");

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

	glCheckError("creating transform axes");

	updateDragAxes();

	glCheckError("updating transform axes");

	float s = 1.0f;
	cCube vertCube(vec3(-s, -s, -s), vec3(s, s, s), { 0, 128, 255, 255 });
	VertexBuffer vertCubeBuffer(colorShader, COLOR_4B | POS_3F, &vertCube, 6 * 6);

	glCheckError("pre render loop");

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

		glCheckError("Setting up view");

		vector<OrderedEnt> orderedEnts;
		mapRenderer->getRenderEnts(orderedEnts);

		// draw opaque world/entity faces
		mapRenderer->render(orderedEnts, transformTarget == TRANSFORM_VERTEX, clipnodeRenderHull, false, false);

		glCheckError("Rendering BSP (opaque pass)");

		// wireframe pass
		if (g_settings.render_flags & RENDER_WIREFRAME) {
			mapRenderer->render(orderedEnts, transformTarget == TRANSFORM_VERTEX, clipnodeRenderHull, false, true);
			glCheckError("Rendering BSP (wireframe pass)");
		}
		
		// studio models have transparent boxes that need to draw over the world but behind transparent
		// brushes like a trigger_once which is rendered using the clipnode model
		if (drawModelsAndSprites()) {
			isLoading = true;
		}
		glCheckError("Rendering models and sprites");
		
		if (mapArrangeMode)
			renderArrangeMaps();

		// draw transparent entity faces
		mapRenderer->render(orderedEnts, transformTarget == TRANSFORM_VERTEX, clipnodeRenderHull, true, false);
		glCheckError("Rendering BSP (transparency pass)");

		if (pickMode == PICK_LEAF) {
			mapRenderer->renderLeaves();
		}

		if (g_settings.show_wpoly || (g_settings.render_flags & RENDER_PVS)) {
			mapRenderer->updatePvs(cameraOrigin);
			
			if ((g_settings.render_flags & RENDER_PVS))
				mapRenderer->drawPvs();
		}

		glCheckError("Rendering leaf selection");

		if (!mapRenderer->isFinishedLoading()) {
			isLoading = true;
		}

		model.loadIdentity();

		colorShader->bind();
		drawEntDirectionVectors(); // draws over world faces
		glCheckError("Rendering entity vectors");

		drawTextureAxes();
		glCheckError("Rendering texture axes");

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

			/*
			if (gui->showDebugWidget && pickInfo.getFace()) {
				BSPFACE& face = *pickInfo.getFace();
				Bsp* map = mapRenderer->map;
				glDisable(GL_CULL_FACE);

				for (int i = 0; i < face.nEdges; i++) {
					int32_t edgeIdx = map->surfedges[face.iFirstEdge + i];
					BSPEDGE& edge = map->edges[abs(edgeIdx)];
					int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
					drawBox(map->verts[vertIdx], 8, COLOR4(0, 128, 0, 255));
					drawLine(map->verts[edge.iVertex[0]], map->verts[edge.iVertex[1]], COLOR4(128, 0, 255, 255));
				
					vec3 start = map->verts[edge.iVertex[0]];
					vec3 end = map->verts[edge.iVertex[1]];
					drawArrow(start, end, COLOR4(0, 255, 0, 255));
				}
				glEnable(GL_CULL_FACE);
			}
			*/

			glCheckError("Rendering debug clipnodes");

			if ((g_settings.render_flags & (RENDER_ORIGIN | RENDER_MAP_BOUNDARY)) || hasCullbox) {
				colorShader->bind();
				model.loadIdentity();
				colorShader->pushMatrix(MAT_MODEL);
				colorShader->updateMatrixes();
				glDisable(GL_CULL_FACE);
				
				if ((g_settings.render_flags & RENDER_MAP_BOUNDARY) && !emptyMapLoaded) {
					glDepthFunc(GL_LESS);

					COLOR4 red = COLOR4(255, 0, 0, 64);
					COLOR4 invisible = COLOR4(0, 0, 0, 0);
					COLOR4 green = COLOR4(0, 255, 0, 64);
					COLOR4 boxColor = gui->hoveredOOB == 0 ? red : green;
					vec3 center = vec3();
					float width = g_settings.mapsize_max;
					vec3 sz = vec3(width, width, width);
					vec3 pos = vec3(center.x, center.z, -center.y);
					cCube cube(pos - sz, pos + sz, gui->hoveredOOB == 0 ? red : green);

					if (gui->hoveredOOB >= 0) {
						red = COLOR4(255, 0, 0, 128);

						BSPPLANE plane;
						plane.fDist = g_settings.mapsize_max;
						switch (gui->hoveredOOB) {
						case 1: plane.vNormal = vec3(1, 0, 0); cube.right.setColor(invisible); break;
						case 2: plane.vNormal = vec3(-1, 0, 0); cube.left.setColor(invisible); break;
						case 3: plane.vNormal = vec3(0, 1, 0);  cube.front.setColor(invisible); break;
						case 4: plane.vNormal = vec3(0, -1, 0); cube.back.setColor(invisible); break;
						case 5: plane.vNormal = vec3(0, 0, 1); cube.bottom.setColor(invisible); break;
						case 6: plane.vNormal = vec3(0, 0, -1); cube.top.setColor(invisible); break;
						}

						drawPlane(plane, red, g_settings.mapsize_max*1.2f);
					}

					{
						VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &cube, 6 * 6);
						buffer.upload();
						buffer.draw(GL_TRIANGLES);
					}
					glDepthFunc(GL_LEQUAL);

					glDepthFunc(GL_LEQUAL); // draw lines in front (still causes some z fighting)
					drawBoxOutline(vec3(), g_settings.mapsize_max * 2, COLOR4(0, 0, 0, 255));
					
					glDepthFunc(GL_LESS);
				}

				if (pickInfo.getEnt()) {
					vec3 offset = mapRenderer->renderOffset;
					model.translate(offset.x, offset.y, offset.z);
				}
				colorShader->updateMatrixes();

				if (hasCullbox) {
					drawBox(cullMins, cullMaxs, COLOR4(255, 0, 0, 64));
				}

				if (g_settings.render_flags & RENDER_ORIGIN) {
					drawLine(debugPoint - vec3(32, 0, 0), debugPoint + vec3(32, 0, 0), { 128, 128, 255, 255 });
					drawLine(debugPoint - vec3(0, 32, 0), debugPoint + vec3(0, 32, 0), { 0, 255, 0, 255 });
					drawLine(debugPoint - vec3(0, 0, 32), debugPoint + vec3(0, 0, 32), { 0, 0, 255, 255 });
				}

				glEnable(GL_CULL_FACE);
				colorShader->popMatrix(MAT_MODEL);
			}

			glCheckError("Rendering cull box");
		}

		drawEntConnections();
		if (entConnectionPoints && (g_settings.render_flags & RENDER_ENT_CONNECTIONS)) {
			model.loadIdentity();
			colorShader->updateMatrixes();
			glDisable(GL_DEPTH_TEST);
			entConnectionPoints->draw(GL_TRIANGLES);
			glEnable(GL_DEPTH_TEST);
		}

		glCheckError("Rendering entity connections");

		bool isScalingObject = transformMode == TRANSFORM_SCALE && transformTarget == TRANSFORM_OBJECT;
		bool isMovingOrigin = transformMode == TRANSFORM_MOVE && transformTarget == TRANSFORM_ORIGIN && originSelected;
		bool isTransformingValid = ((isTransformableSolid && !modelUsesSharedStructures) || !isScalingObject) && transformTarget != TRANSFORM_ORIGIN;
		bool isTransformingWorld = pickInfo.getEntIndex() == 0 && transformTarget != TRANSFORM_OBJECT;
		if (showDragAxes && !movingEnt && !isTransformingWorld && pickInfo.getEntIndex() >= 0 && (isTransformingValid || isMovingOrigin)) {
			drawTransformAxes();
		}

		glCheckError("Rendering transform axes");

		if (modelIdx > 0 && pickMode == PICK_OBJECT) {
			if (transformTarget == TRANSFORM_VERTEX && isTransformableSolid) {
				drawModelVerts();
				glCheckError("Rendering model verts");
			}
			if (transformTarget == TRANSFORM_ORIGIN) {
				drawModelOrigin();
				glCheckError("Rendering model origin");
			}
		}

		if (g_app->debugPoly.isValid)
			drawPolygon3D(g_app->debugPoly, COLOR4(0, 255, 0, 150));
		if (g_app->debugPoly2.isValid)
			drawPolygon3D(g_app->debugPoly2, COLOR4(255, 0, 0, 150));
		if (g_app->debugPoly3.isValid)
			drawPolygon3D(g_app->debugPoly3, COLOR4(255, 255, 255, 150));
		if (g_app->debugLine0 != g_app->debugLine1) {
			drawLine(debugLine0, debugLine1, { 128, 0, 255, 255 });
			drawLine(debugLine2, debugLine3, { 0, 255, 0, 255 });
			drawLine(debugLine4, debugLine5, { 255, 128, 0, 255 });
		}

		glCheckError("Rendering debug polys");

		renderNavMesh();

		if (pickMode == PICK_LEAF && (g_settings.render_flags & RENDER_LEAF_GRAPH)) {
			renderLeafGraph(mapRenderer->leafNavMesh);
		}

		vec3 forward, right, up;
		makeVectors(cameraAngles, forward, right, up);
		//logf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

		if (!g_app->hideGui)
			gui->draw();

		if (!isLoading && openMapAfterLoad.size()) {
			openMap(openMapAfterLoad.c_str());
			glCheckError("Opening map");
		}

		controls();

		glfwSwapBuffers(window);

		glCheckError("Swap buffers and controls");

		if (reloading && fgdFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
			postLoadFgds();
			reloading = reloadingGameDir = false;
			glCheckError("FGD post load");
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

void Renderer::renderNavMesh() {
	const bool navmeshwipcode = false;
	if (!navmeshwipcode)
		return;

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
		//LeafNavMesh* navMesh = LeafNavMeshGenerator().generate(map, false, CONTENTS_EMPTY, 3);
		LeafNavMesh* navMesh = LeafNavMeshGenerator().generate(map, true, CONTENTS_NOT_SOLID, 0);
		debugLeafNavMesh = navMesh;
	}

	if (debugLeafNavMesh && !isLoading) {
		glLineWidth(1);

		// split leaves dynamically by solid entities
		//debugLeafNavMesh->refreshNodes(map);

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

					if (node.leafIdx == 208 && linkLeaf.leafIdx == 76) {
						for (int k = 0; k < linkArea.verts.size(); k++) {
							drawBox(linkArea.verts[k], 1, COLOR4(255, 255, 0, 255));
						}
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

		if (debugPoly.isValid && debugPoly2.isValid) {
			colorShader->bind();
			colorShader->pushMatrix(MAT_PROJECTION);
			colorShader->pushMatrix(MAT_VIEW);
			projection.ortho(0, windowWidth, windowHeight, 0, -1.0f, 1.0f);
			view.loadIdentity();
			colorShader->updateMatrixes();

			vec2 maxSz = vec2(500, 500);
			vec2 sz = debugPoly.localMaxs - debugPoly.localMins;
			float scale = min(maxSz.y / sz.y, maxSz.x / sz.x);
			vec2 offset = debugPoly.localMins * -scale;
			vec2 pos = offset + vec2(700, 100);

			vector<vec2> projectedVerts;
			for (vec3& v : debugPoly2.verts) {
				projectedVerts.push_back(debugPoly.project(v));
			}

			drawPolygon2D(debugPoly.localVerts, pos, scale, COLOR4(255, 0, 0, 255));
			drawPolygon2D(projectedVerts, pos, scale, COLOR4(255, 0, 0, 255));

			debugPoly.coplanerIntersectArea(debugPoly2);

			for (int i = 0; i < debugVerts2d.size(); i++) {
				vec2 v = debugVerts2d[i];
				vec2 vpos = pos + v*scale;
				gui->addText(Text2D(vpos, cstrf("Vert %d: %d %d", i, (int)v.x, (int)v.y)));
				drawBox2D(vpos, 8, COLOR4(255, 255, 0, 255));
			}

			// draw camera origin in the same coordinate space
			vec2 cam = debugPoly.project(cameraOrigin);
			drawBox2D(pos + cam * scale, 16, debugPoly.isInside(cam) ? COLOR4(0, 255, 0, 255) : COLOR4(255, 32, 0, 255));

			colorShader->popMatrix(MAT_PROJECTION);
			colorShader->popMatrix(MAT_VIEW);
		}
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

	glCheckError("Rendering nav mesh");
}

void Renderer::renderLeafGraph(LeafNavMesh* mesh) {
	if (isLoading || !mesh)
		return;

	int leafNavIdx = mesh->getNodeIdx(mapRenderer->map, cameraOrigin);

	if (leafNavIdx == NAV_INVALID_IDX)
		return;

	LeafNode& node = mesh->nodes[leafNavIdx];

	colorShader->bind();
	model.loadIdentity();
	colorShader->updateMatrixes();
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	drawBox(node.origin, 2, COLOR4(0, 255, 0, 255));

	{
		vec3 screenOri = worldToScreen(node.origin - vec3(0, 0, 1));
		if (screenOri.z > 0) {
			const char* label = cstrf("Leaf %d", (int)node.leafIdx);
			gui->addText(Text2D(screenOri.x, screenOri.y, label, TEXT2D_ALIGN_CENTER));
		}
	}

	for (int i = 0; i < node.links.size(); i++) {
		LeafLink& link = node.links[i];
		if (link.node == -1) {
			break;
		}
		LeafNode& linkLeaf = mesh->nodes[link.node];
		if (linkLeaf.childIdx != NAV_INVALID_IDX) {
			continue;
		}

		Polygon3D& linkArea = link.linkArea;

		drawLine(node.origin, link.pos, COLOR4(255, 128, 0, 255));
		drawLine(link.pos, linkLeaf.origin, COLOR4(255, 128, 0, 255));

		for (int i = 0; i < linkArea.verts.size(); i++) {
			vec3& v1 = linkArea.verts[i];
			vec3& v2 = linkArea.verts[(i + 1) % linkArea.verts.size()];

			drawLine(v1, v2, COLOR4(255, 255, 0, 255));
		}

		drawBox(link.pos, 1, COLOR4(255, 255, 0, 255));
		drawLine(link.pos, link.pos + linkArea.plane_z * 16, COLOR4(255, 255, 0, 255));
		drawBox(linkLeaf.origin, 2, COLOR4(0, 255, 0, 255));

		vec3 screenOri = worldToScreen(linkLeaf.origin - vec3(0,0,1));
		if (screenOri.z > 0) {
			const char* label = cstrf("Leaf %d", (int)linkLeaf.leafIdx);
			gui->addText(Text2D(screenOri.x, screenOri.y, label, TEXT2D_ALIGN_CENTER));
		}
	}
}

void Renderer::renderArrangeMaps() {
	struct RenderMap {
		BspRenderer* renderer;
		Entity* controlEnt;
		vector<OrderedEnt> orderedEnts;
		vec3 mins, maxs;
	};
	vector<RenderMap> renderMaps;

	int idx = 1;
	for (BspRenderer* arrangeBsp : arrangeBsps) {
		Entity* controlEnt = mapRenderer->map->ents[idx++];
		arrangeBsp->map->ents[0]->setOrAddKeyvalue("origin", controlEnt->getOrigin().toKeyvalueString());

		vector<OrderedEnt> orderedEnts;
		arrangeBsp->getRenderEnts(orderedEnts);

		RenderMap rmap;
		rmap.controlEnt = controlEnt;
		rmap.renderer = arrangeBsp;
		rmap.orderedEnts = orderedEnts;
		arrangeBsp->map->get_bounding_box(rmap.mins, rmap.maxs);

		renderMaps.push_back(rmap);
	}

	for (RenderMap& arrangeBsp : renderMaps) {
		// opaque pass
		arrangeBsp.renderer->render(arrangeBsp.orderedEnts, false, clipnodeRenderHull, false, false);
	}

	for (RenderMap& arrangeBsp : renderMaps) {
		// wireframe pass
		if (g_settings.render_flags & RENDER_WIREFRAME) {
			arrangeBsp.renderer->render(arrangeBsp.orderedEnts, false, clipnodeRenderHull, false, true);
			glCheckError("Rendering BSP (wireframe pass)");
		}
	}

	for (RenderMap& arrangeBsp : renderMaps) {
		// transparency pass
		arrangeBsp.renderer->render(arrangeBsp.orderedEnts, false, clipnodeRenderHull, true, false);
	}

	colorShader->bind();
	colorShader->modelMat->loadIdentity();
	colorShader->updateMatrixes();

	for (RenderMap& rmap : renderMaps) {
		Bsp* map = rmap.renderer->map;
		COLOR4 boxColor = COLOR4(0, 0, 255, 128);

		vector<Entity*> selected = pickInfo.getEnts();
		for (Entity* selectedEnt : selected) {
			if (selectedEnt == rmap.controlEnt) {
				boxColor.g = 128;
			}
		}

		bool collision = false;
		for (RenderMap& othermap : renderMaps) {
			if (rmap.renderer == othermap.renderer)
				continue;
			if (boxesIntersect(rmap.mins, rmap.maxs, othermap.mins, othermap.maxs)) {
				collision = true;
				break;
			}
		}

		if (collision) {
			boxColor.r = 255;
			boxColor.b = 0;
		}

		drawBox(rmap.mins, rmap.maxs, boxColor);
	}
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
	mapRenderer->pointEntRenderer->uploadCubeBuffers();

	for (int i = 0; i < mapRenderer->map->ents.size(); i++) {
		Entity* ent = mapRenderer->map->ents[i];
		ent->clearCache();
	}

	swapPointEntRenderer = NULL;

	gui->entityReportFilterNeeded = true;

	updateEntConnections();
	updateEntDirectionVectors();
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
	mapArrangeMode = false;
	pvsCopyLeaves.clear();
	hiddenFaces.clear();
	hiddenLeaves.clear();

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
			delete[] initialLumpState.lumps[i];
		}
	}
	memset(&undoLumpState, 0, sizeof(LumpState));
	memset(&initialLumpState, 0, sizeof(LumpState));

	forceAngleRotation = false; // can cause confusion opening a new map
}

void Renderer::reloadMaps() {
	if (!g_app->confirmMapExit()) {
		return;
	}

	string reloadPath = mapRenderer->map->path;

	clearMapData();
	addMap(new Bsp(reloadPath));

	updateEntConnections();

	logf("Reloaded maps\n");
}

void Renderer::updateWindowTitle() {
	string map = mapRenderer->map->path;
	string title = map.empty() ? "bspguy" : getAbsolutePath(map) + " - bspguy";
	glfwSetWindowTitle(window, title.c_str());
}

void Renderer::openMap(const char* fpath) {
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

void Renderer::openMap(Bsp* map) {
	for (BspRenderer* render : arrangeBsps) {
		if (render->map == map) {
			render->map = NULL; // don't delete the map about to be opened;
		}
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
	g_settings.render_flags = g_settings.render_flags;
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
	g_settings.render_flags = g_settings.render_flags;
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
	vec3 renderOffset = mapRenderer->renderOffset;
	vec3 localCameraOrigin = cameraOrigin - mapRenderer->mapOffset;

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

	modelVertBuff->upload();

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
	vec3 renderOffset = mapRenderer->renderOffset;
	Entity* ent = pickInfo.getEnt();

	COLOR4 vertDimColor = { 0, 200, 0, 255 };
	COLOR4 vertHoverColor = { 128, 255, 128, 255 };
	COLOR4 selectColor = { 0, 128, 255, 255 };
	COLOR4 hoverSelectColor = { 96, 200, 255, 255 };

	if (modelUsesSharedStructures) {
		vertDimColor = { 32, 32, 32, 255 };
	}

	vec3 ori = transformedOrigin + renderOffset;
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
	modelOriginBuff->upload();

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
		scaleAxes.buffer->upload();
		scaleAxes.buffer->draw(GL_TRIANGLES);
	}
	if (transformMode == TRANSFORM_MOVE) {
		vec3 ori = moveAxes.origin;

		bool shouldOffset = false;
		for (Entity* ent : pickInfo.getEnts()) {
			shouldOffset = ent->shouldDisplayDirectionVector();
			break;
		}

		float offset = shouldOffset ? 64 : 0;
		model.translate(ori.x, ori.z + offset, -ori.y);
		colorShader->updateMatrixes();
		moveAxes.buffer->upload();
		moveAxes.buffer->draw(GL_TRIANGLES);
	}
}

void Renderer::drawEntConnections() {
	if (entConnections && (g_settings.render_flags & RENDER_ENT_CONNECTIONS)) {
		model.loadIdentity();
		model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);
		colorShader->updateMatrixes();
		entConnections->draw(GL_LINES);
	}
}

void Renderer::updateEntDirectionVectors() {
	if (entDirectionVectors) {
		delete entDirectionVectors;
		entDirectionVectors = NULL;
	}
	
	if (!(g_settings.render_flags & RENDER_ENT_DIRECTIONS)) {
		return;
	}

	vector<Entity*> pickEnts = pickInfo.getEnts();

	if (pickEnts.empty()) {
		return;
	}

	vector<Entity*> directEnts;

	for (Entity* ent : pickEnts) {
		if (ent->shouldDisplayDirectionVector())
			directEnts.push_back(ent);
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
		vec3 angles = ent->getVisualAngles() * (PI / 180.0f);

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
	}

	entDirectionVectors = new VertexBuffer(colorShader, COLOR_4B | POS_3F, arrows, numPointers * arrowVerts);
	entDirectionVectors->ownData = true;
	entDirectionVectors->upload();
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
	model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);
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
	vector<cVert> verts;
	Bsp* map = mapRenderer->map;
	const float len = 16;

	int vidx = 0;
	for (int i = 0; i < pickInfo.faces.size(); i++) {
		int faceidx = pickInfo.faces[i];
		BSPFACE& face = map->faces[faceidx];
		BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
		vec3 center = map->get_face_center(faceidx);

		int model = map->get_model_from_face(faceidx);

		if (model != 0) {
			for (int k = 0; k < map->ents.size(); k++) {
				Entity* ent = map->ents[k];
				if (ent->getBspModelIdx() == model) {
					mat4x4 rotMat = ent->getRotationMatrix(true);
					mat4x4 rotMat2 = ent->getRotationMatrix(false);
					vec3 offset = ent->getOrigin();
					center = ((rotMat * vec4(center, 1)).xyz() + offset).flip();
					vec3 vS = ((rotMat * vec4(info.vS, 1)).xyz()).flip();
					vec3 vT = ((rotMat * vec4(info.vT, 1)).xyz()).flip();
					vec3 norm = crossProduct(vT, vS).normalize();

					verts.push_back(cVert(center, COLOR4(255, 255, 0, 255)));
					verts.push_back(cVert(center + vS.normalize(len), COLOR4(255, 255, 0, 255)));
					verts.push_back(cVert(center, COLOR4(0, 255, 0, 255)));
					verts.push_back(cVert(center + vT.normalize(len), COLOR4(0, 255, 0, 255)));
					verts.push_back(cVert(center, COLOR4(0, 64, 255, 255)));
					verts.push_back(cVert(center + norm.normalize(len), COLOR4(0, 64, 255, 255)));
				}
			}
		}
		else {
			center = center.flip();
			vec3 norm = crossProduct(info.vT, info.vS).normalize();

			// world face
			verts.push_back(cVert(center, COLOR4(255, 255, 0, 255)));
			verts.push_back(cVert(center + info.vS.flip().normalize(len), COLOR4(255, 255, 0, 255)));
			verts.push_back(cVert(center, COLOR4(0, 255, 0, 255)));
			verts.push_back(cVert(center + info.vT.flip().normalize(len), COLOR4(0, 255, 0, 255)));
			verts.push_back(cVert(center, COLOR4(0, 64, 255, 255)));
			verts.push_back(cVert(center + norm.flip().normalize(len), COLOR4(0, 64, 255, 255)));
		}
	}

	cVert* uploadVerts = new cVert[verts.size()];
	memcpy(uploadVerts, &verts[0], sizeof(cVert) * verts.size());

	allTextureAxes = new VertexBuffer(colorShader, COLOR_4B | POS_3F, uploadVerts, verts.size());
	allTextureAxes->upload();
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
	model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);
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

	if (!io.WantCaptureKeyboard && !io.WantCaptureMouse)
		cameraOrigin += getMoveDir() * frameTimeScale;
	
	moveGrabbedEnts();

	static bool oldWantTextInput = false;

	if (!io.WantTextInput && oldWantTextInput) {
		pushEntityUndoState("Edit Keyvalues");
	}

	oldWantTextInput = io.WantTextInput;

	if (!io.WantTextInput) {
		globalShortcutControls();
		shortcutControls();
	}

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

		bool shouldOffset = false;
		for (Entity* ent : pickInfo.getEnts()) {
			shouldOffset = ent->shouldDisplayDirectionVector();
			break;
		}

		vec3 offset = shouldOffset && transformMode == TRANSFORM_MOVE ? vec3(0, 0, 64) : vec3();
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
			gui->showTransformWidget = !gui->showTransformWidget;
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_O] && !oldPressed[GLFW_KEY_O]) {
			openMap((char*)NULL);
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
		mapRenderer->highlightPickedFaces(false);

		// update deselected point ents
		for (int entIdx : pickInfo.ents) {
			Entity* ent = pickInfo.getMap()->ents[entIdx];
			if (!ent->isBspModel()) {
				mapRenderer->refreshPointEnt(entIdx);
			}
		}
	}
	
	int oldEntIdx = pickInfo.getEntIndex();
	int clickedEnt, clickedFace, clickedLeaf;
	float bestDist = FLT_MAX;
	mapRenderer->pickPoly(pickStart, pickDir, clipnodeRenderHull, clickedEnt, clickedFace, clickedLeaf, bestDist);

	if (mapArrangeMode) {
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
			if (pickInfo.isFaceSelected(clickedFace)) {
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

			if (clickedFace != -1) {
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
			if (pickInfo.isLeafSelected(clickedLeaf)) {
				pickInfo.deselectLeaf(clickedLeaf);
			}
			else if (clickedLeaf != -1) {
				pickInfo.selectLeaf(clickedLeaf);
			}
		}
		else {
			pickInfo.deselect();

			if (clickedLeaf != -1) {
				pickInfo.selectLeaf(clickedLeaf);
			}
		}

		pickInfo.selectLeafFaces();
		mapRenderer->highlightPickedFaces(true);
		mapRenderer->highlightPickedLeaves(true);
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

	mapRenderer = new BspRenderer(map, pointEntRenderer);

	glCheckError("creating BSP renderer");

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
	setInitialLumpState();

	updateWindowTitle();

	emptyMapLoaded = false;

	glCheckError("add map");
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
	buffer.upload();
	buffer.draw(GL_LINES);
}

void Renderer::drawArrow(vec3 start, vec3 end, COLOR4 color) {
	struct cArrow {
		cCube shaft; // minor todo: one face can be omitted. make a new struct
		cPyramid tip;
	};
	int arrowVerts = 6 * 6 + (6 + 3 * 4);
	
	vec3 angles = VecToAngles((end - start).normalize());
	mat4x4 rotMat;
	rotMat.loadIdentity();
	rotMat.rotateZ(-angles.x * (PI / 180.0f));
	rotMat.rotateY(-angles.y * (PI / 180.0f));

	float len = (end - start).length();
	cArrow arrow;
	arrow.shaft = cCube(vec3(-1, -1, -1), vec3(len - 16, 1, 1), color);
	arrow.tip = cPyramid(vec3(len - 16, 0, 0), 4, 16, color);

	cVert* rawVerts = (cVert*)&arrow;
	for (int k = 0; k < arrowVerts; k++) {
		vec3* pos = (vec3*)&rawVerts[k].x;
		*pos = (rotMat * vec4(*pos, 1)).xyz() + start.flip();
	}

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &arrow, arrowVerts);
	buffer.upload();
	buffer.draw(GL_TRIANGLES);
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
	buffer.upload();
	buffer.draw(GL_LINES);
}

void Renderer::drawBox(vec3 center, float width, COLOR4 color) {
	width *= 0.5f;
	vec3 sz = vec3(width, width, width);
	vec3 pos = vec3(center.x, center.z, -center.y);
	cCube cube(pos - sz, pos + sz, color);

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &cube, 6 * 6);
	buffer.upload();
	buffer.draw(GL_TRIANGLES);
}

void Renderer::drawBoxOutline(vec3 center, float width, COLOR4 color) {
	width *= 0.5f;
	vec3 sz = vec3(width, width, width);
	vec3 pos = vec3(center.x, center.z, -center.y);
	vec3 mins = pos - sz;
	vec3 maxs = pos + sz;

	vec3 corners[8] = {
		vec3(mins.x, mins.y, mins.z), // 0
		vec3(maxs.x, mins.y, mins.z), // 1
		vec3(mins.x, maxs.y, mins.z), // 2
		vec3(maxs.x, maxs.y, mins.z), // 3
		vec3(mins.x, mins.y, maxs.z), // 4
		vec3(maxs.x, mins.y, maxs.z), // 5
		vec3(mins.x, maxs.y, maxs.z), // 6
		vec3(maxs.x, maxs.y, maxs.z),  // 7
	};

	cVert edges[24] = {
		cVert(corners[0], color), cVert(corners[1], color),
		cVert(corners[1], color), cVert(corners[3], color),
		cVert(corners[3], color), cVert(corners[2], color),
		cVert(corners[2], color), cVert(corners[0], color),
		cVert(corners[4], color), cVert(corners[5], color),
		cVert(corners[5], color), cVert(corners[7], color),
		cVert(corners[7], color), cVert(corners[6], color),
		cVert(corners[6], color), cVert(corners[4], color),
		cVert(corners[0], color), cVert(corners[4], color),
		cVert(corners[1], color), cVert(corners[5], color),
		cVert(corners[2], color), cVert(corners[6], color),
		cVert(corners[3], color), cVert(corners[7], color),
	};

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &edges, 24);
	buffer.upload();
	buffer.draw(GL_LINES);
}

void Renderer::drawBox(vec3 mins, vec3 maxs, COLOR4 color) {
	mins = vec3(mins.x, mins.z, -mins.y);
	maxs = vec3(maxs.x, maxs.z, -maxs.y);

	cCube cube(mins, maxs, color);

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &cube, 6 * 6);
	buffer.upload();
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
	buffer.upload();
	buffer.draw(GL_TRIANGLE_FAN);
}

void Renderer::drawPolygon2D(vector<vec2>& poly, vec2 pos, float scale, COLOR4 color) {
	for (int i = 0; i < poly.size(); i++) {
		vec2 v1 = poly[i];
		vec2 v2 = poly[(i + 1) % poly.size()];
		drawLine2D(pos + v1*scale, pos + v2 * scale, color);
		if (i == 0) {
			drawLine2D(pos + v1 * scale, pos + (v1 + (v2-v1)*0.5f) * scale, COLOR4(0,255,0,255));
		}
	}
}

void Renderer::drawBox2D(vec2 center, float width, COLOR4 color) {
	vec2 pos = vec2(center.x, center.y) - vec2(width*0.5f, width *0.5f);
	cQuad cube(pos.x, pos.y, width, width, color);

	VertexBuffer buffer(colorShader, COLOR_4B | POS_3F, &cube, 6);
	buffer.upload();
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
	buffer.upload();
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
	if (g_loading_models.getValue() > 0) {
		return NULL;
	}

	struct ModelKey {
		string name;
		bool isClassname;
	};

	static vector<ModelKey> tryModelKeys = {
		{"model", false},
		{"new_model", false},
		{"classname", true},
		{"monstertype", true},
	};

	string model;
	string lowerModel;
	bool foundModelKey = false;
	bool isMdlNotSpr = true;
	ent->isIconSprite = false;
	for (int i = 0; i < tryModelKeys.size(); i++) {
		ModelKey key = tryModelKeys[i];
		model = ent->getKeyvalue(key.name);

		if (tryModelKeys[i].isClassname) {
			if (g_app->mergedFgd) {
				FgdClass* fgd = g_app->mergedFgd->getFgdClass(ent->getKeyvalue(key.name));
				if (fgd) {
					if (fgd->model.length()) {
						model = fgd->model;
					}
					else if (fgd->sprite.length()) {
						model = fgd->sprite;
					}
					else if (fgd->iconSprite.length()) {
						model = fgd->iconSprite;
						ent->isIconSprite = true;
					}
					lowerModel = toLowerCase(model);
				}
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
			logf("Failed to find model for entity '%s' (%s): %s\n",
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
			newModel = new MdlRenderer(modelPath);
		}
		else {
			newModel = new SprRenderer(modelPath);
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

bool Renderer::drawModelsAndSprites() {
	if (!(g_settings.render_flags & RENDER_POINT_ENTS))
		return false;

	if (mapRenderer->map->ents.empty()) {
		return false;
	}

	vec3 worldOffset = mapRenderer->map->ents[0]->getOrigin();
	
	colorShader->bind();
	colorShader->setUniform("colorMult", vec4(1, 1, 1, 1));

	if (!(g_settings.render_flags & (RENDER_STUDIO_MDL | RENDER_SPRITES)))
		return false;

	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);

	int drawCount = 0;

	unordered_set<int> selectedEnts;
	for (int idx : pickInfo.ents) {
		selectedEnts.insert(idx);
	}

	vec3 renderOffset = mapRenderer->renderOffset;

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

	
	Frustum frustum = getCameraFrustum();

	bool modelsLoading = false;

	vector<DepthSortedEnt> depthSortedMdlEnts;
	for (int i = 0; i < mapRenderer->map->ents.size(); i++) {
		Entity* ent = mapRenderer->map->ents[i];
		DepthSortedEnt sent;
		sent.ent = ent;
		sent.mdl = loadModel(sent.ent);
		sent.ent->didStudioDraw = false;

		if (ent->hidden)
			continue;

		if (sent.mdl && (sent.mdl->loadState != MODEL_LOAD_DONE)) {
			modelsLoading = true;
		}

		if (sent.mdl && sent.mdl->loadState != MODEL_LOAD_INITIAL) {
			if (sent.mdl->loadState == MODEL_LOAD_WAITING) {
				if (g_loading_models.getValue() == 0) {
					g_loading_models.inc();
					sent.mdl->loadState = MODEL_LOAD_INITIAL;
					std::thread(&BaseRenderer::loadData, sent.mdl).detach();
				}
			}
			else if (!sent.mdl->valid) {
				logf("Failed to load model: %s\n", sent.mdl->fpath.c_str());
				studioModels[ent->cachedMdl->fpath] = NULL;
				delete sent.mdl;
				ent->cachedMdl = sent.mdl = NULL;
			}
			else if (sent.mdl->loadState == MODEL_LOAD_UPLOAD) {
				sent.mdl->upload();
				const char* typ = sent.mdl->isSprite() ? "SPR" : "MDL";
				if (sent.mdl->loadState != MODEL_LOAD_UPLOAD)
					debugf("Loaded %s: %s\n", typ, sent.mdl->fpath.c_str());
			}
		}

		if (sent.mdl && sent.mdl->loadState == MODEL_LOAD_DONE && sent.mdl->valid) {
			if (!ent->drawCached) {
				ent->drawOrigin = ent->getOrigin();
				ent->drawAngles = ent->getVisualAngles();
				EntRenderOpts opts = ent->getRenderOpts();

				if (sent.mdl->isStudioModel()) {
					vec3 mins, maxs;
					((MdlRenderer*)sent.mdl)->getModelBoundingBox(ent->drawAngles, opts.sequence, mins, maxs);
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

			if (sent.ent->lastDrawCall == 0) {
				// need to draw at least once to know mins/maxs
				depthSortedMdlEnts.push_back(sent);
				continue;
			}

			if (!sent.ent->drawCached) {
				EntRenderOpts opts = ent->getRenderOpts();

				if (sent.mdl->isStudioModel()) {
					vec3 mins, maxs;
					((MdlRenderer*)sent.mdl)->getModelBoundingBox(ent->drawAngles, opts.sequence, mins, maxs);
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

	glCheckError("Model/sprite rendering setup");

	for (int i = 0; i < depthSortedMdlEnts.size(); i++) {
		Entity* ent = depthSortedMdlEnts[i].ent;
		BaseRenderer* mdl = depthSortedMdlEnts[i].mdl;
		int entidx = depthSortedMdlEnts[i].idx;

		bool isSelected = selectedEnts.count(entidx);

		bool skipRender = mdl->isStudioModel() && !(g_settings.render_flags & RENDER_STUDIO_MDL)
			|| mdl->isSprite() && !(g_settings.render_flags & RENDER_SPRITES);

		if (skipRender)
			continue;

		EntCube* entcube = mapRenderer->renderEnts[depthSortedMdlEnts[i].idx].pointEntCube;
		if (!entcube->buffer->isUploaded())
			continue;

		{ // draw the colored transparent cube
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			//EntCube* entcube = mapRenderer->pointEntRenderer->getEntCube(ent);
			colorShader->bind();
			colorShader->pushMatrix(MAT_MODEL);
			*colorShader->modelMat = mapRenderer->renderEnts[entidx].modelMat;
			colorShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
			colorShader->updateMatrixes();

			if (isSelected) {
				//glDepthFunc(GL_ALWAYS); // ignore depth testing for the world but not for the model
				colorShader->setUniform("colorMult", vec4(1, 1, 1, 1));
				entcube->wireframeBuffer->draw(GL_LINES);
				//glDepthFunc(GL_LESS);

				glDepthMask(GL_FALSE); // let model draw over this
				colorShader->setUniform("colorMult", vec4(1, 1, 1, 0.5f));
				entcube->selectBuffer->draw(GL_TRIANGLES);
				glDepthMask(GL_TRUE);
			}
			else {
				glDepthMask(GL_FALSE);
				colorShader->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 0.5f));
				entcube->buffer->draw(GL_TRIANGLES);
				glDepthMask(GL_TRUE);
				
				colorShader->setUniform("colorMult", vec4(0.0f, 0.0f, 0.0f, 1.0f));
				entcube->wireframeBuffer->draw(GL_LINES);
			}

			colorShader->popMatrix(MAT_MODEL);

			glCheckError("Rendering model/sprite cube");
		}

		// draw the model
		ent->didStudioDraw = true;
		EntRenderOpts renderOpts = ent->getRenderOpts();
		vec3 drawOri = ent->drawOrigin + worldOffset;
		vec3 drawAngles = ent->drawAngles;

		if (mdl->isStudioModel()) {
			((MdlRenderer*)mdl)->draw(drawOri, drawAngles, ent, g_app->cameraOrigin, g_app->cameraRight, isSelected);
		}
		else if (mdl->isSprite()) {
			COLOR3 color = COLOR3(255, 255, 255);
			COLOR3 outlineColor = COLOR3(0, 0, 0);
			if (ent->isIconSprite) {
				vec3 sz = entcube->maxs - entcube->mins;
				float minDim = min(min(sz.x, sz.y), sz.z);
				renderOpts.scale = ((SprRenderer*)mdl)->getScaleToFitInsideCube(minDim);
				color = ent->getFgdTint();
				
				if (!ent->canRotate()) {
					drawAngles = vec3();
				}
				if (isSelected) {
					color = COLOR3(255, 0, 0);
				}
			}
			else if (isSelected) {
				color = COLOR3(255, 32, 32);
				outlineColor = COLOR3(255, 255, 0);
			}

			((SprRenderer*)mdl)->draw(drawOri, drawAngles, ent, renderOpts, color, outlineColor, ent->isIconSprite);
			glCheckError("Rendering SPR");
		}
		
		drawCount++;

		// debug the model verts bounding box
		if (false && mdl->isStudioModel()) {
			vec3 mins, maxs;
			((MdlRenderer*)mdl)->getModelBoundingBox(ent->drawAngles, renderOpts.sequence, mins, maxs);
			mins += ent->drawOrigin;
			maxs += ent->drawOrigin;

			colorShader->bind();
			colorShader->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 1.0f));
			drawBox(mins, maxs, COLOR4(255, 255, 0, 255));
			glCheckError("Rendering debug MDL");
		}
	}

	//logf("Draw %d models\n", drawCount);

	glCullFace(GL_BACK);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	colorShader->bind();
	colorShader->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 1.0f));

	glCheckError("Model/sprite rendering cleanup");

	return modelsLoading;
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
	modelOriginBuff->upload();

	updateSelectionSize();

	for (int idx : pickInfo.getModelIndexes()) {
		modelUsesSharedStructures |= map->does_model_use_shared_structures(idx);
		if (modelUsesSharedStructures)
			break;
	}

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
	modelVertBuff->upload();
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

	if (!(g_settings.render_flags & RENDER_ENT_CONNECTIONS)) {
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
			unordered_set<string> selfNames = self->getAllTargetnames();

			for (int k = 0; k < map->ents.size(); k++) {
				Entity* ent = map->ents[k];

				if (k == entindx)
					continue;

				
				unordered_set<string> tnames = ent->getAllTargetnames();
				bool isTarget = tnames.size() && self->hasTarget(tnames);
				bool isCaller = selfNames.size() && ent->hasTarget(selfNames);

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
		entConnections->upload();
		entConnectionPoints->upload();
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

	entConnections->upload();
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

void Renderer::hideSelectedLeaves() {
	for (int idx : pickInfo.leaves) {
		hiddenLeaves.insert(idx);
	}

	mapRenderer->highlightPickedFaces(false);
	pickInfo.deselect();
	updateTextureAxes();

	mapRenderer->hideLeaves(true);
}

void Renderer::unhideLeaves() {
	hiddenLeaves.clear();
	mapRenderer->hideLeaves(false);
	mapRenderer->highlightPickedFaces(false);
	mapRenderer->highlightPickedLeaves(false);
	pickInfo.deselect();
}

void Renderer::hideSelectedFaces() {
	for (int idx : pickInfo.faces) {
		hiddenFaces.insert(idx);
	}

	mapRenderer->highlightPickedFaces(false);
	mapRenderer->hideFaces(true);
	pickInfo.deselect();
	updateTextureAxes();

	mapRenderer->hideLeaves(true);
}

void Renderer::unhideFaces() {
	mapRenderer->hideFaces(false);
	hiddenFaces.clear();
	mapRenderer->highlightPickedFaces(false);
	pickInfo.deselect();
	updateTextureAxes();
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

void Renderer::unhideSelectedEnts() {
	vector<Entity*> ents = pickInfo.getEnts();

	if (ents.empty())
		return;

	for (Entity* ent : ents) {
		ent->hidden = false;
	}

	anyHiddenEnts = false;
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->hidden) {
			anyHiddenEnts = true;
			break;
		}
	}

	deselectObject();
	mapRenderer->preRenderEnts();
}

void Renderer::hideSelectedEnts() {
	vector<Entity*> ents = pickInfo.getEnts();
	
	if (ents.empty() || mapArrangeMode)
		return;

	for (Entity* ent : ents) {
		ent->hidden = true;
	}

	deselectObject();
	anyHiddenEnts = true;
	mapRenderer->preRenderEnts();
}

void Renderer::unhideEnts() {
	vector<Entity*> ents = pickInfo.getEnts();
	Bsp* map = mapRenderer->map;

	int numHidden = 0;

	for (int i = 0; i < map->ents.size(); i++) {
		if (map->ents[i]->hidden)
			numHidden++;
		map->ents[i]->hidden = false;
	}

	anyHiddenEnts = false;
	mapRenderer->preRenderEnts();
	logf("Unhid %d entities\n", numHidden);
}

void Renderer::cutEnts() {
	if (pickInfo.getEntIndex() <= 0 || mapArrangeMode)
		return;

	Bsp* map = mapRenderer->map;

	string serialized = "";

	vector<int> indexes;

	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* copy = new Entity();
		*copy = *map->ents[pickInfo.ents[i]];
		serialized += copy->serialize();
		indexes.push_back(pickInfo.ents[i]);
	}
	
	DeleteEntitiesCommand* deleteCommand = new DeleteEntitiesCommand("Cut Entity", indexes);
	deleteCommand->execute();
	pushUndoCommand(deleteCommand);

	ImGui::SetClipboardText(serialized.c_str());
}

void Renderer::copyEnts(bool stringifyBspModels) {
	if (pickInfo.getEntIndex() <= 0 || mapArrangeMode)
		return;

	Bsp* map = mapRenderer->map;

	string serialized = "";

	for (int i = 0; i < pickInfo.ents.size(); i++) {
		Entity* copy = new Entity();
		*copy = *map->ents[pickInfo.ents[i]];
		serialized += copy->serialize(stringifyBspModels);
	}

	ImGui::SetClipboardText(serialized.c_str());
}

bool Renderer::canPasteEnts() {
	const char* clipBoardText = ImGui::GetClipboardText();
	if (!clipBoardText) {
		return false;
	}

	CreateEntityFromTextCommand createCommand("", clipBoardText);
	return !createCommand.parse().empty();
}

void Renderer::pasteEnts(bool noModifyOrigin) {
	if (mapArrangeMode)
		return;

	const char* clipBoardText = ImGui::GetClipboardText();
	if (!clipBoardText) {
		logf("No entity data in clipboard\n");
		return;
	}

	Bsp* map = pickInfo.getMap() ? pickInfo.getMap() : mapRenderer->map;

	CreateEntityFromTextCommand* createCommand = 
		new CreateEntityFromTextCommand("Paste entities", clipBoardText);
	createCommand->execute();

	if (createCommand->createdEnts == 0) {
		logf("No entity data in clipboard\n");
		return;
	}

	logf("Pasted %d entities from clipboard\n", createCommand->createdEnts);

	pushUndoCommand(createCommand);

	bool shouldReload = false;

	vec3 centroid;
	for (int i = 0; i < createCommand->createdEnts; i++) {
		Entity* ent = map->ents[map->ents.size() - (1 + i)];
		shouldReload |= ent->deserialize();
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

	if (shouldReload) {
		mapRenderer->reload();
	}

	if (createCommand->createdEnts)
		createCommand->refresh();

	postSelectEnt();
}

void Renderer::pasteEntsFromText(string text, bool noModifyOrigin) {
	if (mapArrangeMode)
		return;
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
	if (pickInfo.getEntIndex() <= 0 || mapArrangeMode)
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
	mapRenderer->highlightPickedFaces(false);
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


void Renderer::ungrabEnts() {
	if (!movingEnt) {
		return;
	}

	movingEnt = false;

	int plural = pickInfo.ents.size() > 1;
	pushEntityUndoState(plural ? "Move Entities" : "Move Entity");
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
		debugf("Pushed undo state with bad size\n");
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
		return;
	}

	if (mergeResult.overflow) {
		delete command;
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

void Renderer::mergeMultiple(vector<string> fpaths, bool optimizeMerge, bool forceNohull2, int ripentmode) {
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

void Renderer::getWindowSize(int& width, int& height) {
	glfwGetWindowSize(window, &width, &height);
}

void Renderer::handleResize(int width, int height) {
	gui->windowResized(width, height);
}

bool Renderer::entityHasFgd(string cname) {
	return mergedFgd ? mergedFgd->getFgdClass(cname) != NULL : false;
}

bool Renderer::confirmMapExit() {
	if (emptyMapLoaded || mapArrangeMode)
		return true;

	if (g_settings.confirm_exit) {
		Bsp* map = mapRenderer->map;
		LumpState currentLumps = map->duplicate_lumps(0xffffffff);

		bool lumpsChanged = false;
		for (int i = 0; i < HEADER_LUMPS; i++) {
			if (currentLumps.lumpLen[i] != initialLumpState.lumpLen[i]) {
				lumpsChanged = true;
				break;
			}
			if (memcmp(initialLumpState.lumps[i], currentLumps.lumps[i], currentLumps.lumpLen[i])) {
				lumpsChanged = true;
				break;
			}
		}
		for (int i = 0; i < HEADER_LUMPS; i++) {
			delete[] currentLumps.lumps[i];
		}

		if (lumpsChanged) {
			string msg = "Save changes to " + g_app->mapRenderer->map->name + "?";
			int ret = tinyfd_messageBox(
				"Save", /* NULL or "" */
				msg.c_str(), /* NULL or "" may contain \n \t */
				"yesnocancel", /* "ok" "okcancel" "yesno" "yesnocancel" */
				"warning", /* "info" "warning" "error" "question" */
				0);

			if (ret == 0) { // cancel
				glfwSetWindowShouldClose(window, GLFW_FALSE);
				return false;
			}
			else if (ret == 1) { // yes
				Bsp* map = g_app->mapRenderer->map;
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

void Renderer::setInitialLumpState() {
	Bsp* map = mapRenderer->map;

	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (undoLumpState.lumps[i]) {
			delete[] initialLumpState.lumps[i];
		}
	}
	memset(&initialLumpState, 0, sizeof(LumpState));

	saveLumpState(map, 0xffffffff, true);
	initialLumpState = undoLumpState;
	saveLumpState(map, 0xffffffff, false);
}

vec3 Renderer::worldToScreen(const vec3& P) {
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

Frustum Renderer::getCameraFrustum() {
	float aspect = (float)windowWidth / (float)windowHeight;
	return getViewFrustum(cameraOrigin - mapRenderer->mapOffset, cameraAngles, aspect, zNear, zFar, fov);
}