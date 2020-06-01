#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include "Entity.h"

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

private:
	bool vsync;
	bool showDebugWidget;
	bool showKeyvalueWidget;
	bool showTransformWidget;
	bool smartEdit;
	ImFont* smallFont;
	ImFont* largeFont;

	int guiHoverAxis; // axis being hovered in the transform menu
	int contextMenuEnt; // open entity context menu if >= 0
	int emptyContextMenu; // open context menu for rightclicking world/void

	void draw3dContextMenus();
	void drawMenuBar();
	void drawFpsOverlay();
	void drawDebugWidget();
	void drawKeyvalueEditor();
	void drawKeyvalueEditor_SmartEditTab(Entity* ent);
	void drawKeyvalueEditor_FlagsTab(Entity* ent);
	void drawKeyvalueEditor_RawEditTab(Entity* ent);
	void drawTransformWidget();
};