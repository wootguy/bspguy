#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include "remap.h"
#include "bsptypes.h"
#include "qtools/rad.h"
#include "Fgd.h"

class Entity;
class Texture;

struct ModelInfo {
	string classname;
	string targetname;
	string model;
	string val;
	string usage;
	int entIdx;
};

struct AllocInfo {
	string texname;
	string faceCount;
	string val;
	string usage;
	float sort;
	int faceIdx;
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
	void copyLightmap();
	void pasteLightmap();
	void refresh();
	void saveAs();
	const char* openMap();
	void windowResized(int width, int height);

private:
	bool vsync = true;
	bool polycount = false;
	bool showDebugWidget = false;
	bool showKeyvalueWidget = false;
	bool showTransformWidget = false;
	bool showLogWidget = false;
	bool showSettingsWidget = false;
	bool showHelpWidget = false;
	bool showAboutWidget = false;
	bool showLimitsWidget = true;
	bool showTextureWidget = false;
	bool showLightmapEditorWidget = false;
	bool showLightmapEditorUpdate = true;
	bool showEntityReport = false;
	bool reloadSettings = true;
	int settingsTab = 0;
	bool openSavedTabs = false;
	bool transparentClipnodes = true;

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
	vector<AllocInfo> limitAllocs;
	bool loadedStats = false;
	vector<StatInfo> stats;

	bool anyHullValid[MAX_MAP_HULLS] = { false };

	int guiHoverAxis; // axis being hovered in the transform menu
	int contextMenuEnt = -1; // open entity context menu if >= 0
	int emptyContextMenu = 0; // open context menu for rightclicking world/void

	int copiedMiptex = -1;
	int copiedLightmapFace = -1; // index into faces
	LIGHTMAP copiedLightmap = LIGHTMAP();
	bool refreshSelectedFaces = false;

	ImGuiTextBuffer Buf = ImGuiTextBuffer();
	ImVector<int> LineOffsets; // Index to lines offset. We maintain this with AddLog() calls, allowing us to have a random access on lines
	bool AutoScroll = true;  // Keep scrolling if already at the bottom

	bool entityReportFilterNeeded = true;
	bool entityReportReselectNeeded = false;

	float mainMenuBarHeight;

	void draw3dContextMenus();
	void drawMenuBar();
	void drawStatusBar();
	void drawPopups();
	void drawToolbar();
	void drawStatusMessage();
	void drawDebugWidget();
	void drawKeyvalueEditor();
	void drawKeyvalueEditor_SmartEditTab(Fgd* fgd);
	void drawKeyvalueEditor_SmartEditTab_GroupKeys(vector<KeyvalueDef>& keys, float inputWidth, bool isGrouped, int keyOffset);
	void drawKeyvalueEditor_FlagsTab(Fgd* fgd);
	void drawKeyvalueEditor_RawEditTab();
	void drawTransformWidget();
	void drawLog();
	void drawSettings();
	void drawHelp();
	void drawAbout();
	void drawLimits();
	void drawLimitsSummary(Bsp* map, bool modalMode);
	void drawLightMapTool();
	void drawTextureTool();
	void drawLimitTab(Bsp* map, int sortMode);
	void drawAllocBlockLimitTab(Bsp* map);
	void drawEntityReport();
	StatInfo calcStat(string name, uint val, uint max, bool isMem);
	ModelInfo calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, uint val, uint max, bool isMem);
	void checkValidHulls();
	void reloadLimits();

	void clearLog();
	void addLog(const char* s);
	void loadFonts();
	void checkFaceErrors();
	string getUserLayoutPath(); // path to user's saved widget layout
};