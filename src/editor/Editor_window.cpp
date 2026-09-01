#include "Editor.h"
#include "BspRenderer.h"
#include "Gui.h"
#include "FrameBuffer.h"
#include "lodepng.h"

#include "icons/app.h"
#include "icons/app2.h"

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
	static bool isExiting;

	if (isExiting)
		return; // prevent duplicate dialogs on linux

	isExiting = true;
	g_app->exit();
	isExiting = false;
}

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

		if (g_settings.fullscreen) {
			toggleFullscreen();
			oldWindowW = g_settings.windowWidth;
			oldWindowH = g_settings.windowHeight;
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

void Editor::updateWindowTitle() {
	string map = mapRenderer->map->path;
	string title = map.empty() ? "bspguy" : getAbsolutePath(map) + " - bspguy";
	glfwSetWindowTitle(window, title.c_str());
}

void Editor::toggleFullscreen() {
	if (g_settings.fullscreen) {
		glfwGetWindowPos(window, &oldWindowX, &oldWindowY);
		oldWindowW = windowWidth;
		oldWindowH = windowHeight;

		GLFWmonitor* bestMonitor = GetMonitorForWindow(window);
		const GLFWvidmode* mode = glfwGetVideoMode(bestMonitor);

		glfwSetWindowMonitor(window, bestMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);

		handleResize(mode->width, mode->height);
	}
	else {
		glfwSetWindowMonitor(window, NULL, oldWindowX, oldWindowY, oldWindowW, oldWindowH, 0);
		glfwSetWindowSize(window, oldWindowW, oldWindowH);
		handleResize(oldWindowW, oldWindowH);
	}
}

void Editor::getWindowSize(int& width, int& height) {
	glfwGetWindowSize(window, &width, &height);
}

void Editor::handleResize(int width, int height) {
	gui->windowResized(width, height);
	windowWidth = width;
	windowHeight = height;

	if (viewportFbo) {
		delete viewportFbo;
		viewportFbo = new FrameBuffer(windowWidth, windowHeight, viewportScale);
	}
}
