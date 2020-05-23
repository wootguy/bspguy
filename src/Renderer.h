#include "Bsp.h"
#include <GL/glew.h>
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

private:
	GLFWwindow* window;
	ShaderProgram* pipeline;

	vec3 cameraOrigin;
	vec3 cameraAngles;
	bool cameraIsRotating;
	float frameTimeScale = 0.0f;
	mat4x4 model, view, projection, modelView, modelViewProjection;

	vec2 lastMousePos;

	vec3 getMoveDir();
	void cameraControls();
	void setupView();
};