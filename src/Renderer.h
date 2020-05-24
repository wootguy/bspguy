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
	ShaderProgram* bspShader;
	ShaderProgram* colorShader;

	vec3 cameraOrigin;
	vec3 cameraAngles;
	bool cameraIsRotating;
	float frameTimeScale = 0.0f;
	float fov, zNear, zFar;
	mat4x4 model, view, projection, modelView, modelViewProjection;

	vec2 lastMousePos;

	vec3 pickStart, pickDir, pickEnd;

	vec3 getMoveDir();
	void cameraControls();
	void setupView();
	void getPickRay(vec3& start, vec3& pickDir);

	void drawLine(vec3 start, vec3 end, COLOR3 color);
};