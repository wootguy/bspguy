#include "Bsp.h"
#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "gfx/ShaderProgram.h"
#include "gfx/BspRenderer.h"

class Renderer {
public:
	vector<BspRenderer*> mapRenderers;

	Renderer();
	~Renderer();

	void addMap(Bsp* map);

	void renderLoop();

	void imgui_demo();

private:
	GLFWwindow* window;
	ShaderProgram* bspShader;
	ShaderProgram* colorShader;

	vec3 cameraOrigin;
	vec3 cameraAngles;
	bool cameraIsRotating;
	float frameTimeScale = 0.0f;
	float moveSpeed = 4.0f;
	float fov, zNear, zFar;
	mat4x4 model, view, projection, modelView, modelViewProjection;

	vec2 lastMousePos;
	vec3 pickStart, pickDir, pickEnd;

	bool vsync;
	int renderFlags;

	vec3 getMoveDir();
	void cameraControls();
	void setupView();
	void getPickRay(vec3& start, vec3& pickDir);

	void drawLine(vec3 start, vec3 end, COLOR3 color);
	void drawGui();
};