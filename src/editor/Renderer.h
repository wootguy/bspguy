#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include "ShaderProgram.h"
#include "BspRenderer.h"
#include "Fgd.h"
#include <thread>
#include <future>
#include "Command.h"

class Gui;

enum transform_modes {
	TRANSFORM_NONE = -1,
	TRANSFORM_MOVE,
	TRANSFORM_SCALE
};

enum transform_targets {
	TRANSFORM_OBJECT,
	TRANSFORM_VERTEX,
	TRANSFORM_ORIGIN
};

enum pick_modes {
	PICK_OBJECT,
	PICK_FACE
};

struct TransformAxes {
	cCube* model;
	VertexBuffer* buffer;
	vec3 origin;
	vec3 mins[6];
	vec3 maxs[6];
	COLOR4 dimColor[6];
	COLOR4 hoverColor[6];
	int numAxes;
};

struct AppSettings {
	int windowWidth = 800;
	int windowHeight = 600;
	int windowX = 0;
	int windowY = 0;
	int maximized = 0;
	int fontSize = 22;
	string gamedir;
	bool valid = false;
	int undoLevels = 64;
	bool verboseLogs = false;

	bool debug_open = false;
	bool keyvalue_open = false;
	bool transform_open = false;
	bool log_open = false;
	bool settings_open = false;
	bool limits_open = false;
	bool entreport_open = false;
	int settings_tab = 0;

	float fov;
	float zfar;
	float moveSpeed;
	float rotSpeed;
	int render_flags;
	bool vsync;
	bool show_transform_axes;

	vector<string> fgdPaths;
	vector<string> resPaths;

	void load();
	void save();
};

class Renderer;

extern AppSettings g_settings;
extern Renderer* g_app;

class Renderer {
	friend class Gui;
	friend class EditEntityCommand;
	friend class DeleteEntityCommand;
	friend class CreateEntityCommand;
	friend class DuplicateBspModelCommand;
	friend class CreateBspModelCommand;
	friend class EditBspModelCommand;
	friend class CleanMapCommand;
	friend class OptimizeMapCommand;

public:
	vector<BspRenderer*> mapRenderers;

	vec3 debugPoint;
	vec3 debugVec0;
	vec3 debugVec1;
	vec3 debugVec2;
	vec3 debugVec3;

	bool hideGui = false;

	Renderer();
	~Renderer();

	void addMap(Bsp* map);

	void renderLoop();
	void reloadFgdsAndTextures();
	void reloadMaps();
	void saveSettings();
	void loadSettings();

private:
	GLFWwindow* window;
	ShaderProgram* bspShader;
	ShaderProgram* fullBrightBspShader;
	ShaderProgram* colorShader;
	PointEntRenderer* pointEntRenderer;
	PointEntRenderer* swapPointEntRenderer = NULL;
	Gui* gui;

	static future<void> fgdFuture;
	bool reloading = false;
	bool reloadingGameDir = false;
	bool isLoading = false;

	Fgd* fgd = NULL;

	vec3 cameraOrigin;
	vec3 cameraAngles;
	vec3 cameraForward;
	vec3 cameraUp;
	vec3 cameraRight;
	bool cameraIsRotating;
	float frameTimeScale = 0.0f;
	float moveSpeed = 4.0f;
	float fov = 75.0f;
	float zNear = 1.0f;
	float zFar = 262144.0f;
	float rotationSpeed = 5.0f;
	int windowWidth;
	int windowHeight;
	mat4x4 model, view, projection, modelView, modelViewProjection;

	vec2 lastMousePos;
	vec2 totalMouseDrag;

	bool movingEnt = false; // grab an ent and move it with the camera
	vec3 grabStartOrigin;
	vec3 grabStartEntOrigin;
	float grabDist;

	TransformAxes moveAxes;
	TransformAxes scaleAxes;
	int hoverAxis; // axis being hovered
	int draggingAxis = -1; // axis currently being dragged by the mouse
	bool gridSnappingEnabled = true;
	int gridSnapLevel = 0;
	int transformMode = TRANSFORM_MOVE;
	int transformTarget = TRANSFORM_OBJECT;
	int pickMode = PICK_OBJECT;
	bool showDragAxes = false;
	bool pickClickHeld = true; // true if the mouse button is still held after picking an object
	vec3 axisDragStart;
	vec3 axisDragEntOriginStart;
	vector<ScalableTexinfo> scaleTexinfos; // texture coordinates to scale
	bool textureLock = false;
	bool invalidSolid = false;
	bool isTransformableSolid = true;
	bool canTransform = false;
	bool anyEdgeSelected = false;
	bool anyVertSelected = false;

	vector<int> selectedFaces;
	int selectMapIdx = -1;

	vector<TransformVert> modelVerts; // control points for invisible plane intersection verts in HULL 0
	vector<TransformVert> modelFaceVerts; // control points for visible face verts
	vector<HullEdge> modelEdges;
	cCube* modelVertCubes = NULL;
	cCube modelOriginCube;
	VertexBuffer* modelVertBuff = NULL;
	VertexBuffer* modelOriginBuff = NULL;
	bool originSelected = false;
	bool originHovered = false;
	vec3 oldOrigin;
	vec3 transformedOrigin;
	int hoverVert = -1;
	int hoverEdge = -1;
	float vertExtentFactor = 0.01f;
	bool modelUsesSharedStructures = false;
	vec3 selectionSize;

	VertexBuffer* entConnections = NULL;
	VertexBuffer* entConnectionPoints = NULL;

	Entity* copiedEnt = NULL;

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
	int vertPickCount = 0;

	int debugInt = 0;
	int debugIntMax = 0;
	int debugNode = 0;
	int debugNodeMax = 0;
	bool debugClipnodes = false;
	bool debugNodes = false;
	int clipnodeRenderHull = -1;

	int undoLevels = 64;
	int undoMemoryUsage = 0; // approximate space used by undo+redo history
	vector<Command*> undoHistory;
	vector<Command*> redoHistory;
	Entity* undoEntityState = NULL;
	LumpState undoLumpState;
	vec3 undoEntOrigin;

	vec3 getMoveDir();
	void controls();
	void cameraPickingControls();
	void vertexEditControls();
	void cameraRotationControls(vec2 mousePos);
	void cameraObjectHovering();
	void cameraContextMenus(); // right clicking on ents and things
	void moveGrabbedEnt(); // translates the grabbed ent
	void shortcutControls(); // hotkeys for menus and things
	void globalShortcutControls(); // these work even with the UI selected
	void pickObject(); // select stuff with the mouse
	bool transformAxisControls(); // true if grabbing axes
	void applyTransform(bool forceUpdate=false);
	void setupView();
	void getPickRay(vec3& start, vec3& pickDir);
	BspRenderer* getMapContainingCamera();

	void drawModelVerts();
	void drawModelOrigin();
	void drawTransformAxes();
	void drawEntConnections();
	void drawLine(vec3 start, vec3 end, COLOR4 color);
	void drawPlane(BSPPLANE& plane, COLOR4 color);
	void drawClipnodes(Bsp* map, int iNode, int& currentPlane, int activePlane);
	void drawNodes(Bsp* map, int iNode, int& currentPlane, int activePlane);

	vec3 getEntOrigin(Bsp* map, Entity* ent);
	vec3 getEntOffset(Bsp* map, Entity* ent);

	vec3 getAxisDragPoint(vec3 origin);

	void updateDragAxes();
	void updateModelVerts();
	void updateSelectionSize();
	void updateEntConnections();
	void updateEntConnectionPositions(); // only updates positions in the buffer
	bool getModelSolid(vector<TransformVert>& hullVerts, Bsp* map, Solid& outSolid); // calculate face vertices from plane intersections
	void moveSelectedVerts(vec3 delta);
	void splitFace();

	vec3 snapToGrid(vec3 pos);

	void grabEnt();
	void cutEnt();
	void copyEnt();
	void pasteEnt(bool noModifyOrigin);
	void deleteEnt();
	void scaleSelectedObject(float x, float y, float z);
	void scaleSelectedObject(vec3 dir, vec3 fromDir);
	void scaleSelectedVerts(float x, float y, float z);
	vec3 getEdgeControlPoint(vector<TransformVert>& hullVerts, HullEdge& iEdge);
	vec3 getCentroid(vector<TransformVert>& hullVerts);
	void deselectObject(); // keep map selected but unselect all objects
	void deselectFaces();
	void selectEnt(Bsp* map, int entIdx);
	void goToEnt(Bsp* map, int entIdx);
	void ungrabEnt();

	void pushEntityUndoState(string actionDesc);
	void pushModelUndoState(string actionDesc, int targetLumps);
	void pushUndoCommand(Command* cmd);
	void undo();
	void redo();
	void clearUndoCommands();
	void clearRedoCommands();
	void calcUndoMemoryUsage();

	void updateEntityState(Entity* ent);
	void saveLumpState(Bsp* map, int targetLumps, bool deleteOldState);

	void loadFgds();
};