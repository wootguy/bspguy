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
}

Renderer::~Renderer() {
	glfwTerminate();
}

void Renderer::renderLoop() {
	glfwSwapInterval(1);

	ShaderProgram pipeline = ShaderProgram(g_shader_cVert_vertex, g_shader_cVert_fragment);

	mat4x4 model, view, projection, modelView, modelViewProjection;
	pipeline.setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	pipeline.setMatrixNames(NULL, "modelViewProjection");
	pipeline.setVertexAttributeNames("vPosition", "vColor", NULL);
	pipeline.bind();

	cCube cube(vec3(-10, -10, -10), vec3(10, 10, 10), {0, 255, 0} );
	cube.setColor({ 0,255,0 }, { 0,0,255 }, { 255,0,0 }, { 0,255,255 }, { 255,0,255 }, { 0,255,0 });
	
	VertexBuffer buffer(&pipeline, COLOR_3B | POS_3F, &cube, 6*6);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	while (!glfwWindowShouldClose(window))
	{
		float fov = 75.0f;
		int width, height;

		glfwGetFramebufferSize(window, &width, &height);

		glViewport(0, 0, width, height);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		view.loadIdentity();
		view.translate(0, 0, -50);

		model.loadIdentity();
		float spin = glfwGetTime() * 2;
		model.rotateZ(spin);
		model.rotateX(spin);

		projection.perspective(fov, (float)width / (float)height, 1.0f, 4096.0f);

		pipeline.updateMatrixes();
		
		buffer.draw(GL_TRIANGLES);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
}

void Renderer::addMap(Bsp* map) {

}
