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

	window = glfwCreateWindow(1920, 1080, "bspguy", NULL, NULL);
	if (!window)
	{
		printf("Window creation failed\n");
		return;
	}

	glfwMakeContextCurrent(window);
	glfwSetKeyCallback(window, key_callback);

	glewInit();

	pipeline = new ShaderProgram(g_shader_multitexture_vertex, g_shader_multitexture_fragment);
	pipeline->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	pipeline->setMatrixNames(NULL, "modelViewProjection");
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
	glCullFace(GL_FRONT);

	cameraOrigin.y = -50;

	float frameTimes[8] = { 0 };
	int frameTimeIdx = 0;

	float lastFrameTime = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		float frameDelta = glfwGetTime() - lastFrameTime;
		frameTimeScale = 0.05f / frameDelta;
		lastFrameTime = glfwGetTime();

		frameTimes[frameTimeIdx++ % 8] = frameDelta;

		float avg = 0;
		for (int i = 0; i < 8; i++) {
			avg += frameTimes[i];
		}
		avg /= 8;
		float fps = 1.0f / avg;
		if (frameTimeIdx % 20 == 0) {
			printf("FPS: %.2f\n", fps);
		}


		cameraControls();

		float spin = glfwGetTime() * 2;
		model.loadIdentity();
		model.rotateZ(spin);
		model.rotateX(spin);
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		setupView();
		pipeline->updateMatrixes();
		//buffer.draw(GL_TRIANGLES);

		for (int i = 0; i < mapRenderers.size(); i++) {
			model.loadIdentity();
			pipeline->updateMatrixes();
			mapRenderers[i]->render();
		}

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
	mat4x4 rotMat;
	rotMat.loadIdentity();
	rotMat.rotateX(PI * cameraAngles.x / 180.0f);
	rotMat.rotateZ(PI * cameraAngles.z / 180.0f);

	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);


	vec3 wishdir(0, 0, 0);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
	{
		wishdir -= right;
	}
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
	{
		wishdir += right;
	}
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
	{
		wishdir += forward;
	}
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
	{
		wishdir -= forward;
	}

	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		wishdir *= 3.0f;
	if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		wishdir *= 0.333f;
	return wishdir;
}

void Renderer::setupView() {
	float fov = 75.0f;
	int width, height;

	glfwGetFramebufferSize(window, &width, &height);

	glViewport(0, 0, width, height);

	projection.perspective(fov, (float)width / (float)height, 1.0f, 65536.0f);

	view.loadIdentity();
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}

void Renderer::addMap(Bsp* map) {
	BspRenderer* mapRenderer = new BspRenderer(map, pipeline);

	mapRenderers.push_back(mapRenderer);
}

