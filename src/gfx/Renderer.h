#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include "ShaderProgram.h"
#include "BspRenderer.h"
#include "Fgd.h"

class Gui;

enum transform_modes {
	TRANSFORM_MOVE,
	TRANSFORM_SCALE
};

struct TransformAxes {
	cCube* model;
	VertexBuffer* buffer;
	vec3 origin;
	vec3 mins[6];
	vec3 maxs[6];
	COLOR3 dimColor[6];
	COLOR3 hoverColor[6];
	int numAxes;
};

class Renderer {
	friend class Gui;

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
	Gui* gui;

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
	int windowWidth;
	int windowHeight;
	mat4x4 model, view, projection, modelView, modelViewProjection;

	vec2 lastMousePos;
	vec2 totalMouseDrag;

	bool movingEnt; // grab an ent and move it with the camera
	vec3 grabStartOrigin;
	vec3 gragStartEntOrigin;
	float grabDist;

	TransformAxes moveAxes;
	TransformAxes scaleAxes;
	int hoverAxis; // axis being hovered
	int draggingAxis; // axis currently being dragged by the mouse
	bool gridSnappingEnabled;
	int gridSnapLevel;
	int transformMode;
	bool showDragAxes;
	vec3 axisDragStart;
	vec3 axisDragEntOriginStart;
	vec3* scaleVertsStart; // original positions of the verts being scaled
	vec3** scaleVerts; // pointers to verts in the BSP data for scaling
	vector<ScalablePlane> scalePlanes; // verts used to recalculate plane normals/origins after scaling
	vector<ScalableTexinfo> scaleTexinfos; // texture coordinates to scale
	float* scaleVertDists;
	int numScaleVerts;
	bool textureLock;

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
	vec3 debugVec0;
	vec3 debugVec1;
	vec3 debugVec2;
	vec3 debugVec3;

	vec3 getMoveDir();
	void controls();
	void cameraRotationControls(vec2 mousePos);
	void cameraObjectHovering();
	void cameraContextMenus(); // right clicking on ents and things
	void moveGrabbedEnt(); // translates the grabbed ent
	void shortcutControls(); // hotkeys for menus and things
	void pickObject(); // select stuff with the mouse
	bool transformAxisControls(); // true if grabbing axes
	void setupView();
	void getPickRay(vec3& start, vec3& pickDir);
	BspRenderer* getMapContainingCamera();

	void drawTransformAxes();
	void drawLine(vec3 start, vec3 end, COLOR3 color);

	vec3 getEntOrigin(Bsp* map, Entity* ent);
	vec3 getEntOffset(Bsp* map, Entity* ent);

	vec3 getAxisDragPoint(vec3 origin);

	void updateDragAxes();
	void updateScaleVerts(bool currentlyScaling);

	vec3 snapToGrid(vec3 pos);

	void grabEnt();
	void cutEnt();
	void copyEnt();
	void pasteEnt(bool noModifyOrigin);
	void deleteEnt();
	void scaleSelectedVerts(vec3 dir, vec3 fromDir);
};