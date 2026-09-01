#include "Editor.h"
#include "BspRenderer.h"
#include "ModelRenderer.h"
#include "PointEntRenderer.h"
#include "NavRenderer.h"
#include "Entity.h"
#include "FrameBuffer.h"
#include "Gui.h"
#include "render_utils.h"
#include "embedded_shaders.h"

#include <algorithm>

void Editor::updateGpuSupportFlags() {
	const char* openglExts = (const char*)glGetString(GL_EXTENSIONS);

	g_use_texture_arrays = false;

	if (g_settings.texture_atlas) {
		g_use_texture_arrays = false; // prefer to use simple texture mode
	}
	else if (g_settings.renderer == RENDERER_OPENGL_21_LEGACY) {
		logf("Legacy renderer selected. Not checking extension support.\n");
	}
	else if (g_opengl_texture_array_support) {
		g_use_texture_arrays = true;
	}
	else {
		logf("Neither texture arrays nor 3D textures are supported. Map rendering will be slow.\n");
	}
}

void Editor::compileShaderPrograms() {
	float startTime = glfwGetTime();

	g_renderStats.numShaders = 0;
	g_renderStats.numShadersFailed = 0;
	g_shaders.bsp->clearAttributes();
	g_shaders.clipnode->clearAttributes();
	g_shaders.color->clearAttributes();
	g_shaders.texture->clearAttributes();
	g_shaders.mdl->clearAttributes();
	g_shaders.spr->clearAttributes();
	g_shaders.vec3->clearAttributes();

	{
		ShaderProgram* sh = g_shaders.bsp;
		sh->addCompileFlag(SH_BSP_WIREFRAME, "WIREFRAME");
		sh->addCompileFlag(SH_BSP_TEX_ATLAS, "TEX_ATLAS");
		sh->addCompileFlag(SH_BSP_TEX_ARRAY, "TEX_ARRAY");
		sh->addCompileFlag(SH_BSP_TEX_PAL, "TEX_PAL");
		sh->skipCompileBits(SH_BSP_TEX_ARRAY | SH_BSP_TEX_ATLAS, true);
		sh->skipCompileBits(SH_BSP_TEX_ARRAY | SH_BSP_TEX_PAL, true);
		if (!g_opengl_texture_array_support) {
			sh->skipCompileBits(SH_BSP_TEX_ARRAY, true);
		}
		sh->compile(bsp_vert_glsl, bsp_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{ 2, GL_FLOAT, 0, "vTex" },
			{ 4, GL_UNSIGNED_BYTE, 0, "vAtlas" },
			{ 4, GL_UNSIGNED_BYTE, 0, "vCustom" },
			{ 4, GL_UNSIGNED_SHORT, 0, "vLightmapTex01" },
			{ 4, GL_UNSIGNED_SHORT, 0, "vLightmapTex23" },
			{ 4, GL_UNSIGNED_BYTE, 1, "vColor" },
			{ 3, GL_FLOAT, 0, "vPosition" }
			});
		sh->addUniforms({
			{"sTex", UNIFORM_INT},
			{"sLightmapTex", UNIFORM_INT},
			{"pTex", UNIFORM_INT},
			{"colorMult", UNIFORM_VEC4},
			{"alphaTest", UNIFORM_FLOAT},
			{"gamma", UNIFORM_FLOAT},
			{"wireframeColorDark", UNIFORM_VEC4},
			{"wireframeColorBright", UNIFORM_VEC4},
			{"wireframeThickness", UNIFORM_FLOAT},
			{"wireframeOnly", UNIFORM_FLOAT},
			{"textureAtlasScale", UNIFORM_FLOAT},
			{"lightmapAtlasScale", UNIFORM_FLOAT},
			{"paletteAtlasScale", UNIFORM_VEC2},
			{"lightmapMult", UNIFORM_VEC4},
			});
		sh->setUniform("sTex", 0, true);
		sh->setUniform("wireframeThickness", 0.5f, true);
		sh->setUniform("sLightmapTex", 1, true);
		sh->setUniform("pTex", 2, true);
	}

	{
		ShaderProgram* sh = g_shaders.color;
		sh->compile(cvert_vert_glsl, cvert_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{4, GL_UNSIGNED_BYTE, 1, "vColor"},
			{3, GL_FLOAT, 0, "vPosition"},
			});
		sh->addUniforms({
			{ "colorMult", UNIFORM_VEC4 },
			});
		sh->setUniform("colorMult", vec4(1, 1, 1, 1), true);
	}

	{
		ShaderProgram* sh = g_shaders.elink;
		sh->compile(elink_vert_glsl, elink_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{4, GL_UNSIGNED_BYTE, 1, "vColor"},
			{1, GL_FLOAT, 0, "vDist"},
			{1, GL_FLOAT, 0, "vDir"},
			{3, GL_FLOAT, 0, "vPosition"},
			});
		sh->addUniforms({
			{ "colorMult", UNIFORM_VEC4 },
			{ "time", UNIFORM_FLOAT },
			});
		sh->setUniform("colorMult", vec4(1, 1, 1, 1), true);
	}

	{
		ShaderProgram* sh = g_shaders.clipnode;
		sh->compile(clipnode_vert_glsl, clipnode_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{1, GL_UNSIGNED_SHORT, 0, "vEdges"},
			{4, GL_UNSIGNED_BYTE, 1, "vColor"},
			{3, GL_FLOAT, 0, "vPosition"},
			});
		sh->addUniforms({
			{"colorMult", UNIFORM_VEC4},
			{"wireframeThickness", UNIFORM_FLOAT},
			{"opacity", UNIFORM_FLOAT},
			});
		sh->setUniform("colorMult", vec4(1, 1, 1, 1), true);
		sh->setUniform("wireframeThickness", 0.5f, true);
		sh->setUniform("opacity", 0.5f, true);
	}

	{
		ShaderProgram* sh = g_shaders.texture;
		sh->compile(tvert_vert_glsl, tvert_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{2, GL_FLOAT, 0, "vTex"},
			{3, GL_FLOAT, 0, "vPosition"},
			});
	}

	{
		ShaderProgram* sh = g_shaders.mdl;
		sh->addCompileFlag(SH_MDL_BONE_TEXTURE, "BONE_TEXTURE");
		sh->compile(mdl_vert_glsl, mdl_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{2, GL_FLOAT, 0, "vTex"},
			{3, GL_FLOAT, 1, "vNormal"},
			{3, GL_FLOAT, 0, "vPosition"},
			{1, GL_FLOAT, 0, "vBone"},
			});
		sh->addUniforms({
			{"sTex", UNIFORM_INT},
			{"elights", UNIFORM_INT},
			{"ambient", UNIFORM_VEC3},
			{"lights", UNIFORM_MAT3},
			{"additiveEnable", UNIFORM_INT},
			{"chromeEnable", UNIFORM_INT},
			{"flatshadeEnable", UNIFORM_INT},
			{"colorMult", UNIFORM_VEC4},
			{"viewerOrigin", UNIFORM_VEC3},
			{"viewerRight", UNIFORM_VEC3},
			{"boneMatrixTexture", UNIFORM_INT},
			{"gamma", UNIFORM_FLOAT},
			});
	}

	{
		ShaderProgram* sh = g_shaders.spr;
		sh->compile(spr_vert_glsl, spr_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{2, GL_FLOAT, 0, "vTex"},
			{3, GL_FLOAT, 0, "vPosition"},
			});
		sh->addUniform("color", UNIFORM_VEC4);
	}

	{
		ShaderProgram* sh = g_shaders.vec3;
		sh->addCompileFlag(SH_VEC3_DEPTH_HACK, "DEPTH_HACK");
		sh->compile(vec3_vert_glsl, vec3_frag_glsl, "120");
		sh->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
		sh->setMatrixNames(NULL, "modelViewProjection");
		sh->addAttributes({
			{3, GL_FLOAT, 0, "vPosition"}
			});
		sh->addUniforms({
			{"color", UNIFORM_VEC4}
			});
		sh->setUniform("color", vec4(1, 1, 1, 1), true);
	}

	debugf("Compiled %d / %d shaders in %.2f\n",
		(int)(g_renderStats.numShaders - g_renderStats.numShadersFailed), (int)g_renderStats.numShaders,
		glfwGetTime() - startTime);
	glCheckError("compiling shaders");
}


void Editor::renderLoop() {
	glCheckError("entering renderloop");

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glCheckError("renderloop state enable");

	setupTransformAxes();

	glCheckError("pre render loop");

	int loadState = 0;

	fgdFuture = async(launch::async, &Editor::loadFgds, this);

	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	if (g_settings.render_scale != 100) {
		viewportScale = g_settings.render_scale * 0.01f;
		viewportFbo = new FrameBuffer(windowWidth, windowHeight, viewportScale);
	}

	int lastRenderScale = g_settings.render_scale;

	float lastFrameTime = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (isIconified) {
			sleepms(50);
			continue;
		}

		float frameDelta = glfwGetTime() - lastFrameTime;
		frameTimeScale = 0.05f / frameDelta;
		float fps = 1.0f / frameDelta;

		//FIXME : frameTimeScale = 0.05f / frameDelta ???
		frameTimeScale = 144.0f / fps;
		lastFrameTime = glfwGetTime();
		isLoading = reloading;


		if (lastRenderScale != g_settings.render_scale) {
			lastRenderScale = g_settings.render_scale;
			delete viewportFbo;
			viewportFbo = NULL;

			if (g_settings.render_scale != 100) {
				viewportFbo = new FrameBuffer(windowWidth, windowHeight, viewportScale);
			}
		}

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		mapRenderer->delayLoadData();

		if (viewportFbo) {
			viewportFbo->bind();
			drawViewport();
			viewportFbo->unbind();
			viewportFbo->draw();
		}
		else {
			glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
			glViewport(0, 0, windowWidth, windowHeight);
			drawViewport();
		}

		glActiveTexture(GL_TEXTURE0); // needed even if gui isn't drawn(???)

		// updated here so imgui can use control logic from this class
		controlsBegin();

		if (!g_app->hideGui) {
			gui->draw();
			g_active_shader_program = -1;
		}
		else {
			gui->texts.clear();
		}

		viewportControls();
		controlsEnd();

		glfwSwapBuffers(window);
		glCheckError("Swap buffers and controls");

		if (!isLoading && openMapAfterLoad.size()) {
			openMap(openMapAfterLoad.c_str());
			glCheckError("Opening map");
		}

		if (reloading && fgdFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
			postLoadFgds();
			reloading = reloadingGameDir = false;
			glCheckError("FGD post load");
		}

		if (!isFocused && !isHovered) {
			sleepms(50);
		}

		if (loadState == 0) {
			debugf("Startup finished in %.2fs\n", glfwGetTime() - programStartTime);
			loadState = 1;
			programStartTime = -programStartTime;
		}
		if (loadState == 1 && !isLoading) {
			debugf("Map loaded in %.2fs\n", glfwGetTime() - programStartTime);
			loadState = 2;
		}
	}

	glfwTerminate();
}

void Editor::setupView() {
	projection.perspective(fov, (float)windowWidth / (float)windowHeight, zNear, zFar);

	view.loadIdentity();
	view.rotateZ(PI * cameraAngles.y / 180.0f);
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}


void Editor::drawViewport() {
	setupView();
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	glCheckError("Setting up view");

	if (previewMode || (g_settings.render_flags & RENDER_SKYBOX)) {
		bool wireframeOnly = !(g_settings.render_flags & (RENDER_LIGHTMAPS | RENDER_TEXTURES))
			&& (g_settings.render_flags & RENDER_WIREFRAME);
		if (!wireframeOnly) { // skybox can make lines hard to see in this mode
			mapRenderer->drawSkybox();
			glCheckError("Rendering skybox");
		}
	}

	mapRenderer->updateOrderEnts();

	// draw opaque world/entity faces
	mapRenderer->renderSolids(transformTarget == TRANSFORM_VERTEX, false);
	mapRenderer->drawPointEntities();

	glCheckError("Rendering BSP (opaque pass)");

	// studio models have transparent boxes that need to draw over the world but behind transparent
	// brushes like a trigger_once which is rendered using the clipnode model
	if (modelRenderer->drawModelsAndSprites(mapRenderer->renderOffset, cameraOrigin, cameraAngles)) {
		isLoading = true;
	}
	glCheckError("Rendering models and sprites");

	// draw transparent entity faces
	mapRenderer->renderSolids(transformTarget == TRANSFORM_VERTEX, true);

	// don't draw clipnodes in leaf mode because they're the same color/style and confuse picking
	if (pickMode != PICK_LEAF)
		mapRenderer->renderClipnodes(clipnodeRenderHull);

	glCheckError("Rendering BSP (transparency pass)");

	if (mapArrangeMode)
		drawArrangeMaps();

	if (pickMode == PICK_LEAF && !previewMode) {
		mapRenderer->renderLeaves();
	}

	if (!mapArrangeMode) {
		if (g_settings.show_wpoly || (g_settings.render_flags & RENDER_PVS)) {
			mapRenderer->updatePvs(cameraOrigin);

			if ((g_settings.render_flags & RENDER_PVS) && !previewMode)
				mapRenderer->drawPvs();
		}
	}

	glCheckError("Rendering leaf selection");

	if (!mapRenderer->isFinishedLoading()) {
		isLoading = true;
	}

	model.loadIdentity();

	if (!previewMode) {
		g_shaders.color->bind();
		drawEntDirectionVectors(); // draws over world faces
		glCheckError("Rendering entity vectors");

		drawTextureAxes();
		glCheckError("Rendering texture axes");

		if ((g_settings.render_flags & (RENDER_ORIGIN | RENDER_MAP_BOUNDARY)) || hasCullbox) {
			g_shaders.color->bind();
			model.loadIdentity();
			g_shaders.color->pushMatrix(MAT_MODEL);
			g_shaders.color->updateMatrixes();
			glDisable(GL_CULL_FACE);

			if ((g_settings.render_flags & RENDER_MAP_BOUNDARY) && !emptyMapLoaded) {
				drawMapBoundary();
			}

			if (pickInfo.getEnt()) {
				vec3 offset = mapRenderer->renderOffset;
				model.translate(offset.x, offset.y, offset.z);
			}
			g_shaders.color->updateMatrixes();

			if (hasCullbox) {
				drawBox(cullMins, cullMaxs, COLOR4(255, 0, 0, 64));
			}

			if (g_settings.render_flags & RENDER_ORIGIN) {
				originBuffer->draw(g_shaders.color, GL_LINES);
			}

			glEnable(GL_CULL_FACE);
			g_shaders.color->popMatrix(MAT_MODEL);
		}
		glCheckError("Rendering map boundary/cull box");

		drawEntConnections();
		glCheckError("Rendering entity connections");

		bool isScalingObject = transformMode == TRANSFORM_SCALE && transformTarget == TRANSFORM_OBJECT;
		bool isMovingOrigin = transformMode == TRANSFORM_MOVE && transformTarget == TRANSFORM_ORIGIN && originSelected;
		bool isTransformingValid = ((isTransformableSolid && !modelUsesSharedStructures) || !isScalingObject) && transformTarget != TRANSFORM_ORIGIN;
		bool isTransformingWorld = pickInfo.getEntIndex() == 0 && transformTarget != TRANSFORM_OBJECT;
		if (showDragAxes && !movingEnt && !isTransformingWorld && pickInfo.getEntIndex() >= 0 && (isTransformingValid || isMovingOrigin)) {
			drawTransformAxes();
			glCheckError("Rendering transform axes");
		}

		int modelIdx = pickInfo.getModelIndex();
		if (modelIdx > 0 && pickMode == PICK_OBJECT) {
			if (transformTarget == TRANSFORM_VERTEX && isTransformableSolid) {
				drawModelVerts();
				glCheckError("Rendering model verts");
			}
			if (transformTarget == TRANSFORM_ORIGIN) {
				drawModelOrigin();
				glCheckError("Rendering model origin");
			}
		}

		drawDebugObjects();
		glCheckError("Rendering debug polys");

		navRenderer->renderNavMesh(mapRenderer->map, cameraOrigin);

		if (pickMode == PICK_LEAF && (g_settings.render_flags & RENDER_LEAF_GRAPH)) {
			navRenderer->renderLeafGraph(mapRenderer->leafNavMesh, cameraOrigin, mapRenderer->map);
		}

		addNameTags();
	}

	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);
	//logf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

	drawMouseObjects();
	glCheckError("Draw mouse objects");
}

void Editor::drawMapBoundary() {
	glDepthFunc(GL_LESS);

	COLOR4 red = COLOR4(255, 0, 0, 64);
	COLOR4 invisible = COLOR4(0, 0, 0, 0);
	COLOR4 green = COLOR4(0, 255, 0, 64);
	COLOR4 boxColor = gui->hoveredOOB == 0 ? red : green;
	vec3 center = vec3();
	float width = g_settings.mapsize_max;
	vec3 sz = vec3(width, width, width);
	vec3 pos = vec3(center.x, center.z, -center.y);
	cCube cube(pos - sz, pos + sz, gui->hoveredOOB == 0 ? red : green);

	if (gui->hoveredOOB >= 0) {
		red = COLOR4(255, 0, 0, 128);

		BSPPLANE plane;
		plane.fDist = g_settings.mapsize_max;
		switch (gui->hoveredOOB) {
		case 1: plane.vNormal = vec3(1, 0, 0); cube.right.setColor(invisible); break;
		case 2: plane.vNormal = vec3(-1, 0, 0); cube.left.setColor(invisible); break;
		case 3: plane.vNormal = vec3(0, 1, 0);  cube.front.setColor(invisible); break;
		case 4: plane.vNormal = vec3(0, -1, 0); cube.back.setColor(invisible); break;
		case 5: plane.vNormal = vec3(0, 0, 1); cube.bottom.setColor(invisible); break;
		case 6: plane.vNormal = vec3(0, 0, -1); cube.top.setColor(invisible); break;
		}

		drawPlane(plane, red, g_settings.mapsize_max * 1.2f);
	}

	{
		VertexBuffer buffer(g_shaders.color, &cube, 6 * 6);
		buffer.upload();
		buffer.draw(g_shaders.color, GL_TRIANGLES);
	}
	glDepthFunc(GL_LEQUAL);

	glDepthFunc(GL_LEQUAL); // draw lines in front (still causes some z fighting)
	drawBoxOutline(vec3(), g_settings.mapsize_max * 2, COLOR4(0, 0, 0, 255));

	glDepthFunc(GL_LESS);
}

void Editor::drawDebugObjects() {
	int modelIdx = pickInfo.getModelIndex();
	Bsp* map = pickInfo.getMap();

	if (debugClipnodes && modelIdx > 0) {
		BSPMODEL* pickModel = pickInfo.getModel();
		glDisable(GL_CULL_FACE);
		int currentPlane = 0;
		drawClipnodes(map, pickModel->iHeadnodes[1], currentPlane, debugInt);
		debugIntMax = currentPlane - 1;
		glEnable(GL_CULL_FACE);
	}

	if (debugClipnodes && false) {
		glDisable(GL_CULL_FACE);

		vec3 localCamera = cameraOrigin - mapRenderer->mapOffset;
		if (pickInfo.getEnt()) {
			localCamera -= pickInfo.getEnt()->getOrigin();
		}

		vector<int> nodeBranch;
		int leafIdx;
		int childIdx = -1;
		int headNode = map->models[0].iHeadnodes[1];
		int contents = map->pointContents(headNode, localCamera, 1, nodeBranch, leafIdx, childIdx);
		int parentNodeIdx = nodeBranch.size() ? nodeBranch[nodeBranch.size() - 1] : headNode;
		BSPPLANE& plane = map->planes[map->clipnodes[parentNodeIdx].iPlane];
		drawPlane(plane, COLOR4(255, 255, 255, 255));

		glEnable(GL_CULL_FACE);
	}

	if (debugNodes && modelIdx > 0) {
		BSPMODEL* pickModel = pickInfo.getModel();
		glDisable(GL_CULL_FACE);
		int currentPlane = 0;
		drawNodes(map, pickModel->iHeadnodes[0], currentPlane, debugNode);
		debugNodeMax = currentPlane - 1;
		glEnable(GL_CULL_FACE);
	}

	if (false) {
		vec3 start = cameraOrigin;
		vec3 end = cameraOrigin - vec3(0, 0, 2048);
		TraceResult tr;
		map->traceHull(start, end, 0, &tr);
		end = tr.vecEndPos;
		vec3 sz = vec3(4, 4, 4);

		drawBox(end - sz, end + sz, COLOR4(0, 255, 0, 128));

		COLOR3 color = map->get_lighting(cameraOrigin);
		logf("COLOR: %d %d %d\n", color.r, color.g, color.b);
	}

	if (g_app->debugPoly.isValid)
		drawPolygon3D(g_app->debugPoly, COLOR4(0, 255, 0, 150));
	if (g_app->debugPoly2.isValid)
		drawPolygon3D(g_app->debugPoly2, COLOR4(255, 0, 0, 150));
	if (g_app->debugPoly3.isValid)
		drawPolygon3D(g_app->debugPoly3, COLOR4(255, 255, 255, 150));
	if (g_app->debugLine0 != g_app->debugLine1) {
		drawLine(debugLine0, debugLine1, { 128, 0, 255, 255 });
		drawLine(debugLine2, debugLine3, { 0, 255, 0, 255 });
		drawLine(debugLine4, debugLine5, { 255, 128, 0, 255 });
	}

	/*
	if (gui->showDebugWidget && pickInfo.getFace()) {
		BSPFACE& face = *pickInfo.getFace();
		Bsp* map = mapRenderer->map;
		glDisable(GL_CULL_FACE);

		for (int i = 0; i < face.nEdges; i++) {
			int32_t edgeIdx = map->surfedges[face.iFirstEdge + i];
			BSPEDGE& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
			drawBox(map->verts[vertIdx], 8, COLOR4(0, 128, 0, 255));
			drawLine(map->verts[edge.iVertex[0]], map->verts[edge.iVertex[1]], COLOR4(128, 0, 255, 255));

			vec3 start = map->verts[edge.iVertex[0]];
			vec3 end = map->verts[edge.iVertex[1]];
			drawArrow(start, end, COLOR4(0, 255, 0, 255));
		}
		glEnable(GL_CULL_FACE);
	}
	*/

	//glCheckError("Rendering debug clipnodes");
}

void Editor::drawMouseObjects() {
	int w = viewportFbo ? viewportFbo->width : windowWidth;
	int h = viewportFbo ? viewportFbo->height : windowHeight;

	if (cameraMouseCapture || isBoxSelecting) {
		g_shaders.color->bind();
		g_shaders.color->pushMatrix(MAT_PROJECTION);
		g_shaders.color->pushMatrix(MAT_VIEW);
		g_shaders.color->pushMatrix(MAT_MODEL);
		projection.ortho(0, w, h, 0, -1.0f, 1.0f);
		view.loadIdentity();
		model.loadIdentity();
		g_shaders.color->updateMatrixes();
		glDisable(GL_DEPTH_TEST);

		if (cameraMouseCapture) {
			float border = 1;
			float thick = w > 1024 ? 2 : 1.5f;
			float len = w > 1024 ? 12.5f : 8.5f;
			vec2 center(w / 2 + 0.5f, h / 2 + 0.5f);

			drawRect2D(center - vec2(len + border, thick / 2 + border), vec2(len * 2 + border * 2, thick + border * 2), COLOR4(0, 0, 0, 255));
			drawRect2D(center - vec2(thick / 2 + border, len + border), vec2(thick + border * 2, len * 2 + border * 2), COLOR4(0, 0, 0, 255));

			drawRect2D(center - vec2(len, thick / 2), vec2(len * 2, thick), COLOR4(255, 255, 255, 255));
			drawRect2D(center - vec2(thick / 2, len), vec2(thick, len * 2), COLOR4(255, 255, 255, 255));
		}

		bool boxBigEnough = (boxSelectEnd - boxSelectStart).length() > 8;
		if (isBoxSelecting && boxBigEnough && draggingAxis == -1) {
			drawLine2D(vec2(boxSelectStart.x, boxSelectStart.y), vec2(boxSelectEnd.x, boxSelectStart.y), COLOR4(255, 255, 255, 255));
			drawLine2D(vec2(boxSelectEnd.x, boxSelectStart.y), vec2(boxSelectEnd.x, boxSelectEnd.y), COLOR4(255, 255, 255, 255));
			drawLine2D(vec2(boxSelectEnd.x, boxSelectEnd.y), vec2(boxSelectStart.x, boxSelectEnd.y), COLOR4(255, 255, 255, 255));
			drawLine2D(vec2(boxSelectStart.x, boxSelectEnd.y), vec2(boxSelectStart.x, boxSelectStart.y), COLOR4(255, 255, 255, 255));
		}

		glEnable(GL_DEPTH_TEST);
		g_shaders.color->popMatrix(MAT_PROJECTION);
		g_shaders.color->popMatrix(MAT_VIEW);
		g_shaders.color->popMatrix(MAT_MODEL);
	}
}

void Editor::drawArrangeMaps() {
	struct RenderMap {
		BspRenderer* renderer;
		Entity* controlEnt;
		vec3 mins, maxs;
	};
	vector<RenderMap> renderMaps;

	int idx = 1;
	for (BspRenderer* arrangeBsp : arrangeBsps) {
		Entity* controlEnt = mapRenderer->map->ents[idx++];
		arrangeBsp->map->ents[0]->setOrAddKeyvalue("origin", controlEnt->getOrigin().toKeyvalueString());

		arrangeBsp->updateOrderEnts();

		RenderMap rmap;
		rmap.controlEnt = controlEnt;
		rmap.renderer = arrangeBsp;
		arrangeBsp->map->get_bounding_box(rmap.mins, rmap.maxs);

		renderMaps.push_back(rmap);
	}

	for (RenderMap& arrangeBsp : renderMaps) {
		// opaque pass
		arrangeBsp.renderer->renderSolids(false, false);
		arrangeBsp.renderer->drawPointEntities();
	}

	for (RenderMap& arrangeBsp : renderMaps) {
		// transparency pass
		arrangeBsp.renderer->renderSolids(false, true);
		arrangeBsp.renderer->renderClipnodes(clipnodeRenderHull);
	}

	g_shaders.color->bind();
	g_shaders.color->modelMat->loadIdentity();
	g_shaders.color->updateMatrixes();

	for (RenderMap& rmap : renderMaps) {
		Bsp* map = rmap.renderer->map;
		COLOR4 boxColor = COLOR4(0, 0, 255, 128);

		vector<Entity*> selected = pickInfo.getEnts();
		for (Entity* selectedEnt : selected) {
			if (selectedEnt == rmap.controlEnt) {
				boxColor.g = 128;
			}
		}

		bool collision = false;
		for (RenderMap& othermap : renderMaps) {
			if (rmap.renderer == othermap.renderer)
				continue;
			if (boxesIntersect(rmap.mins, rmap.maxs, othermap.mins, othermap.maxs)) {
				collision = true;
				break;
			}
		}

		if (collision) {
			boxColor.r = 255;
			boxColor.b = 0;
		}

		drawBox(rmap.mins, rmap.maxs, boxColor);
	}
}

void Editor::drawModelVerts() {
	if (modelVertBuff == NULL || modelVerts.size() == 0)
		return;
	glClear(GL_DEPTH_BUFFER_BIT);

	Bsp* map = mapRenderer->map;
	Entity* ent = pickInfo.getEnt();
	vec3 renderOffset = mapRenderer->renderOffset;
	vec3 localCameraOrigin = cameraOrigin - mapRenderer->mapOffset;

	COLOR4 vertDimColor = { 200, 200, 200, 255 };
	COLOR4 vertHoverColor = { 255, 255, 255, 255 };
	COLOR4 edgeDimColor = { 255, 128, 0, 255 };
	COLOR4 edgeHoverColor = { 255, 255, 0, 255 };
	COLOR4 selectColor = { 0, 128, 255, 255 };
	COLOR4 hoverSelectColor = { 96, 200, 255, 255 };
	vec3 entOrigin = ent->getOrigin();

	if (modelUsesSharedStructures) {
		vertDimColor = { 32, 32, 32, 255 };
		edgeDimColor = { 64, 64, 32, 255 };
	}

	int cubeIdx = 0;
	for (int i = 0; i < modelVerts.size(); i++) {
		vec3 ori = modelVerts[i].pos + entOrigin;
		float s = (ori - localCameraOrigin).length() * vertExtentFactor;
		ori = ori.flip();

		if (anyEdgeSelected) {
			s = 0; // can't select certs when edges are selected
		}

		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		COLOR4 color;
		if (modelVerts[i].selected) {
			color = i == hoverVert ? hoverSelectColor : selectColor;
		}
		else {
			color = i == hoverVert ? vertHoverColor : vertDimColor;
		}
		modelVertCubes[cubeIdx++] = cCube(min, max, color);
	}

	for (int i = 0; i < modelEdges.size(); i++) {
		vec3 ori = getEdgeControlPoint(modelVerts, modelEdges[i]) + entOrigin;
		float s = (ori - localCameraOrigin).length() * vertExtentFactor;
		ori = ori.flip();

		if (anyVertSelected && !anyEdgeSelected) {
			s = 0; // can't select edges when verts are selected
		}

		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		COLOR4 color;
		if (modelEdges[i].selected) {
			color = i == hoverEdge ? hoverSelectColor : selectColor;
		}
		else {
			color = i == hoverEdge ? edgeHoverColor : edgeDimColor;
		}
		modelVertCubes[cubeIdx++] = cCube(min, max, color);
	}

	modelVertBuff->upload();

	model.loadIdentity();
	model.translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_shaders.color->updateMatrixes();
	modelVertBuff->draw(g_shaders.color, GL_TRIANGLES);
}

void Editor::drawModelOrigin() {
	if (modelOriginBuff == NULL)
		return;

	glClear(GL_DEPTH_BUFFER_BIT);

	Bsp* map = mapRenderer->map;
	vec3 renderOffset = mapRenderer->renderOffset;
	Entity* ent = pickInfo.getEnt();

	COLOR4 vertDimColor = { 0, 200, 0, 255 };
	COLOR4 vertHoverColor = { 128, 255, 128, 255 };
	COLOR4 selectColor = { 0, 128, 255, 255 };
	COLOR4 hoverSelectColor = { 96, 200, 255, 255 };

	if (modelUsesSharedStructures) {
		vertDimColor = { 32, 32, 32, 255 };
	}

	vec3 ori = transformedOrigin;
	float s = (ori - cameraOrigin).length() * vertExtentFactor;
	ori = ori.flip() + renderOffset;

	vec3 min = vec3(-s, -s, -s) + ori;
	vec3 max = vec3(s, s, s) + ori;
	COLOR4 color;
	if (originSelected) {
		color = originHovered ? hoverSelectColor : selectColor;
	}
	else {
		color = originHovered ? vertHoverColor : vertDimColor;
	}
	modelOriginCube = cCube(min, max, color);
	modelOriginBuff->upload();

	model.loadIdentity();
	g_shaders.color->updateMatrixes();
	modelOriginBuff->draw(g_shaders.color, GL_TRIANGLES);
}

void Editor::drawTransformAxes() {
	if (!canTransform) {
		return;
	}

	glClear(GL_DEPTH_BUFFER_BIT);

	updateDragAxes();

	glDisable(GL_CULL_FACE);

	if (transformMode == TRANSFORM_SCALE && transformTarget == TRANSFORM_OBJECT) {
		vec3 ori = scaleAxes.origin;
		model.translate(ori.x, ori.z, -ori.y);
		g_shaders.color->updateMatrixes();
		scaleAxes.buffer->upload();
		scaleAxes.buffer->draw(g_shaders.color, GL_TRIANGLES);
	}
	if (transformMode == TRANSFORM_MOVE) {
		vec3 ori = moveAxes.origin;

		bool shouldOffset = false;
		for (Entity* ent : pickInfo.getEnts()) {
			shouldOffset = ent->shouldDisplayDirectionVector();
			break;
		}

		float offset = shouldOffset ? 64 : 0;
		model.translate(ori.x, ori.z + offset, -ori.y);
		g_shaders.color->updateMatrixes();
		moveAxes.buffer->upload();
		moveAxes.buffer->draw(g_shaders.color, GL_TRIANGLES);
	}
}

void Editor::drawEntConnections() {
	if (g_settings.render_flags & RENDER_ENT_CONNECTIONS) {
		model.loadIdentity();
		model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);

		g_shaders.elink->bind();
		g_shaders.elink->updateMatrixes();
		g_shaders.elink->setUniform("time", (float)glfwGetTime());
		if (entConnections) {
			entConnections->draw(g_shaders.elink, GL_LINES);
		}

		g_shaders.color->updateMatrixes();
		g_shaders.color->bind();

		if (entConnectionPoints) {
			glDisable(GL_DEPTH_TEST);
			entConnectionPoints->draw(g_shaders.color, GL_TRIANGLES);
			glEnable(GL_DEPTH_TEST);
		}
	}
}

void Editor::updateEntDirectionVectors() {
	if (entDirectionVectors) {
		delete entDirectionVectors;
		entDirectionVectors = NULL;
	}

	if (!(g_settings.render_flags & RENDER_ENT_DIRECTIONS)) {
		return;
	}

	vector<Entity*> pickEnts = pickInfo.getEnts();

	if (pickEnts.empty()) {
		return;
	}

	vector<Entity*> directEnts;

	for (Entity* ent : pickEnts) {
		if (ent->shouldDisplayDirectionVector())
			directEnts.push_back(ent);
	}

	if (directEnts.empty())
		return;

	struct cArrow {
		cCube up;
		cCube right;
		cCube shaft; // minor todo: one face can be omitted. make a new struct
		cPyramid tip;
	};
	int arrowVerts = 6 * 6 * 3 + (6 + 3 * 4);

	int numPointers = directEnts.size();
	cArrow* arrows = new cArrow[numPointers];

	for (int i = 0; i < numPointers; i++) {
		Entity* ent = directEnts[i];
		vec3 ori = getEntOrigin(mapRenderer->map, ent).flip();
		vec3 angles = ent->getVisualAngles() * (PI / 180.0f);

		// i swear every use of entity angles needs a matrix with its own unique order/inversions
		// this is the combo used so far
		mat4x4 rotMat;
		rotMat.loadIdentity();
		rotMat.rotateX(-angles.z);
		rotMat.rotateZ(-angles.x);
		rotMat.rotateY(-angles.y);

		arrows[i].shaft = cCube(vec3(-1, -1, -1), vec3(40, 1, 1), COLOR4(0, 255, 0, 255));
		arrows[i].right = cCube(vec3(-0.5f, -0.5f, -0.5f), vec3(0.5f, 0.5f, 24), COLOR4(128, 0, 255, 255));
		arrows[i].up = cCube(vec3(-0.5f, -0.5f, -0.5f), vec3(0.5f, 24, 0.5f), COLOR4(0, 128, 255, 255));
		arrows[i].tip = cPyramid(vec3(40, 0, 0), 4, 16, COLOR4(0, 255, 0, 255));

		cVert* rawVerts = (cVert*)&arrows[i];
		for (int k = 0; k < arrowVerts; k++) {
			vec3* pos = (vec3*)&rawVerts[k].x;
			*pos = (rotMat * vec4(*pos, 1)).xyz() + ori;
		}
	}

	entDirectionVectors = new VertexBuffer(g_shaders.color, arrows, numPointers * arrowVerts, true);
	entDirectionVectors->upload();
}

void Editor::drawEntDirectionVectors() {
	if (!entDirectionVectors) {
		return;
	}

	glCullFace(GL_FRONT);
	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);

	g_shaders.color->bind();
	model.loadIdentity();
	model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);
	g_shaders.color->updateMatrixes();
	entDirectionVectors->draw(g_shaders.color, GL_TRIANGLES);

	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
	glCullFace(GL_BACK);
}

void Editor::updateTextureAxes() {
	if (allTextureAxes) {
		delete allTextureAxes;
		allTextureAxes = NULL;
	}

	if (pickInfo.faces.empty()) {
		return;
	}

	int numVerts = pickInfo.faces.size() * 6;
	vector<cVert> verts;
	Bsp* map = mapRenderer->map;
	const float len = 16;

	int vidx = 0;
	for (int i = 0; i < pickInfo.faces.size(); i++) {
		int faceidx = pickInfo.faces[i];
		BSPFACE& face = map->faces[faceidx];
		BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
		vec3 mcenter = map->get_face_center(faceidx);

		int model = map->get_model_from_face(faceidx);

		if (model != 0) {
			for (int k = 0; k < map->ents.size(); k++) {
				Entity* ent = map->ents[k];
				if (ent->getBspModelIdx() == model) {
					mat4x4 rotMat = ent->getRotationMatrix(true);
					//mat4x4 rotMat2 = ent->getRotationMatrix(false);
					vec3 offset = ent->getOrigin();
					vec3 center = ((rotMat * vec4(mcenter, 1)).xyz() + offset).flip();
					vec3 vS = ((rotMat * vec4(info.vS, 1)).xyz()).flip();
					vec3 vT = ((rotMat * vec4(info.vT, 1)).xyz()).flip();
					vec3 norm = crossProduct(vT, vS).normalize();

					verts.push_back(cVert(center, COLOR4(255, 255, 0, 255)));
					verts.push_back(cVert(center + vS.normalize(len), COLOR4(255, 255, 0, 255)));
					verts.push_back(cVert(center, COLOR4(0, 255, 0, 255)));
					verts.push_back(cVert(center + vT.normalize(len), COLOR4(0, 255, 0, 255)));
					verts.push_back(cVert(center, COLOR4(0, 64, 255, 255)));
					verts.push_back(cVert(center + norm.normalize(len), COLOR4(0, 64, 255, 255)));
				}
			}
		}
		else {
			vec3 center = mcenter.flip();
			vec3 norm = crossProduct(info.vT, info.vS).normalize();

			// world face
			verts.push_back(cVert(center, COLOR4(255, 255, 0, 255)));
			verts.push_back(cVert(center + info.vS.flip().normalize(len), COLOR4(255, 255, 0, 255)));
			verts.push_back(cVert(center, COLOR4(0, 255, 0, 255)));
			verts.push_back(cVert(center + info.vT.flip().normalize(len), COLOR4(0, 255, 0, 255)));
			verts.push_back(cVert(center, COLOR4(0, 64, 255, 255)));
			verts.push_back(cVert(center + norm.flip().normalize(len), COLOR4(0, 64, 255, 255)));
		}
	}

	cVert* uploadVerts = new cVert[verts.size()];
	memcpy(uploadVerts, &verts[0], sizeof(cVert) * verts.size());

	allTextureAxes = new VertexBuffer(g_shaders.color, uploadVerts, verts.size(), true);
	allTextureAxes->upload();
}

void Editor::drawTextureAxes() {
	if (!allTextureAxes) {
		return;
	}

	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);

	g_shaders.color->bind();
	model.loadIdentity();
	model.translate(mapRenderer->renderOffset.x, mapRenderer->renderOffset.y, mapRenderer->renderOffset.z);
	g_shaders.color->updateMatrixes();
	allTextureAxes->draw(g_shaders.color, GL_LINES);

	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
}

void Editor::addNameTags() {
	Bsp* map = mapRenderer->map;

	bool renderAllTags = g_settings.render_flags & RENDER_NAME_TAGS;
	if (!renderAllTags && pickInfo.ents.empty() || map->ents.empty())
		return;

	if (!map->valid || map->modelCount == 0)
		return;

	unordered_set<int> selected;
	unordered_set<int> added;
	for (int i : pickInfo.ents) {
		selected.insert(i);
		added.insert(i);
	}

	struct TagEnt {
		Entity* ent;
		int idx;
		float dist;
		vec3 ori;
		string text;
		COLOR4 color;
	};

	vector<TagEnt> tags;
	vec3 worldOffset = map->ents[0]->getOrigin();

	vector<Entity*> ents = map->ents;
	vector<int> entIdx = pickInfo.ents;
	if (!renderAllTags) {
		ents = pickInfo.getEnts();
		for (auto item : entLinks) {
			if (!added.count(item.first)) {
				entIdx.push_back(item.first);
				ents.push_back(map->ents[item.first]);
				added.insert(item.first);
			}
		}
	}

	for (int i = 0; i < ents.size(); i++) {
		int idx = renderAllTags ? i : entIdx[i];
		Entity* ent = ents[i];
		if (ent->hidden)
			continue;

		string tname = ent->getTargetname();
		if (tname.empty() && renderAllTags)
			continue;

		bool isSelected = selected.count(idx);
		bool isLinked = false;
		COLOR4 color = isSelected ? COLOR4(255, 64, 64, 255) : COLOR4(200, 200, 200, 255);

		if (!isSelected) {
			auto item = entLinks.find(idx);

			if (item != entLinks.end()) {
				if (item->second == 3) {
					color = COLOR4(64, 255, 64, 255);
					isLinked = true;
				}
				else if (item->second == 1) {
					color = COLOR4(255, 255, 32, 255);
					isLinked = true;
				}
				else if (item->second == 2) {
					color = COLOR4(64, 255, 255, 255);
					isLinked = true;
				}
			}
		}

		bool isColored = isSelected || isLinked;

		if (!isColored && !renderAllTags)
			continue;

		vec3 ori = ent->getOrigin() + worldOffset;
		int modelIdx = ent->getBspModelIdx();

		if (modelIdx == -1) {
			if (!(g_settings.render_flags & RENDER_POINT_ENTS))
				continue;

			EntCube* cube = mapRenderer->pointEntRenderer->getEntCube(ent);
			ori += vec3(0, 0, cube->mins.z);
		}
		else {
			if (!(g_settings.render_flags & RENDER_ENTS))
				continue;

			BSPMODEL& model = map->models[modelIdx];
			float oldZ = ori.z;
			ori += model.nMins + (model.nMaxs - model.nMins) * 0.5f;
			ori.z = oldZ + model.nMins.z;
		}

		vec3 tpos = worldToScreen(ori);

		if (tpos.z < 0)
			continue;

		float dist = (ori - cameraOrigin).length();
		if (!isColored && dist > g_settings.zFarMdl) {
			continue;
		}

		if (!isColored && map->pointContents(map->models[0].iHeadnodes[0], cameraOrigin, 0) != CONTENTS_SOLID) {
			// TODO: trace lines are broken. Some faces don't clip the trace
			TraceResult tr;
			map->traceHull(cameraOrigin, ori, 0, &tr);
			//drawLine(cameraOrigin - vec3(0, 0, 4), tr.vecEndPos, COLOR4(255, 0, 0, 255));
			if ((tr.vecEndPos - ori).length() > 32)
				continue;
		}

		TagEnt tag;
		tag.ent = ent;
		tag.idx = idx;
		tag.dist = dist;
		tag.color = color;
		tag.text = tname;
		tag.ori = tpos;
		tag.dist = isColored ? dist * 0.00001f : dist; // always draw highlighted stuff first, less chance of limiting
		tags.push_back(tag);
	}

	// in case some can't be drawn due to text limits
	sort(tags.begin(), tags.end(), [](TagEnt& a, TagEnt& b) {
		return a.dist < b.dist;
		});

	for (TagEnt& tag : tags) {
		gui->addText(Text2D(tag.ori.x, tag.ori.y, tag.text, TEXT2D_ALIGN_CENTER, tag.color));
	}
}


void Editor::setupTransformAxes() {
	{
		moveAxes.dimColor[0] = { 110, 0, 160, 255 };
		moveAxes.dimColor[1] = { 0, 160, 0, 255 };
		moveAxes.dimColor[2] = { 0, 0, 220, 255 };
		moveAxes.dimColor[3] = { 160, 160, 160, 255 };

		moveAxes.hoverColor[0] = { 128, 64, 255, 255 };
		moveAxes.hoverColor[1] = { 64, 255, 64, 255 };
		moveAxes.hoverColor[2] = { 64, 64, 255, 255 };
		moveAxes.hoverColor[3] = { 255, 255, 255, 255 };

		// flipped for HL coords
		moveAxes.model = new cCube[4];
		moveAxes.buffer = new VertexBuffer(g_shaders.color, moveAxes.model, 6 * 6 * 4);
		moveAxes.numAxes = 4;
	}

	{
		scaleAxes.dimColor[0] = { 110, 0, 160, 255 };
		scaleAxes.dimColor[1] = { 0, 0, 220, 255 };
		scaleAxes.dimColor[2] = { 0, 160, 0, 255 };

		scaleAxes.dimColor[3] = { 110, 0, 160, 255 };
		scaleAxes.dimColor[4] = { 0, 0, 220, 255 };
		scaleAxes.dimColor[5] = { 0, 160, 0, 255 };

		scaleAxes.hoverColor[0] = { 128, 64, 255, 255 };
		scaleAxes.hoverColor[1] = { 64, 64, 255, 255 };
		scaleAxes.hoverColor[2] = { 64, 255, 64, 255 };

		scaleAxes.hoverColor[3] = { 128, 64, 255, 255 };
		scaleAxes.hoverColor[4] = { 64, 64, 255, 255 };
		scaleAxes.hoverColor[5] = { 64, 255, 64, 255 };

		// flipped for HL coords
		scaleAxes.model = new cCube[6];
		scaleAxes.buffer = new VertexBuffer(g_shaders.color, scaleAxes.model, 6 * 6 * 6);
		scaleAxes.numAxes = 6;
	}

	{
		vec3 ori = vec3();

		vec3 dirs[3] = {
			vec3(128, 0, 0),
			vec3(0, 128, 0),
			vec3(0, 0, 128),
		};

		COLOR4 colors[3]{
			{ 255, 0, 0, 255 },
			{ 0, 255, 0, 255 },
			{ 0, 0, 255, 255 },
		};

		cVert* verts = new cVert[3 * 2];
		int idx = 0;

		for (int i = 0; i < 3; i++) {
			verts[idx].x = ori.x;
			verts[idx].y = ori.z;
			verts[idx].z = -ori.y;
			verts[idx].c = colors[i];
			idx++;

			vec3 end = ori + dirs[i];
			verts[idx].x = end.x;
			verts[idx].y = end.z;
			verts[idx].z = -end.y;
			verts[idx].c = colors[i];
			idx++;
		}

		originBuffer = new VertexBuffer(g_shaders.color, verts, 3 * 2, true);
		originBuffer->upload();
	}

	glCheckError("creating transform axes");

	updateDragAxes();

	glCheckError("updating transform axes");
}

void Editor::updateDragAxes() {
	Bsp* map = NULL;
	Entity* ent = NULL;
	vec3 mapOffset;

	if (pickInfo.getEnt()) {
		map = mapRenderer->map;
		ent = pickInfo.getEnt();
		mapOffset = mapRenderer->mapOffset;
	}
	else
	{
		return;
	}

	vec3 localCameraOrigin = cameraOrigin - mapOffset;

	vec3 entMin, entMax;
	// set origin of the axes
	if (transformMode == TRANSFORM_SCALE) {
		if (ent != NULL && ent->isBspModel()) {
			map->get_model_vertex_bounds(ent->getBspModelIdx(), entMin, entMax);
			vec3 modelOrigin = entMin + (entMax - entMin) * 0.5f;

			entMax -= modelOrigin;
			entMin -= modelOrigin;

			scaleAxes.origin = modelOrigin;
			if (ent->hasKey("origin")) {
				scaleAxes.origin += parseVector(ent->getKeyvalue("origin"));
			}
		}
	}
	else {
		if (ent != NULL) {
			if (transformTarget == TRANSFORM_ORIGIN) {
				moveAxes.origin = transformedOrigin;
				debugVec0 = transformedOrigin;
			}
			else {
				moveAxes.origin = getEntOrigin(map, ent);
			}
		}
		if (pickInfo.getEntIndex() == 0) {
			moveAxes.origin -= mapOffset;
		}

		if (transformTarget == TRANSFORM_VERTEX) {
			vec3 entOrigin = ent ? ent->getOrigin() : vec3();
			vec3 min(FLT_MAX, FLT_MAX, FLT_MAX);
			vec3 max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			int selectTotal = 0;
			for (int i = 0; i < modelVerts.size(); i++) {
				if (modelVerts[i].selected) {
					vec3 v = modelVerts[i].pos + entOrigin;
					if (v.x < min.x) min.x = v.x;
					if (v.y < min.y) min.y = v.y;
					if (v.z < min.z) min.z = v.z;
					if (v.x > max.x) max.x = v.x;
					if (v.y > max.y) max.y = v.y;
					if (v.z > max.z) max.z = v.z;
					selectTotal++;
				}
			}
			if (selectTotal != 0)
				moveAxes.origin = min + (max - min) * 0.5f;
		}
	}

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	float baseScale = (activeAxes.origin - localCameraOrigin).length() * 0.005f;
	float s = baseScale;
	float s2 = baseScale * 2;
	float d = baseScale * 32;

	// create the meshes
	if (transformMode == TRANSFORM_SCALE) {
		vec3 axisMins[6] = {
			vec3(0, -s, -s) + vec3(entMax.x,0,0), // x+
			vec3(-s, 0, -s) + vec3(0,entMax.y,0), // y+
			vec3(-s, -s, 0) + vec3(0,0,entMax.z), // z+

			vec3(-d, -s, -s) + vec3(entMin.x,0,0), // x-
			vec3(-s, -d, -s) + vec3(0,entMin.y,0), // y-
			vec3(-s, -s, -d) + vec3(0,0,entMin.z)  // z-
		};
		vec3 axisMaxs[6] = {
			vec3(d, s, s) + vec3(entMax.x,0,0), // x+
			vec3(s, d, s) + vec3(0,entMax.y,0), // y+
			vec3(s, s, d) + vec3(0,0,entMax.z), // z+

			vec3(0, s, s) + vec3(entMin.x,0,0), // x-
			vec3(s, 0, s) + vec3(0,entMin.y,0), // y-
			vec3(s, s, 0) + vec3(0,0,entMin.z)  // z-
		};

		scaleAxes.model[0] = cCube(axisMins[0], axisMaxs[0], scaleAxes.dimColor[0]);
		scaleAxes.model[1] = cCube(axisMins[1], axisMaxs[1], scaleAxes.dimColor[1]);
		scaleAxes.model[2] = cCube(axisMins[2], axisMaxs[2], scaleAxes.dimColor[2]);

		scaleAxes.model[3] = cCube(axisMins[3], axisMaxs[3], scaleAxes.dimColor[3]);
		scaleAxes.model[4] = cCube(axisMins[4], axisMaxs[4], scaleAxes.dimColor[4]);
		scaleAxes.model[5] = cCube(axisMins[5], axisMaxs[5], scaleAxes.dimColor[5]);

		// flip to HL coords
		cVert* verts = (cVert*)scaleAxes.model;
		for (int i = 0; i < 6 * 6 * 6; i++) {
			float tmp = verts[i].z;
			verts[i].z = -verts[i].y;
			verts[i].y = tmp;
		}

		// larger mins/maxs so you can be less precise when selecting them
		s *= 4;
		vec3 grabAxisMins[6] = {
			vec3(0, -s, -s) + vec3(entMax.x,0,0), // x+
			vec3(-s, 0, -s) + vec3(0,entMax.y,0), // y+
			vec3(-s, -s, 0) + vec3(0,0,entMax.z), // z+

			vec3(-d, -s, -s) + vec3(entMin.x,0,0), // x-
			vec3(-s, -d, -s) + vec3(0,entMin.y,0), // y-
			vec3(-s, -s, -d) + vec3(0,0,entMin.z)  // z-
		};
		vec3 grabAxisMaxs[6] = {
			vec3(d, s, s) + vec3(entMax.x,0,0), // x+
			vec3(s, d, s) + vec3(0,entMax.y,0), // y+
			vec3(s, s, d) + vec3(0,0,entMax.z), // z+

			vec3(0, s, s) + vec3(entMin.x,0,0), // x-
			vec3(s, 0, s) + vec3(0,entMin.y,0), // y-
			vec3(s, s, 0) + vec3(0,0,entMin.z)  // z-
		};

		for (int i = 0; i < 6; i++) {
			scaleAxes.mins[i] = grabAxisMins[i];
			scaleAxes.maxs[i] = grabAxisMaxs[i];
		}
	}
	else {
		// flipped for HL coords
		moveAxes.model[0] = cCube(vec3(0, -s, -s), vec3(d, s, s), moveAxes.dimColor[0]);
		moveAxes.model[2] = cCube(vec3(-s, 0, -s), vec3(s, d, s), moveAxes.dimColor[2]);
		moveAxes.model[1] = cCube(vec3(-s, -s, 0), vec3(s, s, -d), moveAxes.dimColor[1]);
		moveAxes.model[3] = cCube(vec3(-s2, -s2, -s2), vec3(s2, s2, s2), moveAxes.dimColor[3]);

		// larger mins/maxs so you can be less precise when selecting them
		s *= 4;
		s2 *= 1.5f;

		activeAxes.mins[0] = vec3(0, -s, -s);
		activeAxes.mins[1] = vec3(-s, 0, -s);
		activeAxes.mins[2] = vec3(-s, -s, 0);
		activeAxes.mins[3] = vec3(-s2, -s2, -s2);

		activeAxes.maxs[0] = vec3(d, s, s);
		activeAxes.maxs[1] = vec3(s, d, s);
		activeAxes.maxs[2] = vec3(s, s, d);
		activeAxes.maxs[3] = vec3(s2, s2, s2);
	}


	if (draggingAxis >= 0 && draggingAxis < activeAxes.numAxes) {
		activeAxes.model[draggingAxis].setColor(activeAxes.hoverColor[draggingAxis]);
	}
	else if (hoverAxis >= 0 && hoverAxis < activeAxes.numAxes) {
		activeAxes.model[hoverAxis].setColor(activeAxes.hoverColor[hoverAxis]);
	}
	else if (gui->guiHoverAxis >= 0 && gui->guiHoverAxis < activeAxes.numAxes) {
		activeAxes.model[gui->guiHoverAxis].setColor(activeAxes.hoverColor[gui->guiHoverAxis]);
	}
}

void Editor::updateModelVerts() {

	if (modelVertBuff) {
		delete modelVertBuff;
		delete[] modelVertCubes;
		modelVertBuff = NULL;
		modelVertCubes = NULL;
		modelOriginBuff = NULL;
		scaleTexinfos.clear();
		modelEdges.clear();
		modelVerts.clear();
		modelFaceVerts.clear();
	}

	if (!pickInfo.getEnt() || pickInfo.getModelIndex() <= 0) {
		originSelected = false;
		modelUsesSharedStructures = false;
		updateSelectionSize();
		return;
	}

	Bsp* map = mapRenderer->map;
	int modelIdx = pickInfo.getModelIndex();

	if (modelOriginBuff) {
		delete modelOriginBuff;
	}

	if (pickInfo.getEnt()) {
		transformedOrigin = oldOrigin = pickInfo.getOrigin();
	}

	modelOriginBuff = new VertexBuffer(g_shaders.color, &modelOriginCube, 6 * 6);
	modelOriginBuff->upload();

	updateSelectionSize();

	modelUsesSharedStructures = false;
	for (int idx : pickInfo.getModelIndexes()) {
		modelUsesSharedStructures |= map->does_model_use_shared_structures(idx);
		if (modelUsesSharedStructures)
			break;
	}

	if (!map->is_convex(modelIdx)) {
		return;
	}

	scaleTexinfos = map->getScalableTexinfos(modelIdx);
	map->getModelPlaneIntersectVerts(pickInfo.getModelIndex(), modelVerts); // for vertex manipulation + scaling
	modelFaceVerts = map->getModelVerts(pickInfo.getModelIndex()); // for scaling only

	Solid modelSolid;
	if (!getModelSolid(modelVerts, map, modelSolid)) {
		modelVerts.clear();
		modelFaceVerts.clear();
		scaleTexinfos.clear();
		return;
	};
	modelEdges = modelSolid.hullEdges;

	int numCubes = modelVerts.size() + modelEdges.size();
	modelVertCubes = new cCube[numCubes];
	modelVertBuff = new VertexBuffer(g_shaders.color, modelVertCubes, 6 * 6 * numCubes);
	modelVertBuff->upload();
	//logf("%d intersection points\n", modelVerts.size());
}

void Editor::updateEntConnections() {
	// todo: these shouldn't be here
	updateCullBox();
	updateEntDirectionVectors();
	updateTextureAxes();
	gui->entityReportReselectNeeded = true;

	if (entConnections) {
		delete entConnections;
		delete entConnectionPoints;
		entConnections = NULL;
		entConnectionPoints = NULL;
		entConnectionLinks.clear();
	}

	entLinks.clear();

	if (!(g_settings.render_flags & RENDER_ENT_CONNECTIONS)) {
		return;
	}

	unordered_set<int> testedTargets;

	if (pickInfo.getMap() && pickInfo.getEnt()) {
		Bsp* map = pickInfo.getMap();

		const COLOR4 targetColor = { 255, 255, 0, 255 };
		const COLOR4 callerColor = { 0, 255, 255, 255 };
		const COLOR4 bothColor = { 0, 255, 0, 255 };

		for (int i = 0; i < pickInfo.ents.size(); i++) {
			int entidx = pickInfo.ents[i];
			Entity* self = map->ents[entidx];
			const StringSet& selfNames = self->getAllTargetnames();

			for (int k = 0; k < map->ents.size(); k++) {
				Entity* ent = map->ents[k];

				if (k == entidx)
					continue;

				if (testedTargets.count(k))
					continue;

				const StringSet& tnames = ent->getAllTargetnames();
				bool isTarget = tnames.size() && self->hasTarget(tnames);
				bool isCaller = selfNames.size() && ent->hasTarget(selfNames);

				EntConnection link;
				memset(&link, 0, sizeof(EntConnection));
				link.selfIdx = entidx;
				link.targetIdx = k;

				if (isTarget && isCaller) {
					link.color = bothColor;
					link.dir = 0;
					entLinks[k] = 3;
					entConnectionLinks.push_back(link);
				}
				else if (isTarget) {
					link.color = targetColor;
					link.dir = -1;
					entLinks[k] = 1;
					entConnectionLinks.push_back(link);
				}
				else if (isCaller) {
					link.color = callerColor;
					link.dir = 1;
					entLinks[k] = 2;
					entConnectionLinks.push_back(link);
				}
			}

			testedTargets.insert(entidx);
		}

		if (entConnectionLinks.empty()) {
			return;
		}

		int numVerts = entConnectionLinks.size() * 2;
		int numPoints = entConnectionLinks.size();
		eLinkVert* lines = new eLinkVert[numVerts];
		cCube* points = new cCube[numPoints];

		int idx = 0;
		int cidx = 0;
		float s = 1.5f;
		vec3 extent = vec3(s, s, s);

		for (int i = 0; i < entConnectionLinks.size(); i++) {
			EntConnection& link = entConnectionLinks[i];

			if (link.selfIdx >= map->ents.size() || link.targetIdx >= map->ents.size())
				continue;
			Entity* self = map->ents[link.selfIdx];
			Entity* targ = map->ents[link.targetIdx];

			vec3 srcPos = getEntOrigin(map, self).flip();
			vec3 ori = getEntOrigin(map, targ).flip();
			float dist = (ori - srcPos).length();
			points[cidx++] = cCube(ori - extent, ori + extent, link.color);
			lines[idx++] = eLinkVert(srcPos, link.color, 0, link.dir);
			lines[idx++] = eLinkVert(ori, link.color, dist, link.dir);
		}

		entConnections = new VertexBuffer(g_shaders.elink, lines, numVerts, true);
		entConnectionPoints = new VertexBuffer(g_shaders.color, points, numPoints * 6 * 6, true);
		entConnections->upload();
		entConnectionPoints->upload();
	}
}

void Editor::updateEntConnectionPositions() {
	// todo: these shouldn't be here
	updateCullBox();
	updateEntDirectionVectors();
	updateTextureAxes();

	if (!entConnections) {
		return;
	}

	Bsp* map = pickInfo.getMap();

	eLinkVert* lines = (eLinkVert*)entConnections->data;
	cCube* points = (cCube*)entConnectionPoints->data;

	const float s = 1.5f;
	const vec3 extent = vec3(s, s, s);

	for (int k = 0; k < entConnectionLinks.size(); k++) {
		EntConnection& link = entConnectionLinks[k];

		if (link.selfIdx >= map->ents.size() || link.targetIdx >= map->ents.size())
			continue;
		Entity* self = map->ents[link.selfIdx];
		Entity* targ = map->ents[link.targetIdx];

		vec3 srcPos = getEntOrigin(map, self).flip();
		vec3 dstPos = getEntOrigin(map, targ).flip();

		int idx = k * 2;
		lines[idx].x = srcPos.x;
		lines[idx].y = srcPos.y;
		lines[idx].z = srcPos.z;
		lines[idx + 1].x = dstPos.x;
		lines[idx + 1].y = dstPos.y;
		lines[idx + 1].z = dstPos.z;

		points[k] = cCube(dstPos - extent, dstPos + extent, link.color);
	}

	entConnections->upload();
	entConnectionPoints->upload();
}
