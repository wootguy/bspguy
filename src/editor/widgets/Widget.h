#pragma once
#include "Gui.h"
#include "Renderer.h"
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
	int widgetFlags;

	// popup state, do not touch
	bool isPopup = false;
	bool shouldReturnToThisPopup = false;
	bool popupWasOpen = false; // true if the popup was open last frame

	// for convenience
	Renderer* app = NULL;
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

	void tooltip(const char* text, float hoverDelay=g_tooltip_delay);

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
