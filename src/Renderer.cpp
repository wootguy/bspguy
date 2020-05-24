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

	bspShader = new ShaderProgram(g_shader_multitexture_vertex, g_shader_multitexture_fragment);
	bspShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	bspShader->setMatrixNames(NULL, "modelViewProjection");

	colorShader = new ShaderProgram(g_shader_cVert_vertex, g_shader_cVert_fragment);
	colorShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	colorShader->setMatrixNames(NULL, "modelViewProjection");
	colorShader->setVertexAttributeNames("vPosition", "vColor", NULL);
}

Renderer::~Renderer() {
	glfwTerminate();
}

void Renderer::renderLoop() {
	glfwSwapInterval(1);

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
		bspShader->bind();
		glEnable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);

		for (int i = 0; i < mapRenderers.size(); i++) {
			model.loadIdentity();
			bspShader->updateMatrixes();
			mapRenderers[i]->render();
		}		

		model.loadIdentity();
		colorShader->bind();

		drawLine(pickStart, pickStart + pickDir * 64.0f, { 0, 0, 255 });
		drawLine(pickStart, pickEnd, { 0, 255, 255 });

		drawLine(pickEnd + vec3(-32, 0, 0), pickEnd + vec3(32,0,0), { 255, 0, 0 });
		drawLine(pickEnd + vec3(0, -32, 0), pickEnd + vec3(0,32,0), { 255, 255, 0 });
		drawLine(pickEnd + vec3(0, 0, -32), pickEnd + vec3(0,0,32), { 0, 255, 0 });

		vec3 forward, right, up;
		makeVectors(cameraAngles, forward, right, up);
		//printf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

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

	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		getPickRay(pickStart, pickDir);

		float bestDist = 9e99;
		for (int i = 0; i < mapRenderers.size(); i++) {
			float dist = mapRenderers[i]->pickPoly(pickStart, pickDir);
			if (dist < bestDist) {
				bestDist = dist;
			}
		}

		pickEnd = pickStart + pickDir*bestDist;
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
		wishdir *= 4.0f;
	if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		wishdir *= 0.25f;
	return wishdir;
}

void Renderer::getPickRay(vec3& start, vec3& pickDir) {
	double xpos, ypos;
	int width, height;
	glfwGetCursorPos(window, &xpos, &ypos);
	glfwGetFramebufferSize(window, &width, &height);

	// invert ypos
	ypos = height - ypos;

	// translate mouse coordinates so that the origin lies in the center and is a scaler from +/-1.0
	float mouseX = ((xpos / (double)width) * 2.0f) - 1.0f;
	float mouseY = ((ypos / (double)height) * 2.0f) - 1.0f;

	// http://schabby.de/picking-opengl-ray-tracing/
	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);

	vec3 view = forward.normalize(1.0f);
	vec3 h = crossProduct(view, up).normalize(1.0f); // 3D float vector
	vec3 v = crossProduct(h, view).normalize(1.0f); // 3D float vector

	// convert fovy to radians 
	float rad = fov * PI / 180.0f;
	float vLength = tan(rad / 2.0f) * zNear;
	float hLength = vLength * (width / (float)height);

	v *= vLength;
	h *= hLength;

	// linear combination to compute intersection of picking ray with view port plane
	start = cameraOrigin + view * zNear + h * mouseX + v * mouseY;

	// compute direction of picking ray by subtracting intersection point with camera position
	pickDir = (start - cameraOrigin).normalize(1.0f);
}

void Renderer::setupView() {
	fov = 75.0f;
	zNear = 1.0f;
	zFar = 262144.0f;
	int width, height;

	glfwGetFramebufferSize(window, &width, &height);

	glViewport(0, 0, width, height);

	projection.perspective(fov, (float)width / (float)height, zNear, zFar);

	view.loadIdentity();
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}

void Renderer::addMap(Bsp* map) {
	BspRenderer* mapRenderer = new BspRenderer(map, bspShader);

	mapRenderers.push_back(mapRenderer);
}

void Renderer::drawLine(vec3 start, vec3 end, COLOR3 color) {
	cVert verts[2];

	verts[0].x = start.x;
	verts[0].y = start.z;
	verts[0].z = -start.y;
	verts[0].c = color;

	verts[1].x = end.x;
	verts[1].y = end.z;
	verts[1].z = -end.y;
	verts[1].c = color;

	VertexBuffer buffer(colorShader, COLOR_3B | POS_3F, &verts[0], 2);
	buffer.draw(GL_LINES);
}
