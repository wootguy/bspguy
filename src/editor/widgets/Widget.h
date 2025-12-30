#pragma once
#include "Gui.h"
#include "Renderer.h"
#include "globals.h"
#include "Command.h"
#include "tinyfiledialogs.h"

class Widget {
public:
	bool widgetVisible = false; // should this widget be drawn
	bool widgetIsOpen = false; // is this widget expanded
	bool allowInMapArrangeMode = false; // allow using this widget when arranging maps for a merge
	const char* widgetName; // imgui child id
	ImVec2 widgetSizeDefault; // default size for the widget (for first use)
	ImVec2 widgetSizeMin; // minimum size for the widget
	int widgetFlags;

	// for convenience
	Renderer* app = NULL;
	Bsp* map = NULL;
	ImGuiStyle& style;
	ImGuiContext& g;
	ImGuiIO& io;
	Gui* gui;
	float uiScale;

	Widget(Gui* gui, const char* widgetName, ImVec2 widgetSizeDefault, ImVec2 widgetSizeMin, int widgetFlags)
		: widgetName(widgetName), widgetSizeDefault(widgetSizeDefault), widgetSizeMin(widgetSizeMin),
		widgetFlags(widgetFlags), app(g_app), gui(gui), style(ImGui::GetStyle()), g(*GImGui), io(ImGui::GetIO()) {
	}

	virtual void setup() {} // do any extra window setup before drawing

	virtual void draw() = 0;

	void tooltip(const char* text, float hoverDelay=g_tooltip_delay);
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
	bool AutoScroll = true;  // Keep scrolling if already at the bottom

	void clearLog();
	void addLog(const char* s);
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

private:
	int copiedLightmapFace = -1; // index into faces
	int copiedLightmapLayer = 0; // index into styles

	using Widget::Widget;
	void draw() override;

	void drawTextureEditor();
	void drawLightmapsEditor();

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
