#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "primitives.h"
#include "BspRenderer.h"
#include "bsptypes.h"
#include "BspMerger.h"

class Gui;
class Fgd;
class Command;
class BspRenderer;
class VertexBuffer;
class ShaderProgram;
class PointEntRenderer;
class Entity;
class Bsp;
class LeafNavMesh;

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

struct EntConnection {
	Entity* self;
	Entity* target;
	COLOR4 color;
};

class Renderer;

class Renderer {
	friend class Gui;
	friend class EditEntitiesCommand;
	friend class DeleteEntitiesCommand;
	friend class CreateEntitiesCommand;
	friend class CreateEntityFromTextCommand;
	friend class DuplicateBspModelCommand;
	friend class CreateBspModelCommand;
	friend class EditBspModelCommand;
	friend class CleanMapCommand;
	friend class OptimizeMapCommand;
	friend class DeleteBoxedDataCommand;
	friend class DeleteOobDataCommand;
	friend class FixSurfaceExtentsCommand;
	friend class DeduplicateModelsCommand;
	friend class MoveMapCommand;
	friend class LeafNavMesh;

public:
	BspRenderer* mapRenderer;

	vec3 debugPoint;
	vec3 debugVec0;
	vec3 debugVec1;
	vec3 debugVec2;
	vec3 debugVec3;

	int debugInt = 0;
	int debugIntMax = 0;
	int debugNode = 0;
	int debugNodeMax = 0;
	bool debugClipnodes = false;
	bool debugNodes = false;
	int clipnodeRenderHull = -1;

	vec3 debugLine0;
	vec3 debugLine1;
	Line2D debugCut;
	Polygon3D debugPoly;
	Polygon3D debugPoly2;
	NavMesh* debugNavMesh = NULL;
	LeafNavMesh* debugLeafNavMesh = NULL;
	int debugNavPoly = -1;
	vec3 debugTraceStart;
	TraceResult debugTrace;
	MergeResult mergeResult;

	bool hideGui = false;
	bool isFocused = false;
	bool isHovered = false;
	bool isIconified = false;

	Renderer();
	~Renderer();

	void addMap(Bsp* map);

	void renderLoop();
	void postLoadFgdsAndTextures();
	void postLoadFgds();
	void reloadMaps();
	void openMap(const char* path);
	void saveSettings();
	void loadSettings();
	void merge(string fpath);

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

	Fgd* mergedFgd = NULL; // merged FGD
	vector<Fgd*> fgds; // individually loaded FGDs

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
	mat4x4 model = mat4x4(), view = mat4x4(), projection = mat4x4(), modelView = mat4x4(), modelViewProjection = mat4x4();

	vec2 lastMousePos;
	vec2 totalMouseDrag;

	bool movingEnt = false; // grab an ent and move it with the camera
	vec3 grabStartOrigin;
	vector<vec3> grabStartEntOrigin;
	float grabDist;

	TransformAxes moveAxes = TransformAxes();
	TransformAxes scaleAxes = TransformAxes();
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
	vector<vec3> axisDragEntOriginStart; // starting positions for entities being dragged by visual axes
	vector<ScalableTexinfo> scaleTexinfos; // texture coordinates to scale
	bool textureLock = false;
	bool invalidSolid = false;
	bool isTransformableSolid = true;
	bool canTransform = false;
	bool anyEdgeSelected = false;
	bool anyVertSelected = false;

	vector<TransformVert> modelVerts; // control points for invisible plane intersection verts in HULL 0
	vector<TransformVert> modelFaceVerts; // control points for visible face verts
	vector<HullEdge> modelEdges;
	cCube* modelVertCubes = NULL;
	cCube modelOriginCube = cCube();
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
	vector<EntConnection> entConnectionLinks;

	vector<Entity*> copiedEnts;

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

	PickInfo pickInfo = PickInfo();
	int pickCount = 0; // used to give unique IDs to text inputs so switching ents doesn't update keys accidentally
	int vertPickCount = 0;

	int undoLevels = 64;
	int undoMemoryUsage = 0; // approximate space used by undo+redo history
	vector<Command*> undoHistory;
	vector<Command*> redoHistory;
	vector<EntityState> undoEntityState;
	LumpState undoLumpState = LumpState();
	vec3 undoEntOrigin;

	bool hasCullbox;
	vec3 cullMins;
	vec3 cullMaxs;

	vec3 getMoveDir();
	void controls();
	void cameraPickingControls();
	void vertexEditControls();
	void cameraRotationControls(vec2 mousePos);
	void cameraObjectHovering();
	void cameraContextMenus(); // right clicking on ents and things
	void moveGrabbedEnts(); // translates the grabbed ent
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
	void drawLine2D(vec2 start, vec2 end, COLOR4 color);
	void drawBox(vec3 center, float width, COLOR4 color);
	void drawBox(vec3 mins, vec3 maxs, COLOR4 color);
	void drawPolygon3D(Polygon3D& poly, COLOR4 color);
	float drawPolygon2D(Polygon3D poly, vec2 pos, vec2 maxSz, COLOR4 color); // returns render scale
	void drawBox2D(vec2 center, float width, COLOR4 color);
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

	void updateCullBox();

	vec3 snapToGrid(vec3 pos);

	void grabEnts();
	void cutEnts();
	void copyEnts();
	void pasteEnts(bool noModifyOrigin);
	void pasteEntsFromText(string text);
	void deleteEnts();
	void scaleSelectedObject(float x, float y, float z);
	void scaleSelectedObject(vec3 dir, vec3 fromDir);
	void scaleSelectedVerts(float x, float y, float z);
	vec3 getEdgeControlPoint(vector<TransformVert>& hullVerts, HullEdge& iEdge);
	vec3 getCentroid(vector<TransformVert>& hullVerts);
	void deselectObject(); // keep map selected but unselect all objects
	void deselectFaces();
	void postSelectEnt(); // react to selecting new entities 
	void goToEnt(Bsp* map, int entIdx);
	void goToCoords(float x, float y, float z);
	void goToFace(Bsp* map, int faceIdx);
	void ungrabEnts();

	bool canPushEntityUndoState();
	void pushEntityUndoState(string actionDesc);
	void pushModelUndoState(string actionDesc, int targetLumps);
	void pushUndoCommand(Command* cmd);
	void undo();
	void redo();
	void clearUndoCommands();
	void clearRedoCommands();
	void calcUndoMemoryUsage();

	void updateEntityUndoState();
	void saveLumpState(Bsp* map, int targetLumps, bool deleteOldState);
	void updateEntityLumpUndoState(Bsp* map);

	void loadFgds();
};