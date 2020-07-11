#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include "Entity.h"
#include "remap.h"
#include "bsptypes.h"
#include "Texture.h"

struct ModelInfo {
	string classname;
	string targetname;
	string model;
	string val;
	string usage;
	int entIdx;
};

struct StatInfo {
	string name;
	string val;
	string max;
	string fullness;
	float progress;
	ImVec4 color;
};

class Renderer;

class Gui {
	friend class Renderer;

public:
	Renderer* app;

	Gui(Renderer* app);

	void init();
	void draw();

	// -1 for empty selection
	void openContextMenu(int entIdx);
	void copyTexture();
	void pasteTexture();

private:
	bool vsync = true;
	bool showDebugWidget = false;
	bool showKeyvalueWidget = false;
	bool showTransformWidget = false;
	bool showLogWidget = false;
	bool showSettingsWidget = false;
	bool showHelpWidget = false;
	bool showAboutWidget = false;
	bool showLimitsWidget = true;
	bool showTextureWidget = false;
	bool reloadSettings = true;
	int settingsTab = 0;
	int transformTab = 0;
	bool openSavedTabs = false;

	ImFont* smallFont;
	ImFont* largeFont;
	ImFont* consoleFont;
	ImFont* consoleFontLarge;
	int fontSize = 22;
	bool shouldReloadFonts = false;
	bool shouldReloadTextureInfo = false;

	Texture* objectIconTexture;
	Texture* faceIconTexture;

	bool badSurfaceExtents = false;
	bool lightmapTooLarge = false;

	bool loadedLimit[SORT_MODES] = { false };
	vector<ModelInfo> limitModels[SORT_MODES];
	bool loadedStats = false;
	vector<StatInfo> stats;

	bool anyHullValid[MAX_MAP_HULLS] = { false };

	int guiHoverAxis; // axis being hovered in the transform menu
	int contextMenuEnt = -1; // open entity context menu if >= 0
	int emptyContextMenu = 0; // open context menu for rightclicking world/void

	int copiedMiptex = -1;
	bool refreshSelectedFaces = false;

	ImGuiTextBuffer Buf;
	ImVector<int> LineOffsets; // Index to lines offset. We maintain this with AddLog() calls, allowing us to have a random access on lines
	bool AutoScroll = true;  // Keep scrolling if already at the bottom

	void draw3dContextMenus();
	void drawMenuBar();
	void drawToolbar();
	void drawFpsOverlay();
	void drawStatusMessage();
	void drawDebugWidget();
	void drawKeyvalueEditor();
	void drawKeyvalueEditor_SmartEditTab(Entity* ent);
	void drawKeyvalueEditor_FlagsTab(Entity* ent);
	void drawKeyvalueEditor_RawEditTab(Entity* ent);
	void drawTransformWidget();
	void drawLog();
	void drawSettings();
	void drawHelp();
	void drawAbout();
	void drawLimits();
	void drawTextureTool();
	void drawLimitTab(Bsp* map, int sortMode);
	StatInfo calcStat(string name, uint val, uint max, bool isMem);
	ModelInfo calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, uint val, uint max, bool isMem);
	void reloadLimits();
	void checkValidHulls();

	void clearLog();
	void addLog(const char* s);
	void loadFonts();
	void checkFaceErrors();
};