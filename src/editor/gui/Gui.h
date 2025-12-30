#pragma once
#include <GL/glew.h>
#include "imgui.h"
#include "imgui_freetype.h" 
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include "remap.h"
#include "bsptypes.h"
#include "qtools/rad.h"
#include "Fgd.h"

class Entity;
class Texture;
class Widget;
class MenuBar;

enum WidgetIds {
	WIDGET_DEBUG,
	WIDGET_KEYVALUE_EDITOR,
	WIDGET_TRANSFORM,
	WIDGET_MESSAGES,
	WIDGET_SETTINGS,
	WIDGET_HELP,
	WIDGET_ABOUT,
	WIDGET_LIMITS,
	WIDGET_ENT_REPORT,
	WIDGET_FACE_EDITOR,
	WIDGET_RAD_PREP,
	WIDGET_DEDUP_MODELS,
	WIDGET_MERGE_OVERLAP,
	WIDGET_MERGE_FAILED,
	WIDGET_MERGE_MULTI,
	WIDGET_FIX_EXTENTS,
	WIDGET_MODEL_MERGE_CONFIRM,
	NUM_WIDGET_IDS
};

enum Text2dAlignment {
	TEXT2D_ALIGN_LEFT,
	TEXT2D_ALIGN_CENTER,
	TEXT2D_ALIGN_RIGHT,
};

struct Text2D {
	int x, y;
	int align;
	string text;
	COLOR4 color;

	Text2D(int x, int y, string text, int align=TEXT2D_ALIGN_LEFT, COLOR4 color = COLOR4(255, 255, 255, 255))
		: text(text), x(x), y(y), align(align), color(color) { }

	Text2D(vec2 pos, string text, int align = TEXT2D_ALIGN_LEFT, COLOR4 color = COLOR4(255, 255, 255, 255))
		: text(text), x(pos.x), y(pos.y), align(align), color(color) {}
};

class Renderer;

extern int g_font_scale_base;
extern float g_tooltip_delay;

extern char const* bspFilterPatterns[1];
extern char const* entFilterPatterns[1];
extern char const* wadFilterPatterns[1];
extern char const* prtFilterPatterns[1];
extern char const* radFilterPatterns[1];
extern char const* imgFilterPatterns[2];
extern char const* pngFilterPatterns[1];
extern char const* bmpFilterPatterns[1];

extern const char* g_optimize_tip;

void tooltip(const char* text, float hoverDelay = g_tooltip_delay);

class Gui {
	friend class Renderer;
	friend class MenuBar;
	friend class SettingsWidget;
	friend class FaceEditor;

public:
	Renderer* app;

	// state shared with vieport and ui components
	int hoveredOOB;
	bool lightmapEditorNeedsUpdate = true;
	bool entityReportFilterNeeded = true;
	bool entityReportReselectNeeded = false;
	bool reloadSettings = true;
	int guiHoverAxis; // axis being hovered in the transform menu
	int settingsTab = 0; // active tab in the setting widget
	bool loadedStats = false; // set to false to force limits widget to reload stats
	bool loadedLimit[SORT_MODES] = { false }; // set to false to force reload of a stat category
	bool badSurfaceExtents = false; // selected face has bad extents
	bool lightmapTooLarge = false; // selected face has too big of a lightmap
	bool anyHullValid[MAX_MAP_HULLS] = { false };

	Widget* widgets[NUM_WIDGET_IDS];

	ImFont* defaultFont;
	ImFont* consoleFont;

	Gui(Renderer* app);

	void init();
	void draw();

	// -1 for empty selection
	void openContextMenu(int entIdx);
	void copyTexture();
	void pasteTexture();
	void copyLightmap(int faceIdx, int layer);
	void pasteLightmap(int faceIdx, int layer);
	void refresh();
	const char* openMap();
	void windowResized(int width, int height);
	void showWidget(int id, bool showNotHide);

private:
	MenuBar* menuBar = NULL;

	bool vsync = true;
	bool polycount = false;
	bool openSavedTabs = false;
	bool shouldReloadFonts = false;

	bool shouldReloadTextureInfo = false;

	Texture* objectIconTexture;
	Texture* faceIconTexture;
	Texture* leafIconTexture;

	int contextMenuEnt = -1; // open entity context menu if >= 0
	int emptyContextMenu = 0; // open context menu for rightclicking world/void

	int copiedMiptex = -1;

	float uiScale;

	vector<Text2D> texts;
	vector<int> popupStack; // return to a popup after dismissing another

	void drawWidgets();
	void draw3dContextMenus();
	void drawStatusBar();
	void drawPopups();
	void drawToolbar();
	void drawStatusMessage();
	void drawWelcomePopup();
	void drawDebugText();
	void checkValidHulls();
	void reloadLimits();

	void loadFonts();
	void updateUiScale();
	string getUserLayoutPath(); // path to user's saved widget layout
	void addText(Text2D text);
	void switchToLeafSelectMode(bool selectFaceLeaves, bool strictFaceLeafSelection);
	void selectLeafPvs();
};