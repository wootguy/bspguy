#pragma once
#include "Gui.h"
#include "Renderer.h"
#include "globals.h"
#include "Command.h"
#include "tinyfiledialogs.h"

class MenuBar {
public:
	float height = 0; // height of the menu bar

	MenuBar(Renderer* app, Gui* gui) : app(app), gui(gui) {}

	void draw();
	void drawEditOptions(bool isMainMenu);
	void saveAs();

private:
	Renderer* app;
	Gui* gui;
	bool editWasOpen = false;
	bool transparentClipnodes = true;

	void drawFileMenu();
	void drawEditMenu();
	void drawViewMenu();
	void drawSettingsMenu();
	void drawCreateMenu();
	void drawToolsMenu();
	void drawWidgetsMenu();
	void drawHelpMenu();

	
	void createSeriesWad();
};