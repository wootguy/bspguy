#include "Bsp.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "gfx/ShaderProgram.h"

class Renderer {
public:
	vector<Bsp*> maps;

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

	void renderMap(Bsp* bsp);

	vec3 getMoveDir();
	void cameraControls();
	void setupView();
};