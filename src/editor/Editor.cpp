#include "Editor.h"
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
#include "StudioMdlRenderer.h"
#include "SprRenderer.h"
#include <unordered_set>
#include "tinyfiledialogs.h"
#include <lodepng.h>
#include "embedded_shaders.h"
#include "Widget.h"
#include "MenuBar.h"
#include "render_utils.h"
#include "ModelRenderer.h"
#include "NavRenderer.h"

#include "icons/app.h"
#include "icons/app2.h"

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

void error_callback(int error, const char* description)
{
	errorf("GLFW Error: %s\n", description);
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

	GLint texImageUnits, vertexAttributes, varyingFloats;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_max_texture_size);
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texImageUnits);
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &g_max_texture_array_layers);
	glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &g_max_vtf_units);
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &vertexAttributes);
	glGetIntegerv(GL_MAX_VARYING_FLOATS, &varyingFloats);
	const char* openglExts = (const char*)glGetString(GL_EXTENSIONS);

	logf("\nOpenGL Version: %s\n", (char*)glGetString(GL_VERSION));
	debugf("    Max Texture size: %dx%d\n", g_max_texture_size, g_max_texture_size);
	debugf("    Max Vertex Attributes: %d / %d\n", vertexAttributes, MAX_VERTEX_ATTRIBUTES);
	debugf("    Max Varying Floats: %d / 32\n", varyingFloats);
	debugf("    Texture Units: %d / 3\n", texImageUnits);
	debugf("    Texture Array Layers: %d\n", g_max_texture_array_layers);
	debugf("    Vertex Texture Fetch Units: %d\n", g_max_vtf_units);
	debugf("OpenGL Extensions:\n%s\n\n", openglExts);
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

bool Editor::createWindow() {
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

void Editor::updateGpuSupportFlags() {
	const char* openglExts = (const char*)glGetString(GL_EXTENSIONS);

	g_opengl_texture_array_support = false;

	if (g_settings.texture_atlas) {
		g_opengl_texture_array_support = false; // prefer to use simple texture mode
	}
	else if (g_settings.renderer == RENDERER_OPENGL_21_LEGACY) {
		logf("Legacy renderer selected. Not checking extension support.\n");
	}
	else if (strstr(openglExts, "GL_EXT_texture_array")) {
		g_opengl_texture_array_support = true;
	}
	else {
		logf("Neither texture arrays nor 3D textures are supported. Map rendering will be slow.\n");
	}
}

void Editor::compileShaderPrograms() {
	float startTime = glfwGetTime();

	g_renderStats.numShaders = 0;
	g_renderStats.numShadersFailed = 0;
	g_shaders.bsp->clearAttributes();
	g_shaders.clipnode->clearAttributes();
	g_shaders.color->clearAttributes();
	g_shaders.texture->clearAttributes();
	g_shaders.mdl->clearAttributes();
	g_shaders.spr->clearAttributes();
	g_shaders.vec3->clearAttributes();

	{
		ShaderProgram* sh = g_shaders.bsp;
		sh->addCompileFlag(SH_BSP_WIREFRAME, "WIREFRAME");
		sh->addCompileFlag(SH_BSP_TEX_ATLAS, "TEXTURE_ATLAS");
		sh->addCompileFlag(SH_BSP_TEX_ARRAY, "TEXTURE_ARRAY");
		sh->addCompileFlag(SH_BSP_TEX_PAL, "TEXTURE_PAL");
		sh->compile(bsp_vert_glsl, bsp_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{ 2, GL_FLOAT, 0, "vTex" },
			{ 4, GL_UNSIGNED_BYTE, 0, "vAtlas" },
			{ 4, GL_UNSIGNED_BYTE, 0, "vCustom" },
			{ 4, GL_UNSIGNED_SHORT, 0, "vLightmapTex01" },
			{ 4, GL_UNSIGNED_SHORT, 0, "vLightmapTex23" },
			{ 4, GL_UNSIGNED_BYTE, 1, "vColor" },
			{ 3, GL_FLOAT, 0, "vPosition" }
		});
		sh->addUniforms({
			{"sTex", UNIFORM_INT},
			{"sLightmapTex", UNIFORM_INT},
			{"pTex", UNIFORM_INT},
			{"colorMult", UNIFORM_VEC4},
			{"alphaTest", UNIFORM_FLOAT},
			{"gamma", UNIFORM_FLOAT},
			{"wireframeColorDark", UNIFORM_VEC4},
			{"wireframeColorBright", UNIFORM_VEC4},
			{"wireframeThickness", UNIFORM_FLOAT},
			{"wireframeOnly", UNIFORM_FLOAT},
			{"textureAtlasScale", UNIFORM_FLOAT},
			{"lightmapAtlasScale", UNIFORM_FLOAT},
			{"paletteAtlasScale", UNIFORM_VEC2},
			{"lightmapMult", UNIFORM_VEC4},
		});
		sh->setUniform("sTex", 0, true);
		sh->setUniform("wireframeThickness", 0.5f, true);
		sh->setUniform("sLightmapTex", 1, true);
		sh->setUniform("pTex", 2, true);
	}

	{
		ShaderProgram* sh = g_shaders.color;
		sh->compile(cvert_vert_glsl, cvert_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{4, GL_UNSIGNED_BYTE, 1, "vColor"},
			{3, GL_FLOAT, 0, "vPosition"},
		});
		sh->addUniforms({
			{ "colorMult", UNIFORM_VEC4 },
		});
		sh->setUniform("colorMult", vec4(1, 1, 1, 1), true);
	}

	{
		ShaderProgram* sh = g_shaders.clipnode;
		sh->compile(clipnode_vert_glsl, clipnode_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{1, GL_UNSIGNED_SHORT, 0, "vEdges"},
			{4, GL_UNSIGNED_BYTE, 1, "vColor"},
			{3, GL_FLOAT, 0, "vPosition"},
		});
		sh->addUniforms({
			{"colorMult", UNIFORM_VEC4},
			{"wireframeThickness", UNIFORM_FLOAT},
			{"opacity", UNIFORM_FLOAT},
		});
		sh->setUniform("colorMult", vec4(1, 1, 1, 1), true);
		sh->setUniform("wireframeThickness", 0.5f, true);
		sh->setUniform("opacity", 0.5f, true);
	}

	{
		ShaderProgram* sh = g_shaders.texture;
		sh->compile(tvert_vert_glsl, tvert_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{2, GL_FLOAT, 0, "vTex"},
			{3, GL_FLOAT, 0, "vPosition"},
		});
	}

	{
		ShaderProgram* sh = g_shaders.mdl;
		sh->addCompileFlag(SH_MDL_BONE_TEXTURE, "BONE_TEXTURE");
		sh->compile(mdl_vert_glsl, mdl_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{2, GL_FLOAT, 0, "vTex"},
			{3, GL_FLOAT, 1, "vNormal"},
			{3, GL_FLOAT, 0, "vPosition"},
			{1, GL_FLOAT, 0, "vBone"},
		});
		sh->addUniforms({
			{"sTex", UNIFORM_INT},
			{"elights", UNIFORM_INT},
			{"ambient", UNIFORM_VEC3},
			{"lights", UNIFORM_MAT3},
			{"additiveEnable", UNIFORM_INT},
			{"chromeEnable", UNIFORM_INT},
			{"flatshadeEnable", UNIFORM_INT},
			{"colorMult", UNIFORM_VEC4},
			{"viewerOrigin", UNIFORM_VEC3},
			{"viewerRight", UNIFORM_VEC3},
			{"boneMatrixTexture", UNIFORM_INT},
		});
	}

	{
		ShaderProgram* sh = g_shaders.spr;
		sh->compile(spr_vert_glsl, spr_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{2, GL_FLOAT, 0, "vTex"},
			{3, GL_FLOAT, 0, "vPosition"},
		});
		sh->addUniform("color", UNIFORM_VEC4);
	}
	
	{
		ShaderProgram* sh = g_shaders.vec3;
		sh->addCompileFlag(SH_VEC3_DEPTH_HACK, "DEPTH_HACK");
		sh->compile(vec3_vert_glsl, vec3_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{3, GL_FLOAT, 0, "vPosition"}
		});
		sh->addUniforms({
			{"color", UNIFORM_VEC4}
		});
		sh->setUniform("color", vec4(1, 1, 1, 1), true);
	}

	debugf("Compiled %d / %d shaders in %.2f\n",
		(int)(g_renderStats.numShaders - g_renderStats.numShadersFailed), (int)g_renderStats.numShaders,
		glfwGetTime() - startTime);
	glCheckError("compiling shaders");
}

Editor::~Editor() {
	glfwTerminate();
}

void Editor::setupTransformAxes() {
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
		moveAxes.buffer = new VertexBuffer(g_shaders.color, moveAxes.model, 6 * 6 * 4);
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
		scaleAxes.buffer = new VertexBuffer(g_shaders.color, scaleAxes.model, 6 * 6 * 6);
		scaleAxes.numAxes = 6;
	}

	{
		vec3 ori = vec3();
		
		vec3 dirs[3] = {
			vec3(128, 0, 0),
			vec3(0, 128, 0),
			vec3(0, 0, 128),
		};

		COLOR4 colors[3]{
			{ 255, 0, 0, 255 },
			{ 0, 255, 0, 255 },
			{ 0, 0, 255, 255 },
		};
		
		cVert* verts = new cVert[3*2];
		int idx = 0;

		for (int i = 0; i < 3; i++) {
			verts[idx].x = ori.x;
			verts[idx].y = ori.z;
			verts[idx].z = -ori.y;
			verts[idx].c = colors[i];
			idx++;

			vec3 end = ori + dirs[i];
			verts[idx].x = end.x;
			verts[idx].y = end.z;
			verts[idx].z = -end.y;
			verts[idx].c = colors[i];
			idx++;
		}

		originBuffer = new VertexBuffer(g_shaders.color, verts, 3*2, true);
	}

	glCheckError("creating transform axes");

	updateDragAxes();

	glCheckError("updating transform axes");
}

void Editor::renderLoop() {
	glCheckError("entering renderloop");

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glCheckError("renderloop state enable");

	setupTransformAxes();

	glCheckError("pre render loop");

	int loadState = 0;

	fgdFuture = async(launch::async, &Editor::loadFgds, this);

	float lastFrameTime = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (isIconified) {
			sleepms(50);
			continue;
		}

		float frameDelta = glfwGetTime() - lastFrameTime;
		frameTimeScale = 0.05f / frameDelta;
		float fps = 1.0f / frameDelta;
		
		//FIXME : frameTimeScale = 0.05f / frameDelta ???
		frameTimeScale = 144.0f / fps;
		lastFrameTime = glfwGetTime();
		isLoading = reloading;

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		mapRenderer->delayLoadData();
		drawViewport();
		glActiveTexture(GL_TEXTURE0); // needed even if gui isn't drawn(???)

		// updated here so imgui can use control logic from this class
		controlsBegin();

		if (!g_app->hideGui) {
			gui->draw();
			g_active_shader_program = -1;
		}
		else {
			gui->texts.clear();
		}

		viewportControls();
		controlsEnd();

		glfwSwapBuffers(window);
		glCheckError("Swap buffers and controls");

		if (!isLoading && openMapAfterLoad.size()) {
			openMap(openMapAfterLoad.c_str());
			glCheckError("Opening map");
		}

		if (reloading && fgdFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
			postLoadFgds();
			reloading = reloadingGameDir = false;
			glCheckError("FGD post load");
		}

		if (!isFocused && !isHovered) {
			sleepms(50);
		}

		if (loadState == 0) {
			debugf("Startup finished in %.2fs\n", glfwGetTime() - programStartTime);
			loadState = 1;
			programStartTime = -programStartTime;
		}
		if (loadState == 1 && !isLoading) {
			debugf("Map loaded in %.2fs\n", glfwGetTime() - programStartTime);
			loadState = 2;
		}
	}

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
	clearStringMap();

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

void Editor::updateWindowTitle() {
	string map = mapRenderer->map->path;
	string title = map.empty() ? "bspguy" : getAbsolutePath(map) + " - bspguy";
	glfwSetWindowTitle(window, title.c_str());
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

	logf("Loaded map: %s\n", map->path.c_str());
}

void Editor::saveSettings() {
	g_settings.debug_open = gui->widgets[WIDGET_DEBUG]->widgetVisible;
	g_settings.keyvalue_open = gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible;
	g_settings.transform_open = gui->widgets[WIDGET_TRANSFORM]->widgetVisible;
	g_settings.log_open = gui->widgets[WIDGET_MESSAGES]->widgetVisible;
	g_settings.settings_open = gui->widgets[WIDGET_SETTINGS]->widgetVisible;
	g_settings.limits_open = gui->widgets[WIDGET_LIMITS]->widgetVisible;
	g_settings.entreport_open = gui->widgets[WIDGET_ENT_REPORT]->widgetVisible;
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

	swapPointEntRenderer = new PointEntRenderer(mergedFgd, fgds);
}

void Editor::drawViewport() {
	setupView();
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	glCheckError("Setting up view");

	if (previewMode || (g_settings.render_flags & RENDER_SKYBOX)) {
		bool wireframeOnly = !(g_settings.render_flags & (RENDER_LIGHTMAPS | RENDER_TEXTURES))
			&& (g_settings.render_flags & RENDER_WIREFRAME);
		if (!wireframeOnly) { // skybox can make lines hard to see in this mode
			mapRenderer->drawSkybox();
			glCheckError("Rendering skybox");
		}
	}

	mapRenderer->updateOrderEnts();

	// draw opaque world/entity faces
	mapRenderer->renderSolids(transformTarget == TRANSFORM_VERTEX, false);
	mapRenderer->drawPointEntities();

	glCheckError("Rendering BSP (opaque pass)");

	// studio models have transparent boxes that need to draw over the world but behind transparent
	// brushes like a trigger_once which is rendered using the clipnode model
	if (modelRenderer->drawModelsAndSprites(mapRenderer->renderOffset, cameraOrigin, cameraAngles)) {
		isLoading = true;
	}
	glCheckError("Rendering models and sprites");

	// draw transparent entity faces
	mapRenderer->renderSolids(transformTarget == TRANSFORM_VERTEX, true);

	// don't draw clipnodes in leaf mode because they're the same color/style and confuse picking
	if (pickMode != PICK_LEAF)
		mapRenderer->renderClipnodes(clipnodeRenderHull);

	glCheckError("Rendering BSP (transparency pass)");

	if (mapArrangeMode)
		drawArrangeMaps();

	if (pickMode == PICK_LEAF && !previewMode) {
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

	if (!previewMode) {
		g_shaders.color->bind();
		drawEntDirectionVectors(); // draws over world faces
		glCheckError("Rendering entity vectors");

		drawTextureAxes();
		glCheckError("Rendering texture axes");

		if ((g_settings.render_flags & (RENDER_ORIGIN | RENDER_MAP_BOUNDARY)) || hasCullbox) {
			g_shaders.color->bind();
			model.loadIdentity();
			g_shaders.color->pushMatrix(MAT_MODEL);
			g_shaders.color->updateMatrixes();
			glDisable(GL_CULL_FACE);

			if ((g_settings.render_flags & RENDER_MAP_BOUNDARY) && !emptyMapLoaded) {
				drawMapBoundary();
			}

			if (pickInfo.getEnt()) {
				vec3 offset = mapRenderer->renderOffset;
				model.translate(offset.x, offset.y, offset.z);
			}
			g_shaders.color->updateMatrixes();

			if (hasCullbox) {
				drawBox(cullMins, cullMaxs, COLOR4(255, 0, 0, 64));
			}

			if (g_settings.render_flags & RENDER_ORIGIN) {
				originBuffer->draw(g_shaders.color, GL_LINES);
			}

			glEnable(GL_CULL_FACE);
			g_shaders.color->popMatrix(MAT_MODEL);
		}
		glCheckError("Rendering map boundary/cull box");

		drawEntConnections();
		glCheckError("Rendering entity connections");

		bool isScalingObject = transformMode == TRANSFORM_SCALE && transformTarget == TRANSFORM_OBJECT;
		bool isMovingOrigin = transformMode == TRANSFORM_MOVE && transformTarget == TRANSFORM_ORIGIN && originSelected;
		bool isTransformingValid = ((isTransformableSolid && !modelUsesSharedStructures) || !isScalingObject) && transformTarget != TRANSFORM_ORIGIN;
		bool isTransformingWorld = pickInfo.getEntIndex() == 0 && transformTarget != TRANSFORM_OBJECT;
		if (showDragAxes && !movingEnt && !isTransformingWorld && pickInfo.getEntIndex() >= 0 && (isTransformingValid || isMovingOrigin)) {
			drawTransformAxes();
			glCheckError("Rendering transform axes");
		}

		int modelIdx = pickInfo.getModelIndex();
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

		drawDebugObjects();
		glCheckError("Rendering debug polys");

		navRenderer->renderNavMesh(mapRenderer->map, cameraOrigin);

		if (pickMode == PICK_LEAF && (g_settings.render_flags & RENDER_LEAF_GRAPH)) {
			navRenderer->renderLeafGraph(mapRenderer->leafNavMesh, cameraOrigin, mapRenderer->map);
		}

		addNameTags();
	}

	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);
	//logf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

	drawMouseObjects();
	glCheckError("Draw mouse objects");
}

void Editor::drawMapBoundary() {
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

		drawPlane(plane, red, g_settings.mapsize_max * 1.2f);
	}

	{
		VertexBuffer buffer(g_shaders.color, &cube, 6 * 6);
		buffer.upload();
		buffer.draw(g_shaders.color, GL_TRIANGLES);
	}
	glDepthFunc(GL_LEQUAL);

	glDepthFunc(GL_LEQUAL); // draw lines in front (still causes some z fighting)
	drawBoxOutline(vec3(), g_settings.mapsize_max * 2, COLOR4(0, 0, 0, 255));

	glDepthFunc(GL_LESS);
}

void Editor::drawDebugObjects() {
	int modelIdx = pickInfo.getModelIndex();

	if (debugClipnodes && modelIdx > 0) {
		BSPMODEL* pickModel = pickInfo.getModel();
		glDisable(GL_CULL_FACE);
		int currentPlane = 0;
		drawClipnodes(pickInfo.getMap(), pickModel->iHeadnodes[1], currentPlane, debugInt);
		debugIntMax = currentPlane - 1;
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

	//glCheckError("Rendering debug clipnodes");
}

void Editor::drawMouseObjects() {
	if (cameraMouseCapture || isBoxSelecting) {
		g_shaders.color->bind();
		g_shaders.color->pushMatrix(MAT_PROJECTION);
		g_shaders.color->pushMatrix(MAT_VIEW);
		g_shaders.color->pushMatrix(MAT_MODEL);
		projection.ortho(0, windowWidth, windowHeight, 0, -1.0f, 1.0f);
		view.loadIdentity();
		model.loadIdentity();
		g_shaders.color->updateMatrixes();
		glDisable(GL_DEPTH_TEST);

		if (cameraMouseCapture) {
			int border = 1;
			int thick = 2;
			int len = 12;
			vec2 center(windowWidth / 2, windowHeight / 2);

			drawRect2D(center - vec2(len + border, thick / 2 + border), vec2(len * 2 + border * 2, thick + border * 2), COLOR4(0, 0, 0, 255));
			drawRect2D(center - vec2(thick / 2 + border, len + border), vec2(thick + border * 2, len * 2 + border * 2), COLOR4(0, 0, 0, 255));

			drawRect2D(center - vec2(len, thick / 2), vec2(len * 2, thick), COLOR4(255, 255, 255, 255));
			drawRect2D(center - vec2(thick / 2, len), vec2(thick, len * 2), COLOR4(255, 255, 255, 255));
		}

		bool boxBigEnough = (boxSelectEnd - boxSelectStart).length() > 8;
		if (isBoxSelecting && boxBigEnough && draggingAxis == -1) {
			drawLine2D(vec2(boxSelectStart.x, boxSelectStart.y), vec2(boxSelectEnd.x, boxSelectStart.y), COLOR4(255, 255, 255, 255));
			drawLine2D(vec2(boxSelectEnd.x, boxSelectStart.y), vec2(boxSelectEnd.x, boxSelectEnd.y), COLOR4(255, 255, 255, 255));
			drawLine2D(vec2(boxSelectEnd.x, boxSelectEnd.y), vec2(boxSelectStart.x, boxSelectEnd.y), COLOR4(255, 255, 255, 255));
			drawLine2D(vec2(boxSelectStart.x, boxSelectEnd.y), vec2(boxSelectStart.x, boxSelectStart.y), COLOR4(255, 255, 255, 255));
		}

		glEnable(GL_DEPTH_TEST);
		g_shaders.color->popMatrix(MAT_PROJECTION);
		g_shaders.color->popMatrix(MAT_VIEW);
		g_shaders.color->popMatrix(MAT_MODEL);
	}
}

void Editor::drawArrangeMaps() {
	struct RenderMap {
		BspRenderer* renderer;
		Entity* controlEnt;
		vec3 mins, maxs;
	};
	vector<RenderMap> renderMaps;

	int idx = 1;
	for (BspRenderer* arrangeBsp : arrangeBsps) {
		Entity* controlEnt = mapRenderer->map->ents[idx++];
		arrangeBsp->map->ents[0]->setOrAddKeyvalue("origin", controlEnt->getOrigin().toKeyvalueString());

		arrangeBsp->updateOrderEnts();

		RenderMap rmap;
		rmap.controlEnt = controlEnt;
		rmap.renderer = arrangeBsp;
		arrangeBsp->map->get_bounding_box(rmap.mins, rmap.maxs);

		renderMaps.push_back(rmap);
	}

	for (RenderMap& arrangeBsp : renderMaps) {
		// opaque pass
		arrangeBsp.renderer->renderSolids(false, false);
		arrangeBsp.renderer->drawPointEntities();
	}

	for (RenderMap& arrangeBsp : renderMaps) {
		// transparency pass
		arrangeBsp.renderer->renderSolids(false, true);
		arrangeBsp.renderer->renderClipnodes(clipnodeRenderHull);
	}

	g_shaders.color->bind();
	g_shaders.color->modelMat->loadIdentity();
	g_shaders.color->updateMatrixes();

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

void Editor::drawModelVerts() {
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
	g_shaders.color->updateMatrixes();
	modelVertBuff->draw(g_shaders.color, GL_TRIANGLES);
}

void Editor::drawModelOrigin() {
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

	vec3 ori = transformedOrigin;
	float s = (ori - cameraOrigin).length() * vertExtentFactor;
	ori = ori.flip() + renderOffset;

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
	g_shaders.color->updateMatrixes();
	modelOriginBuff->draw(g_shaders.color, GL_TRIANGLES);
}

void Editor::drawTransformAxes() {
	if (!canTransform) {
		return;
	}

	glClear(GL_DEPTH_BUFFER_BIT);

	updateDragAxes();

	glDisable(GL_CULL_FACE);

	if (transformMode == TRANSFORM_SCALE && transformTarget == TRANSFORM_OBJECT) {
		vec3 ori = scaleAxes.origin;
		model.translate(ori.x, ori.z, -ori.y);
		g_shaders.color->updateMatrixes();
		scaleAxes.buffer->upload();
		scaleAxes.buffer->draw(g_shaders.color, GL_TRIANGLES);
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
		g_shaders.color->updateMatrixes();
		moveAxes.buffer->upload();
		moveAxes.buffer->draw(g_shaders.color, GL_TRIANGLES);
	}
}

void Editor::drawEntConnections() {
	if (g_settings.render_flags & RENDER_ENT_CONNECTIONS) {
		model.loadIdentity();
		model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);
		g_shaders.color->updateMatrixes();

		if (entConnections) {
			entConnections->draw(g_shaders.color, GL_LINES);
		}

		if (entConnectionPoints) {
			glDisable(GL_DEPTH_TEST);
			entConnectionPoints->draw(g_shaders.color, GL_TRIANGLES);
			glEnable(GL_DEPTH_TEST);
		}
	}
}

void Editor::updateEntDirectionVectors() {
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

	entDirectionVectors = new VertexBuffer(g_shaders.color, arrows, numPointers * arrowVerts, true);
	entDirectionVectors->upload();
}

void Editor::drawEntDirectionVectors() {
	if (!entDirectionVectors) {
		return;
	}

	glCullFace(GL_FRONT);
	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);

	g_shaders.color->bind();
	model.loadIdentity();
	model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);
	g_shaders.color->updateMatrixes();
	entDirectionVectors->draw(g_shaders.color, GL_TRIANGLES);

	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
	glCullFace(GL_BACK);
}

void Editor::updateTextureAxes() {
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

	allTextureAxes = new VertexBuffer(g_shaders.color, uploadVerts, verts.size(), true);
	allTextureAxes->upload();
}

void Editor::drawTextureAxes() {
	if (!allTextureAxes) {
		return;
	}

	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);

	g_shaders.color->bind();
	model.loadIdentity();
	model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);
	g_shaders.color->updateMatrixes();
	allTextureAxes->draw(g_shaders.color, GL_LINES);

	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
}

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

	if (!io.WantTextInput && !io.WantCaptureMouse && !guiWasFocused) {
		globalShortcutControls();
		shortcutControls();
	}

	if (io.WantTextInput) {
		guiWasFocused = true;
	}

	if (!io.WantCaptureMouse) {
		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);
		vec2 mousePos(xpos, ypos);

		cameraContextMenus();

		cameraRotationControls(mousePos);

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

		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);

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

void Editor::cameraRotationControls(vec2 mousePos) {
	static double lastTime = 0;
	double now = glfwGetTime();
	double deltaTime = now - lastTime;
	lastTime = now;
	float ymult = g_settings.invert_y_axis ? -1 : 1;

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
			cameraAngles.z += drag.x * rotationSpeed*0.1f;
			cameraAngles.x += drag.y * rotationSpeed*0.1f * ymult;

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

void Editor::moveGrabbedEnts() {
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
			mapRenderer->pickFrustum(getPickFrustum(), boxSelectEnts, boxSelectFaces, boxSelectLeaves, clipnodeRenderHull);
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
	}

	postSelectEnt();
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
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	return getPickRay(vec2(xpos, ypos), start, pickDir);
}

void Editor::getPickRay(vec2 mousePos, vec3& start, vec3& pickDir) {
	// invert ypos
	mousePos.y = windowHeight - mousePos.y;

	// translate mouse coordinates so that the origin lies in the center and is a scaler from +/-1.0
	float mouseX = ((mousePos.x / (double)windowWidth) * 2.0f) - 1.0f;
	float mouseY = ((mousePos.y / (double)windowHeight) * 2.0f) - 1.0f;

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

void Editor::setupView() {
	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	glViewport(0, 0, windowWidth, windowHeight);

	projection.perspective(fov, (float)windowWidth / (float)windowHeight, zNear, zFar);

	view.loadIdentity();
	view.rotateZ(PI * cameraAngles.y / 180.0f);
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}

void Editor::addMap(Bsp* map) {
	g_settings.addRecentFile(map->path);
	g_settings.save(); // in case the program crashes
	
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

	glCheckError("add map");
}

void Editor::addNameTags() {
	Bsp* map = mapRenderer->map;

	bool renderAllTags = g_settings.render_flags & RENDER_NAME_TAGS;
	if (!renderAllTags && pickInfo.ents.empty() || map->ents.empty())
		return;

	if (!map->valid || map->modelCount == 0)
		return;

	unordered_set<int> selected;
	for (int i : pickInfo.ents)
		selected.insert(i);

	struct TagEnt {
		Entity* ent;
		int idx;
		float dist;
		vec3 ori;
		string text;
		COLOR4 color;
	};

	vector<TagEnt> tags;
	vec3 worldOffset = map->ents[0]->getOrigin();

	vector<Entity*> ents = map->ents;
	if (!renderAllTags) {
		ents = pickInfo.getEnts();
	}

	for (int i = 0; i < ents.size(); i++) {
		Entity* ent = ents[i];
		if (ent->hidden)
			continue;

		string tname = ent->getTargetname();
		if (tname.empty())
			continue;

		bool isSelected = selected.count(i);
		bool isLinked = false;
		COLOR4 color = isSelected ? COLOR4(255, 64, 64, 255) : COLOR4(200, 200, 200, 255);

		if (!isSelected) {
			auto item = entLinks.find(i);

			if (item != entLinks.end()) {
				if (item->second == 3) {
					color = COLOR4(64, 255, 64, 255);
					isLinked = true;
				}
				else if (item->second == 1) {
					color = COLOR4(255, 255, 32, 255);
					isLinked = true;
				}
				else if (item->second == 2) {
					color = COLOR4(64, 255, 255, 255);
					isLinked = true;
				}
			}
		}

		bool isColored = isSelected || isLinked;

		if (!isColored && !renderAllTags)
			continue;

		vec3 ori = ent->getOrigin() + worldOffset;
		int modelIdx = ent->getBspModelIdx();

		if (modelIdx == -1) {
			if (!(g_settings.render_flags & RENDER_POINT_ENTS))
				continue;

			EntCube* cube = mapRenderer->pointEntRenderer->getEntCube(ent);
			ori += vec3(0, 0, cube->mins.z);
		}
		else {
			if (!(g_settings.render_flags & RENDER_ENTS))
				continue;

			BSPMODEL& model = map->models[modelIdx];
			float oldZ = ori.z;
			ori += model.nMins + (model.nMaxs - model.nMins) * 0.5f;
			ori.z = oldZ + model.nMins.z;
		}

		vec3 tpos = worldToScreen(ori);

		if (tpos.z < 0)
			continue;

		float dist = (ori - cameraOrigin).length();
		if (!isColored && dist > g_settings.zFarMdl) {
			continue;
		}

		if (!isColored && map->pointContents(map->models[0].iHeadnodes[0], cameraOrigin, 0) != CONTENTS_SOLID) {
			// TODO: trace lines are broken. Some faces don't clip the trace
			TraceResult tr;
			map->traceHull(cameraOrigin, ori, 0, &tr);
			//drawLine(cameraOrigin - vec3(0, 0, 4), tr.vecEndPos, COLOR4(255, 0, 0, 255));
			if ((tr.vecEndPos - ori).length() > 32)
				continue;
		}

		TagEnt tag;
		tag.ent = ent;
		tag.idx = i;
		tag.dist = dist;
		tag.color = color;
		tag.text = tname;
		tag.ori = tpos;
		tag.dist = isColored ? dist * 0.00001f : dist; // always draw highlighted stuff first, less chance of limiting
		tags.push_back(tag);
	}

	// in case some can't be drawn due to text limits
	sort(tags.begin(), tags.end(), [](TagEnt& a, TagEnt& b) {
		return a.dist < b.dist;
	});

	for (TagEnt& tag : tags) {
		gui->addText(Text2D(tag.ori.x, tag.ori.y, tag.text, TEXT2D_ALIGN_CENTER, tag.color));
	}
}

vec3 Editor::getEntOrigin(Bsp* map, Entity* ent) {
	return ent->getOrigin() + getEntOffset(map, ent);
}

vec3 Editor::getEntOffset(Bsp* map, Entity* ent) {
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

void Editor::updateDragAxes() {
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

void Editor::updateModelVerts() {

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
	
	modelOriginBuff = new VertexBuffer(g_shaders.color, &modelOriginCube, 6 * 6);
	modelOriginBuff->upload();

	updateSelectionSize();

	modelUsesSharedStructures = false;
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
	modelVertBuff = new VertexBuffer(g_shaders.color, modelVertCubes, 6 * 6 * numCubes);
	modelVertBuff->upload();
	//logf("%d intersection points\n", modelVerts.size());
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

void Editor::updateEntConnections() {
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

	entLinks.clear();

	if (!(g_settings.render_flags & RENDER_ENT_CONNECTIONS)) {
		return;
	}	

	unordered_set<int> testedTargets;

	if (pickInfo.getMap() && pickInfo.getEnt()) {
		Bsp* map = pickInfo.getMap();

		const COLOR4 targetColor = { 255, 255, 0, 255 };
		const COLOR4 callerColor = { 0, 255, 255, 255 };
		const COLOR4 bothColor = { 0, 255, 0, 255 };

		for (int i = 0; i < pickInfo.ents.size(); i++) {
			int entidx = pickInfo.ents[i];
			Entity* self = map->ents[entidx];
			const StringSet& selfNames = self->getAllTargetnames();

			for (int k = 0; k < map->ents.size(); k++) {
				Entity* ent = map->ents[k];

				if (k == entidx)
					continue;

				if (testedTargets.count(k))
					continue;
				
				const StringSet& tnames = ent->getAllTargetnames();
				bool isTarget = tnames.size() && self->hasTarget(tnames);
				bool isCaller = selfNames.size() && ent->hasTarget(selfNames);

				EntConnection link;
				memset(&link, 0, sizeof(EntConnection));
				link.self = self;
				link.target = ent;

				if (isTarget && isCaller) {
					link.color = bothColor;
					entLinks[k] = 3;
					entConnectionLinks.push_back(link);
				}
				else if (isTarget) {
					link.color = targetColor;
					entLinks[k] = 1;
					entConnectionLinks.push_back(link);
				}
				else if (isCaller) {
					link.color = callerColor;
					entLinks[k] = 2;
					entConnectionLinks.push_back(link);
				}
			}

			testedTargets.insert(entidx);
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

		entConnections = new VertexBuffer(g_shaders.color, lines, numVerts, true);
		entConnectionPoints = new VertexBuffer(g_shaders.color, points, numPoints * 6 * 6, true);
		entConnections->upload();
		entConnectionPoints->upload();
	}
}

void Editor::updateEntConnectionPositions() {
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
	entConnectionPoints->upload();
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

void Editor::grabEnts() {
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

void Editor::unhideSelectedEnts() {
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

void Editor::hideSelectedEnts() {
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

void Editor::unhideEnts() {
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
	mapRenderer->reloadMegaBuffers();
	logf("Unhid %d entities\n", numHidden);
}

void Editor::cutEnts() {
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

void Editor::copyEnts(bool stringifyBspModels) {
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

bool Editor::canPasteEnts() {
	const char* clipBoardText = ImGui::GetClipboardText();
	if (!clipBoardText) {
		return false;
	}

	CreateEntityFromTextCommand createCommand("", clipBoardText);
	return !createCommand.parse().empty();
}

void Editor::pasteEnts(bool noModifyOrigin) {
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

void Editor::pasteEntsFromText(string text, bool noModifyOrigin) {
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

void Editor::deleteEnts() {
	if (pickInfo.getEntIndex() <= 0 || mapArrangeMode)
		return;

	DeleteEntitiesCommand* deleteCommand = new DeleteEntitiesCommand("Delete Entity", pickInfo.ents);
	deleteCommand->execute();
	pushUndoCommand(deleteCommand);
}

void Editor::deselectObject() {
	if (pickInfo.getEnt() && pickInfo.getEnt()->isBspModel())
		saveLumpState(pickInfo.getMap(), 0xffffffff, true);

	// update deselected point ents
	for (int entIdx : pickInfo.ents) {
		Entity* ent = pickInfo.getMap()->ents[entIdx];
		if (!ent->isBspModel()) {
			mapRenderer->refreshPointEnt(entIdx, false);
		}
	}
	mapRenderer->pointEnts->deleteBuffer();
	mapRenderer->pointEnts->upload();

	pickInfo.deselect();
	isTransformableSolid = true;
	modelUsesSharedStructures = false;
	hoverVert = -1;
	hoverEdge = -1;
	hoverAxis = -1;
	updateEntConnections();
}

void Editor::deselectFaces() {
	mapRenderer->highlightPickedFaces(false);
	pickInfo.deselect();
}

void Editor::postSelectEnt() {
	updateSelectionSize();
	updateEntConnections();
	updateEntityUndoState();
	pickCount++; // force transform window update
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


void Editor::ungrabEnts() {
	if (!movingEnt) {
		return;
	}

	movingEnt = false;

	int plural = pickInfo.ents.size() > 1;
	pushEntityUndoState(plural ? "Move Entities" : "Move Entity");
	pickCount++; // force transform window to recalc offsets
}

void Editor::updateEntityUndoState() {
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

void Editor::saveLumpState(Bsp* map, int targetLumps, bool deleteOldState) {
	if (deleteOldState) {
		for (int i = 0; i < HEADER_LUMPS; i++) {
			if (undoLumpState.lumps[i])
				delete[] undoLumpState.lumps[i];
		}
	}

	undoLumpState = map->duplicate_lumps(targetLumps);
}

void Editor::updateEntityLumpUndoState(Bsp* map) {
	if (undoLumpState.lumps[LUMP_ENTITIES])
		delete[] undoLumpState.lumps[LUMP_ENTITIES];

	LumpState dupLump = map->duplicate_lumps(LUMP_ENTITIES);
	undoLumpState.lumps[LUMP_ENTITIES] = dupLump.lumps[LUMP_ENTITIES];
	undoLumpState.lumpLen[LUMP_ENTITIES] = dupLump.lumpLen[LUMP_ENTITIES];
}

bool Editor::canPushEntityUndoState() {
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

void Editor::pushEntityUndoState(string actionDesc) {
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

void Editor::getWindowSize(int& width, int& height) {
	glfwGetWindowSize(window, &width, &height);
}

void Editor::handleResize(int width, int height) {
	gui->windowResized(width, height);
}

bool Editor::entityHasFgd(string cname) {
	return mergedFgd ? mergedFgd->getFgdClass(cname) != NULL : false;
}

bool Editor::confirmMapExit() {
	if (emptyMapLoaded || mapArrangeMode)
		return true;

	if (g_settings.confirm_exit) {
		Bsp* map = mapRenderer->map;

		if (map->did_lumps_change()) {
			string msg = "Save changes to " + map->name + "?";
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

vector<Entity*>& Editor::ents() {
	static vector<Entity*> dummyList;
	return (mapRenderer && mapRenderer->map) ? mapRenderer->map->ents : dummyList;
}