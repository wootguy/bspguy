#include "Renderer.h"
#include "gfx/ShaderProgram.h"
#include "gfx/primitives.h"
#include "gfx/VertexBuffer.h"
#include "gfx/shaders.h"

void error_callback(int error, const char* description)
{
	printf("GLFW Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

Renderer::Renderer() {
	if (!glfwInit())
	{
		printf("GLFW initialization failed\n");
		return;
	}

	glfwSetErrorCallback(error_callback);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	window = glfwCreateWindow(640, 480, "bspguy", NULL, NULL);
	if (!window)
	{
		printf("Window creation failed\n");
		return;
	}

	glfwMakeContextCurrent(window);
	glfwSetKeyCallback(window, key_callback);

	glewInit();

	pipeline = new ShaderProgram(g_shader_cVert_vertex, g_shader_cVert_fragment);
	pipeline->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	pipeline->setMatrixNames(NULL, "modelViewProjection");
	pipeline->setVertexAttributeNames("vPosition", "vColor", NULL);
	pipeline->bind();
}

Renderer::~Renderer() {
	glfwTerminate();
}

void Renderer::renderLoop() {
	glfwSwapInterval(1);

	cCube cube(vec3(-10, -10, -10), vec3(10, 10, 10), {0, 255, 0} );
	cube.setColor({ 0,255,0 }, { 0,0,255 }, { 255,0,0 }, { 0,255,255 }, { 255,0,255 }, { 0,255,0 });
	
	VertexBuffer buffer(pipeline, COLOR_3B | POS_3F, &cube, 6*6);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	cameraOrigin.y = -50;

	float lastFrameTime = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		float frameDelta = glfwGetTime() - lastFrameTime;
		frameTimeScale = 0.002f / frameDelta;
		lastFrameTime = glfwGetTime();

		cameraControls();

		float spin = glfwGetTime() * 2;
		model.loadIdentity();
		model.rotateZ(spin);
		model.rotateX(spin);
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		setupView();
		pipeline->updateMatrixes();

		buffer.draw(GL_TRIANGLES);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
}

void Renderer::cameraControls() {
	cameraOrigin += getMoveDir() * frameTimeScale;

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	vec2 mousePos(xpos, ypos);

	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
		if (!cameraIsRotating) {
			lastMousePos = mousePos;
			cameraIsRotating = true;
		}
		else {
			vec2 drag = mousePos - lastMousePos;
			cameraAngles.z += drag.x * 0.5f;
			cameraAngles.x += drag.y * 0.5f;
			lastMousePos = mousePos;
		}
	}
	else {
		cameraIsRotating = false;
	}
}

vec3 Renderer::getMoveDir()
{
	vec3 wishdir(0, 0, 0);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
	{
		wishdir.x += (float)-cos(PI * (cameraAngles.z) / 180.0f);
		wishdir.y += (float)sin(PI * (cameraAngles.z) / 180.0f);
	}
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
	{
		wishdir.x -= (float)-cos(PI * (cameraAngles.z) / 180.0f);
		wishdir.y -= (float)sin(PI * (cameraAngles.z) / 180.0f);
	}

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
	{
		wishdir.x += (float)-cos(PI * (cameraAngles.z + 90) / 180.0f);
		wishdir.y += (float)sin(PI * (cameraAngles.z + 90) / 180.0f);
	}
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
	{
		wishdir.x -= (float)-cos(PI * (cameraAngles.z + 90) / 180.0f);
		wishdir.y -= (float)sin(PI * (cameraAngles.z + 90) / 180.0f);
	}

	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		wishdir.z += 1;
	if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		wishdir.z -= 1;
	return wishdir;
}

void Renderer::setupView() {
	float fov = 75.0f;
	int width, height;

	glfwGetFramebufferSize(window, &width, &height);

	glViewport(0, 0, width, height);

	projection.perspective(fov, (float)width / (float)height, 1.0f, 4096.0f);

	view.loadIdentity();
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}

void Renderer::addMap(Bsp* map) {
	
}

void Renderer::renderMap(Bsp* bsp) {

}
