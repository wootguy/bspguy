#include "Bsp.h"
#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include "ShaderProgram.h"
#include "BspRenderer.h"
#include "Fgd.h"

class Renderer {
public:
	vector<BspRenderer*> mapRenderers;

	Renderer();
	~Renderer();

	void addMap(Bsp* map);

	void renderLoop();

private:
	GLFWwindow* window;
	ShaderProgram* bspShader;
	ShaderProgram* colorShader;
	PointEntRenderer* pointEntRenderer;

	Fgd* fgd;

	vec3 cameraOrigin;
	vec3 cameraAngles;
	vec3 cameraForward;
	vec3 cameraUp;
	vec3 cameraRight;
	bool cameraIsRotating;
	float frameTimeScale = 0.0f;
	float moveSpeed = 4.0f;
	float fov, zNear, zFar;
	mat4x4 model, view, projection, modelView, modelViewProjection;

	vec2 lastMousePos;
	vec2 totalMouseDrag;

	int windowWidth;
	int windowHeight;

	bool vsync;
	bool showDebugWidget;
	bool showKeyvalueWidget;
	bool showTransformWidget;
	bool smartEdit;
	ImFont* smallFont;
	ImFont* largeFont;

	int contextMenuEnt; // open entity context menu if >= 0
	int emptyContextMenu; // open context menu for rightclicking world/void
	bool movingEnt; // grab an ent and move it with the camera
	vec3 grabStartOrigin;
	vec3 gragStartEntOrigin;
	float grabDist;

	cCube* axisModel;
	vec3 axisModelMins[4];
	vec3 axisModelMaxs[4];
	COLOR3 axisDimColors[4];
	COLOR3 axisHoverColors[4];
	VertexBuffer* axisBuffer;
	int hoverAxis; // axis being hovered
	int guiHoverAxis; // axis being hovered in the transform menu
	int draggingAxis; // axis currently being dragged by the mouse
	bool gridSnappingEnabled;
	int gridSnapLevel;
	bool showDragAxes;
	vec3 axisDragStart;
	vec3 axisDragEntOriginStart;

	Entity* copiedEnt;

	int oldLeftMouse;
	int oldRightMouse;
	int oldScroll;
	bool pressed[GLFW_KEY_LAST];
	bool released[GLFW_KEY_LAST];
	char oldPressed[GLFW_KEY_LAST];
	char oldReleased[GLFW_KEY_LAST];
	bool anyCtrlPressed;
	bool anyAltPressed;
	bool anyShiftPressed;

	PickInfo pickInfo;
	int pickCount = 0; // used to give unique IDs to text inputs so switching ents doesn't update keys accidentally

	vec3 debugPoint;

	vec3 getMoveDir();
	void cameraControls();
	void setupView();
	void getPickRay(vec3& start, vec3& pickDir);
	BspRenderer* getMapContainingCamera();

	void drawLine(vec3 start, vec3 end, COLOR3 color);

	void drawGui();
	void draw3dContextMenus();
	void drawMenuBar();
	void drawFpsOverlay();
	void drawDebugWidget();
	void drawKeyvalueEditor();
	void drawKeyvalueEditor_SmartEditTab(Entity* ent);
	void drawKeyvalueEditor_FlagsTab(Entity* ent);
	void drawKeyvalueEditor_RawEditTab(Entity* ent);
	void drawTransformWidget();

	vec3 getEntOrigin(Bsp* map, Entity* ent);
	vec3 getEntOffset(Bsp* map, Entity* ent);

	vec3 getAxisDragPoint(vec3 origin);

	void updateDragAxes();

	vec3 snapToGrid(vec3 pos);

	void grabEnt();
	void cutEnt();
	void copyEnt();
	void pasteEnt(bool noModifyOrigin);
	void deleteEnt();
};