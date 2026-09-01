#pragma once
#include "Gui.h"
#include "Editor.h"
#include "globals.h"
#include "Command.h"
#include "tinyfiledialogs.h"

class Widget {
public:
	bool widgetVisible = false; // should this widget be drawn. Set to false in popups to close the popup.
	bool widgetIsOpen = false; // is this widget expanded
	bool allowInMapArrangeMode = false; // allow using this widget when arranging maps for a merge
	const char* widgetName; // imgui child id
	ImVec2 widgetSizeDefault; // default size for the widget (for first use)
	ImVec2 widgetSizeMin; // minimum size for the widget
	ImVec2 lastPosition; // last position the window drew at
	int widgetFlags;

	// popup state, do not touch
	bool isPopup = false;
	bool shouldReturnToThisPopup = false;
	bool popupWasOpen = false; // true if the popup was open last frame
	bool shouldResetPosition = false;

	// for convenience
	Editor* app = NULL;
	Bsp* map = NULL;
	ImGuiStyle& style;
	ImGuiContext& g;
	ImGuiIO& io;
	Gui* gui;
	float uiScale = 1;

	Widget(Gui* gui, const char* widgetName, ImVec2 widgetSizeDefault, ImVec2 widgetSizeMin, int widgetFlags)
		: widgetName(widgetName), widgetSizeDefault(widgetSizeDefault), widgetSizeMin(widgetSizeMin),
		widgetFlags(widgetFlags), app(g_app), gui(gui), style(ImGui::GetStyle()), g(*GImGui), io(ImGui::GetIO()) {
	}

	virtual void open() {} // called when a popup is about to open (first render frame)

	virtual void setup() {} // do any extra window setup before drawing

	virtual void draw() = 0;

	// called by you. Set pushStack to reopen this widget after the next one is dismissed
	virtual void close(bool pushStack = false) { widgetVisible = false; shouldReturnToThisPopup = pushStack; }

	virtual int calcMemoryUsage() { return sizeof(Widget); }
};

class Popup : public Widget {
public:
	Popup(Gui* gui, const char* widgetName, ImVec2 widgetSizeDefault, ImVec2 widgetSizeMin, int widgetFlags)
		: Widget(gui, widgetName, widgetSizeDefault, widgetSizeMin, widgetFlags) {
		isPopup = true;
	}
};

class DebugWidget : public Widget {
	using Widget::Widget;
	void setup() override;
	void draw() override;
	
	void drawSelectionDetails();
};

class KeyvalueEditor : public Widget {
	using Widget::Widget;
	void draw() override;

	unordered_map<string, string> copiedKeyvalues;

	void drawSmartEditTab_GroupKeys(vector<KeyvalueDef*>& keys, bool isGrouped, int keyOffset);
	void drawSmartEditTab(Fgd* fgd);
	void drawFlagsTab(Fgd* fgd);
	void drawRawEditTab();
};

class TransformWidget : public Widget {
	using Widget::Widget;
	void draw() override;
};

class LogWidget : public Widget {
	using Widget::Widget;
	void draw() override;

	ImGuiTextBuffer Buf = ImGuiTextBuffer();
	ImVector<int> LineOffsets; // Index to lines offset. We maintain this with AddLog() calls, allowing us to have a random access on lines
	ImVector<ImVec4> LineColors;
	bool AutoScroll = true;  // Keep scrolling if already at the bottom

	void clearLog();
	void addLog(LogEntry& entry);

	int calcMemoryUsage();
};

class SettingsWidget : public Widget {
	using Widget::Widget;
	void setup() override;
	void draw() override;
};

class HelpWidget : public Widget {
	using Widget::Widget;
	void draw() override;
};

class AboutWidget : public Widget {
	using Widget::Widget;
	void draw() override;
};

class FaceEditor : public Widget {
public:
	bool refreshAfterFacePaste = false;

	void checkFaceErrors();
	void clearTextureBrowserCache(); // call when wads change

private:
	int copiedLightmapFace = -1; // index into faces
	int copiedLightmapLayer = 0; // index into styles

	uint16_t resizeWidth = 0;
	uint16_t resizeHeight = 0;
	uint16_t resizeOriginalWidth = 0;
	uint16_t resizeOriginalHeight = 0;
	int resizeTextureIdx = 0;
	bool resizeMasked = false;
	COLOR3 resizeMaskColor;

	float scaleX = 0, scaleY = 0, shiftX = 0, shiftY = 0, rotate = 0;
	bool isSpecial = false;
	int width = 0, height = 0;
	ImTextureID textureId = 0; // OpenGL ID
	Texture* buttonTexture = NULL;
	int lastTextureIdx = 0;
	char textureName[MAXTEXTURENAME];
	int lastPickCount = -1;
	bool validTexture = true;
	bool isEmbedded = false;
	string texture_src;
	string last_texture_name;
	int tex_size_kb = 0;

	bool texture_browser_open = false;
	int filterWad = -2;
	bool onlyUsedTextures = false;
	char texNameFilter[MAXTEXTURENAME];
	bool pickedBrowserTexture = false;
	int scrollToTexIdx;

	struct BrowserTexture {
		string name;
		string lowerName; // for sorting/filtering
		string contextId; // for context menus
		WADTEX wadTex; // embedded if this is null
		int width, height;
		bool usedInMap;
		int source; // -1 = bsp, 0+ = wad index
		ImVec2 textSize;
		int iMiptex; // -1 if not used in map
		Texture* tex;
	};
	vector<BrowserTexture> browserTextures;
	vector<int> filteredTextures;

	using Widget::Widget;
	void draw() override;
	void drawTextureEditor();
	void drawEmbedCheckbox();
	void drawTextureButton();
	void drawResizePopup();
	void drawTextureBrowserPopup();
	void drawTextureBrowserGrid();
	void drawLightmapsEditor();

	void updateTextureSelection();
	void openTextureBrowser();
	void loadBrowserTextures();
	void filterTextureBrowser();

	void copyLightmap(int faceIdx, int layer);
	void pasteLightmap(int faceIdx, int layer);
};

struct ReportEnt {
	int idx;
	bool selected;
	bool hasFgd;
	float scrollY;
	string cname;
};
class EntityReport : public Widget {
	using Widget::Widget;
	void setup() override;
	void draw() override;

	string title;
	vector<ReportEnt> filteredEnts;
};

class QuadTree;

class LeafWidget : public Widget {
public:
	using Widget::Widget;
	void draw() override;

	void selectLeaves(vector<int>& leaves);

	bool needsRefresh = true;

private:
	void refresh();

	vector<GraphNode> gnodes;
	unordered_map<uint64_t, int> nodeIdxToGnode;
	unordered_set<uint64_t> hotPath;
	unordered_set<int> mergedLeaves;
	
	struct NodeEdge {
		vec2 start, end;
		int endNode;
	};

	QuadTree* nodeTree = NULL;
	QuadTree* edgeTree = NULL;
	vector<NodeEdge> edges;

	float graphWidth, graphHeight;
};

class RadWidget : public Popup {
public:
	bool refreshTexlightList = false;
	unordered_map<string, string> texlights;

	using Popup::Popup;
	void open() override;
	void setup() override;
	void draw() override;
};

class DedupModelsWidget : public Popup {
	using Popup::Popup;
	void draw() override;
};

class MergeOverlapWidget : public Popup {
	using Popup::Popup;
	void draw() override;
};

class MergeFailedWidget : public Popup {
	using Popup::Popup;
	void open() override;
	void draw() override;
};

class MergeMultipleWidget : public Popup {
	using Popup::Popup;
	void draw() override;
};

class FixExtentsWidget : public Popup {
	using Popup::Popup;
	void draw() override;
};

class ModelMergeWidget : public Popup {
	using Popup::Popup;
	void setup() override;
	void draw() override;
};
