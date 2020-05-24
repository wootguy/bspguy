#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"

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

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
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

	g_render_flags = RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_WIREFRAME | RENDER_SPECIAL | RENDER_ENTS | RENDER_SPECIAL_ENTS;
	showDebugWidget = true;
	showKeyvalueWidget = true;
	pickInfo.valid = false;

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


	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	const char* glsl_version = "#version 130";
	ImGui_ImplOpenGL3_Init(glsl_version);

	vsync = true;
	io.Fonts->AddFontFromFileTTF("../imgui/misc/fonts/Roboto-Medium.ttf", 20.0f);

	float lastFrameTime = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float frameDelta = glfwGetTime() - lastFrameTime;
		frameTimeScale = 0.05f / frameDelta;
		float fps = 1.0f / frameDelta;
		
		frameTimeScale = 144.0f / fps;

		lastFrameTime = glfwGetTime();

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

			int highlightEnt = -1;
			if (pickInfo.valid && pickInfo.mapIdx == i) {
				highlightEnt = pickInfo.entIdx;
			}
			mapRenderers[i]->render(highlightEnt);
		}

		model.loadIdentity();
		colorShader->bind();

		drawLine(pickEnd + vec3(-32, 0, 0), pickEnd + vec3(32,0,0), { 255, 0, 0 });
		drawLine(pickEnd + vec3(0, -32, 0), pickEnd + vec3(0,32,0), { 255, 255, 0 });
		drawLine(pickEnd + vec3(0, 0, -32), pickEnd + vec3(0,0,32), { 0, 255, 0 });

		vec3 forward, right, up;
		makeVectors(cameraAngles, forward, right, up);
		//printf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

		drawGui();

		glfwSwapBuffers(window);
	}

	glfwTerminate();
}

void Renderer::drawGui() {
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGui::ShowDemoWindow();

	{
		ImGui::BeginMainMenuBar();

		if (ImGui::BeginMenu("Widgets"))
		{
			if (ImGui::MenuItem("Debug", NULL, showDebugWidget)) {
				showDebugWidget = !showDebugWidget;
			}
			if (ImGui::MenuItem("Keyvalue Editor", NULL, showKeyvalueWidget)) {
				showKeyvalueWidget = !showKeyvalueWidget;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Render"))
		{
			if (ImGui::MenuItem("Textures", NULL, g_render_flags & RENDER_TEXTURES)) {
				g_render_flags ^= RENDER_TEXTURES;
			}
			if (ImGui::MenuItem("Lightmaps", NULL, g_render_flags & RENDER_LIGHTMAPS)) {
				g_render_flags ^= RENDER_LIGHTMAPS;
			}
			if (ImGui::MenuItem("Wireframe", NULL, g_render_flags & RENDER_WIREFRAME)) {
				g_render_flags ^= RENDER_WIREFRAME;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Entities", NULL, g_render_flags & RENDER_ENTS)) {
				g_render_flags ^= RENDER_ENTS;
			}
			if (ImGui::MenuItem("Special", NULL, g_render_flags & RENDER_SPECIAL)) {
				g_render_flags ^= RENDER_SPECIAL;
			}
			if (ImGui::MenuItem("Special Entities", NULL, g_render_flags & RENDER_SPECIAL_ENTS)) {
				g_render_flags ^= RENDER_SPECIAL_ENTS;
			}			
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	{
		ImVec2 window_pos = ImVec2(10.0f, 35.0f);
		ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
		if (ImGui::Begin("Overlay", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
			if (ImGui::BeginPopupContextWindow())
			{
				if (ImGui::MenuItem("VSync", NULL, vsync)) {
					vsync = !vsync;
					glfwSwapInterval(vsync ? 1 : 0);
				}
				ImGui::EndPopup();
			}
		}
		ImGui::End();
	}

	if (showDebugWidget) {
		ImGui::SetNextWindowBgAlpha(0.75f);
		if (ImGui::Begin("Debug info", &showDebugWidget, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (pickInfo.valid) {
				Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
				Entity* ent = map->ents[pickInfo.entIdx];
				BSPMODEL& model = map->models[pickInfo.modelIdx];
				BSPFACE& face = map->faces[pickInfo.faceIdx];

				if (ImGui::TreeNodeEx("Map", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Text("Name: %s", map->name.c_str());
					ImGui::TreePop();
				}
				
				ImGui::Separator();

				if (ImGui::TreeNodeEx("Entity", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::TreePop();
				}

				
				ImGui::Separator();

				ImGui::Text("Face ID: %d", pickInfo.faceIdx);
				ImGui::Text("Plane ID: %d", face.iPlane);
			}
			else {
				ImGui::Text("Click on an object for debug info");
			}
			
		}
		ImGui::End();
	}

	if (showKeyvalueWidget) {
		ImGui::SetNextWindowBgAlpha(0.75f);
		string title = "Keyvalue Editor";

		title += "###entwindow";

		ImGui::SetNextWindowContentSize(ImVec2(400, 0.0f));
		if (ImGui::Begin(title.c_str(), &showKeyvalueWidget, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (pickInfo.valid) {
				Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
				Entity* ent = map->ents[pickInfo.entIdx];
				BSPMODEL& model = map->models[pickInfo.modelIdx];
				BSPFACE& face = map->faces[pickInfo.faceIdx];

				
				ImGui::Columns(2, "mycolumns", false); // 4-ways, with border
				ImGui::Text("Key"); ImGui::NextColumn();
				ImGui::Text("Value"); ImGui::NextColumn();
				
				static char keyNames[128][64];
				static char keyValues[128][64];

				ImGuiStyle& style = ImGui::GetStyle();
				float paddingx = style.WindowPadding.x + style.FramePadding.x;
				float inputWidth = (ImGui::GetWindowWidth() - paddingx * 2) * 0.5f;

				//ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
				for (int i = 0; i < ent->keyOrder.size(); i++) {
					string key = ent->keyOrder[i];
					string value = ent->keyvalues[key];
					strcpy(keyNames[i], key.c_str());
					strcpy(keyValues[i], value.c_str());
					
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::InputText(("##key" + to_string(i)).c_str(), keyNames[i], 64); ImGui::NextColumn();

					ImGui::SetNextItemWidth(inputWidth);
					ImGui::InputText(("##val" + to_string(i)).c_str(), keyValues[i], 64); ImGui::NextColumn();
				}

				ImGui::Columns(1);
			}
			else {
				ImGui::Text("Click on an entity to edit");
			}

		}
		ImGui::End();
	}

	// Rendering
	ImGui::Render();
	int display_w, display_h;
	glfwGetFramebufferSize(window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Renderer::cameraControls() {
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	if (!io.WantCaptureKeyboard)
		cameraOrigin += getMoveDir() * frameTimeScale;

	if (io.WantCaptureMouse)
		return;


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

		ImGui::SetWindowFocus(NULL);
		ImGui::ClearActiveID();
	}
	else {
		cameraIsRotating = false;
	}

	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && oldLeftMouse != GLFW_PRESS) {
		getPickRay(pickStart, pickDir);

		float bestDist = 9e99;
		memset(&pickInfo, 0, sizeof(PickInfo));
		for (int i = 0; i < mapRenderers.size(); i++) {
			if (mapRenderers[i]->pickPoly(pickStart, pickDir, bestDist, pickInfo)) {
				pickInfo.mapIdx = i;
			}
		}

		pickEnd = pickStart + pickDir*bestDist;
	}

	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
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

	wishdir *= moveSpeed;

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
