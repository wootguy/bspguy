#include "Gui.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Renderer.h"
#include <lodepng.h>
#include <algorithm>
#include "BspMerger.h"
#include "filedialog/ImFileDialog.h"
// embedded binary data
#include "fonts/robotomono.h"
#include "fonts/robotomedium.h"
#include "icons/object.h"
#include "icons/face.h"

#include <Windows.h>

float g_tooltip_delay = 0.6f; // time in seconds before showing a tooltip

static bool filterNeeded = true;

std::string iniPath;

Gui::Gui(Renderer* app) {
	iniPath = getConfigDir() + "imgui.ini";
	this->app = app;
	init();
}

void Gui::init() {
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	io.IniFilename = iniPath.c_str();

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(app->window, true);
	const char* glsl_version = "#version 130";
	ImGui_ImplOpenGL3_Init(glsl_version);

	//ImGui::StyleColorsLight();


	// ImFileDialog requires you to set the CreateTexture and DeleteTexture
	ifd::FileDialog::Instance().CreateTexture = [](unsigned char* data, int w, int h, char fmt) -> void* {
		GLuint tex;

		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, (fmt == 0) ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);

		return (void*)(uint64_t)tex;
	};
	ifd::FileDialog::Instance().DeleteTexture = [](void* tex) {
		GLuint texID = (GLuint)((uintptr_t)tex);
		glDeleteTextures(1, &texID);
	};


	loadFonts();

	io.ConfigWindowsMoveFromTitleBarOnly = true;

	clearLog();

	// load icons
	unsigned char* icon_data = NULL;
	unsigned int w, h;

	lodepng_decode32(&icon_data, &w, &h, object_icon, sizeof(object_icon));
	objectIconTexture = new Texture(w, h, icon_data);
	objectIconTexture->upload(GL_RGBA);

	lodepng_decode32(&icon_data, &w, &h, face_icon, sizeof(face_icon));
	faceIconTexture = new Texture(w, h, icon_data);
	faceIconTexture->upload(GL_RGBA);
}

void Gui::draw() {
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	drawMenuBar();

	drawFpsOverlay();
	drawToolbar();
	drawStatusMessage();

	if (showDebugWidget) {
		drawDebugWidget();
	}
	if (showKeyvalueWidget) {
		drawKeyvalueEditor();
	}
	if (showTransformWidget) {
		drawTransformWidget();
	}
	if (showLogWidget) {
		drawLog();
	}
	if (showSettingsWidget) {
		drawSettings();
	}
	if (showHelpWidget) {
		drawHelp();
	}
	if (showAboutWidget) {
		drawAbout();
	}
	if (showImportMapWidget) {
		drawImportMapWidget();
	}
	if (showMergeMapWidget) {
		drawMergeWindow();
	}
	if (showLimitsWidget) {
		drawLimits();
	}
	if (showTextureWidget) {
		drawTextureTool();
	}
	if (showLightmapEditorWidget) {
		drawLightMapTool();
	}
	if (showEntityReport) {
		drawEntityReport();
	}
	if (showGOTOWidget) {
		drawGOTOWidget();
	}

	if (app->pickMode == PICK_OBJECT) {
		if (contextMenuEnt != -1) {
			ImGui::OpenPopup("ent_context");
			contextMenuEnt = -1;
		}
		if (emptyContextMenu) {
			emptyContextMenu = 0;
			ImGui::OpenPopup("empty_context");
		}
	}
	else {
		if (contextMenuEnt != -1 || emptyContextMenu) {
			emptyContextMenu = 0;
			contextMenuEnt = -1;
			ImGui::OpenPopup("face_context");
		}
	}


	draw3dContextMenus();

	// Rendering
	ImGui::Render();
	glViewport(0, 0, app->windowWidth, app->windowHeight);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	if (shouldReloadFonts) {
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		shouldReloadFonts = false;

		ImGui_ImplOpenGL3_DestroyFontsTexture();
		io.Fonts->Clear();

		loadFonts();

		io.Fonts->Build();
		ImGui_ImplOpenGL3_CreateFontsTexture();
	}
}

void Gui::openContextMenu(int entIdx) {
	if (entIdx == -1) {
		emptyContextMenu = 1;
	}
	contextMenuEnt = entIdx;
}

void Gui::copyTexture() {
	Bsp* map = app->getSelectedMap();
	if (!map) {
		return;
	}
	BSPTEXTUREINFO& texinfo = map->texinfos[map->faces[app->pickInfo.faceIdx].iTextureInfo];
	copiedMiptex = texinfo.iMiptex;
}

void Gui::pasteTexture() {
	refreshSelectedFaces = true;
}

void Gui::copyLightmap() {
	Bsp* map = app->getSelectedMap();

	if (!map) {
		return;
	}

	copiedLightmapFace = app->pickInfo.faceIdx;

	int size[2];
	GetFaceLightmapSize(map, app->pickInfo.faceIdx, size);
	copiedLightmap.width = size[0];
	copiedLightmap.height = size[1];
	copiedLightmap.layers = map->lightmap_count(app->pickInfo.faceIdx);
	//copiedLightmap.luxelFlags = new unsigned char[size[0] * size[1]];
	//qrad_get_lightmap_flags(map, app->pickInfo.faceIdx, copiedLightmap.luxelFlags);
}

void Gui::pasteLightmap() {
	Bsp* map = app->getSelectedMap();
	if (!map) {
		return;
	}
	int size[2];
	GetFaceLightmapSize(map, app->pickInfo.faceIdx, size);
	LIGHTMAP dstLightmap;
	dstLightmap.width = size[0];
	dstLightmap.height = size[1];
	dstLightmap.layers = map->lightmap_count(app->pickInfo.faceIdx);

	if (dstLightmap.width != copiedLightmap.width || dstLightmap.height != copiedLightmap.height) {
		logf("WARNING: lightmap sizes don't match (%dx%d != %d%d)",
			copiedLightmap.width,
			copiedLightmap.height,
			dstLightmap.width,
			dstLightmap.height);
		// TODO: resize the lightmap, or maybe just shift if the face is the same size
	}

	BSPFACE& src = map->faces[copiedLightmapFace];
	BSPFACE& dst = map->faces[app->pickInfo.faceIdx];
	dst.nLightmapOffset = src.nLightmapOffset;
	memcpy(dst.nStyles, src.nStyles, 4);

	map->getBspRender()->reloadLightmaps();
}


void ExportModel(Bsp* map, unsigned int id, int ExportType)
{
	map->update_ent_lump();

	Bsp tmpMap = Bsp(map->path);
	tmpMap.is_model = true;

	BSPMODEL tmpModel = map->models[id];

	Entity* tmpEnt = new Entity(*map->ents[0]);

	vec3 EntOffset = vec3();

	for (int i = 0; i < map->ents.size(); i++)
	{
		if (map->ents[i]->getBspModelIdx() == id)
		{
			EntOffset = map->ents[i]->getOrigin();
			break;
		}
	}

	vec3 modelOrigin = map->get_model_center(id);

	tmpMap.modelCount = 1;
	tmpMap.models[0] = tmpModel;

	// Move model to 0 0 0
	tmpMap.move(-modelOrigin, 0, true);

	for (int i = 1; i < tmpMap.ents.size(); i++)
	{
		delete tmpMap.ents[i];
	}

	tmpMap.ents.clear();

	tmpEnt->setOrAddKeyvalue("origin", vec3(0, 0, 0).toKeyvalueString());
	tmpEnt->setOrAddKeyvalue("compiler", g_version_string);
	tmpEnt->setOrAddKeyvalue("message", "bsp model");
	tmpMap.ents.push_back(tmpEnt);

	tmpMap.update_ent_lump();

	tmpMap.lumps[LUMP_MODELS] = (unsigned char*)tmpMap.models;
	tmpMap.header.lump[LUMP_MODELS].nLength = sizeof(BSPMODEL);
	tmpMap.update_lump_pointers();


	STRUCTCOUNT removed = tmpMap.remove_unused_model_structures(ExportType != 1);
	if (!removed.allZero())
		removed.print_delete_stats(1);

	if (!tmpMap.validate())
	{
		logf("Tried to fix model by adding emply missing data %d\n", id);
		int markid = 0;
		for (unsigned int i = 0; i < tmpMap.leafCount; i++)
		{
			BSPLEAF& tmpLeaf = tmpMap.leaves[i];
			tmpLeaf.iFirstMarkSurface = markid;
			markid += tmpLeaf.nMarkSurfaces;
		}

		while (tmpMap.models[0].nVisLeafs >= (int)tmpMap.leafCount)
			tmpMap.create_leaf(ExportType == 2 ? CONTENTS_WATER : CONTENTS_EMPTY);

		//tmpMap.lumps[LUMP_LEAVES] = (unsigned char*)tmpMap.leaves;
		tmpMap.update_lump_pointers();
	}

	if (!tmpMap.validate())
	{
		logf("Failed to export model %d\n", id);
		return;
	}

	BSPNODE* tmpNode = new BSPNODE[2];
	tmpNode[0].firstFace = tmpMap.models[0].iFirstFace;
	tmpNode[0].iPlane = tmpMap.faces[tmpNode[0].firstFace].iPlane;
	tmpNode[0].nFaces = tmpMap.models[0].nFaces;
	tmpNode[0].nMaxs[0] = (short)tmpMap.models[0].nMaxs[0];
	tmpNode[0].nMaxs[1] = (short)tmpMap.models[0].nMaxs[1];
	tmpNode[0].nMaxs[2] = (short)tmpMap.models[0].nMaxs[2];
	tmpNode[0].nMins[0] = (short)tmpMap.models[0].nMins[0];
	tmpNode[0].nMins[1] = (short)tmpMap.models[0].nMins[1];
	tmpNode[0].nMins[2] = (short)tmpMap.models[0].nMins[2];

	tmpNode[1].firstFace = tmpMap.models[0].iFirstFace;
	tmpNode[1].iPlane = tmpMap.faces[tmpNode[1].firstFace].iPlane;
	tmpNode[1].nFaces = tmpMap.models[0].nFaces;
	tmpNode[1].nMaxs[0] = (short)tmpMap.models[0].nMaxs[0];
	tmpNode[1].nMaxs[1] = (short)tmpMap.models[0].nMaxs[1];
	tmpNode[1].nMaxs[2] = (short)tmpMap.models[0].nMaxs[2];
	tmpNode[1].nMins[0] = (short)tmpMap.models[0].nMins[0];
	tmpNode[1].nMins[1] = (short)tmpMap.models[0].nMins[1];
	tmpNode[1].nMins[2] = (short)tmpMap.models[0].nMins[2];

	short sharedSolidLeaf = 0;


	short anyEmptyLeaf = -1;
	for (unsigned int i = 0; i < tmpMap.leafCount; i++) {
		if (tmpMap.leaves[i].nContents == CONTENTS_EMPTY) {
			anyEmptyLeaf = i;
			break;
		}
	}

	if (anyEmptyLeaf < 0)
	{
		anyEmptyLeaf = tmpMap.create_leaf(CONTENTS_EMPTY);
	}

	if (ExportType == 2)
	{
		tmpMap.leaves[0].nContents = CONTENTS_WATER;
	}

	tmpNode[0].iChildren[0] = ~sharedSolidLeaf;
	tmpNode[0].iChildren[1] = ~anyEmptyLeaf;
	tmpNode[1].iChildren[0] = ~sharedSolidLeaf;
	tmpNode[1].iChildren[1] = ~anyEmptyLeaf;

	tmpMap.lumps[LUMP_NODES] = (unsigned char*)&tmpNode[0];
	tmpMap.header.lump[LUMP_NODES].nLength = sizeof(BSPNODE) * 2;
	tmpMap.update_lump_pointers();

	tmpMap.models[0].iHeadnodes[0] = 1;

	createDir(GetWorkDir());
	logf("Export model %d to %s\n", id, (GetWorkDir() + "model" + std::to_string(id) + ".bsp").c_str());
	tmpMap.write(GetWorkDir() + "model" + std::to_string(id) + ".bsp");
}

void Gui::draw3dContextMenus() {
	ImGuiContext& g = *GImGui;

	Bsp* map = app->getSelectedMap();

	if (map && app->originHovered) {
		if (ImGui::BeginPopup("ent_context") || ImGui::BeginPopup("empty_context")) {
			if (ImGui::MenuItem("Center", "")) {
				app->transformedOrigin = app->getEntOrigin(map, app->pickInfo.ent);
				app->applyTransform();
				app->pickCount++; // force gui refresh
			}

			if (app->pickInfo.ent && ImGui::BeginMenu("Align")) {
				BSPMODEL& model = map->models[app->pickInfo.ent->getBspModelIdx()];

				if (ImGui::MenuItem("Top")) {
					app->transformedOrigin.z = app->oldOrigin.z + model.nMaxs.z;
					app->applyTransform();
					app->pickCount++;
				}
				if (ImGui::MenuItem("Bottom")) {
					app->transformedOrigin.z = app->oldOrigin.z + model.nMins.z;
					app->applyTransform();
					app->pickCount++;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Left")) {
					app->transformedOrigin.x = app->oldOrigin.x + model.nMins.x;
					app->applyTransform();
					app->pickCount++;
				}
				if (ImGui::MenuItem("Right")) {
					app->transformedOrigin.x = app->oldOrigin.x + model.nMaxs.x;
					app->applyTransform();
					app->pickCount++;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Back")) {
					app->transformedOrigin.y = app->oldOrigin.y + model.nMins.y;
					app->applyTransform();
					app->pickCount++;
				}
				if (ImGui::MenuItem("Front")) {
					app->transformedOrigin.y = app->oldOrigin.y + model.nMaxs.y;
					app->applyTransform();
					app->pickCount++;
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		return;
	}

	if (app->pickMode == PICK_OBJECT) {
		if (ImGui::BeginPopup("ent_context"))
		{
			if (ImGui::MenuItem("Cut", "Ctrl+X")) {
				app->cutEnt();
			}
			if (ImGui::MenuItem("Copy", "Ctrl+C")) {
				app->copyEnt();
			}
			if (ImGui::MenuItem("Delete", "Del")) {
				app->deleteEnt();
			}
			ImGui::Separator();
			if (app->pickInfo.modelIdx > 0) {
				BSPMODEL& model = map->models[app->pickInfo.modelIdx];

				if (ImGui::BeginMenu("Create Hull", !app->invalidSolid && app->isTransformableSolid)) {
					if (ImGui::MenuItem("Clipnodes")) {
						map->regenerate_clipnodes(app->pickInfo.modelIdx, -1);
						checkValidHulls();
						logf("Regenerated hulls 1-3 on model %d\n", app->pickInfo.modelIdx);
					}

					ImGui::Separator();

					for (int i = 1; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str())) {
							map->regenerate_clipnodes(app->pickInfo.modelIdx, i);
							checkValidHulls();
							logf("Regenerated hull %d on model %d\n", i, app->pickInfo.modelIdx);
						}
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Delete Hull", !app->isLoading)) {
					if (ImGui::MenuItem("All Hulls")) {
						map->delete_hull(0, app->pickInfo.modelIdx, -1);
						map->delete_hull(1, app->pickInfo.modelIdx, -1);
						map->delete_hull(2, app->pickInfo.modelIdx, -1);
						map->delete_hull(3, app->pickInfo.modelIdx, -1);
						map->getBspRender()->refreshModel(app->pickInfo.modelIdx);
						checkValidHulls();
						logf("Deleted all hulls on model %d\n", app->pickInfo.modelIdx);
					}
					if (ImGui::MenuItem("Clipnodes")) {
						map->delete_hull(1, app->pickInfo.modelIdx, -1);
						map->delete_hull(2, app->pickInfo.modelIdx, -1);
						map->delete_hull(3, app->pickInfo.modelIdx, -1);
						map->getBspRender()->refreshModelClipnodes(app->pickInfo.modelIdx);
						checkValidHulls();
						logf("Deleted hulls 1-3 on model %d\n", app->pickInfo.modelIdx);
					}

					ImGui::Separator();

					for (int i = 0; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), 0, false, isHullValid)) {
							map->delete_hull(i, app->pickInfo.modelIdx, -1);
							checkValidHulls();
							if (i == 0)
								map->getBspRender()->refreshModel(app->pickInfo.modelIdx);
							else
								map->getBspRender()->refreshModelClipnodes(app->pickInfo.modelIdx);
							logf("Deleted hull %d on model %d\n", i, app->pickInfo.modelIdx);
						}
					}

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Simplify Hull", !app->isLoading)) {
					if (ImGui::MenuItem("Clipnodes")) {
						map->simplify_model_collision(app->pickInfo.modelIdx, 1);
						map->simplify_model_collision(app->pickInfo.modelIdx, 2);
						map->simplify_model_collision(app->pickInfo.modelIdx, 3);
						map->getBspRender()->refreshModelClipnodes(app->pickInfo.modelIdx);
						logf("Replaced hulls 1-3 on model %d with a box-shaped hull\n", app->pickInfo.modelIdx);
					}

					ImGui::Separator();

					for (int i = 1; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), 0, false, isHullValid)) {
							map->simplify_model_collision(app->pickInfo.modelIdx, 1);
							map->getBspRender()->refreshModelClipnodes(app->pickInfo.modelIdx);
							logf("Replaced hull %d on model %d with a box-shaped hull\n", i, app->pickInfo.modelIdx);
						}
					}

					ImGui::EndMenu();
				}

				bool canRedirect = model.iHeadnodes[1] != model.iHeadnodes[2] || model.iHeadnodes[1] != model.iHeadnodes[3];

				if (ImGui::BeginMenu("Redirect Hull", canRedirect && !app->isLoading)) {
					for (int i = 1; i < MAX_MAP_HULLS; i++) {
						if (ImGui::BeginMenu(("Hull " + std::to_string(i)).c_str())) {

							for (int k = 1; k < MAX_MAP_HULLS; k++) {
								if (i == k)
									continue;

								bool isHullValid = model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] != model.iHeadnodes[i];

								if (ImGui::MenuItem(("Hull " + std::to_string(k)).c_str(), 0, false, isHullValid)) {
									model.iHeadnodes[i] = model.iHeadnodes[k];
									map->getBspRender()->refreshModelClipnodes(app->pickInfo.modelIdx);
									checkValidHulls();
									logf("Redirected hull %d to hull %d on model %d\n", i, k, app->pickInfo.modelIdx);
								}
							}

							ImGui::EndMenu();
						}
					}

					ImGui::EndMenu();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Duplicate BSP model", 0, false, !app->isLoading)) {
					DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", app->pickInfo);
					command->execute();
					app->pushUndoCommand(command);
				}
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Create a copy of this BSP model and assign to this entity.\n\nThis lets you edit the model for this entity without affecting others.");
					ImGui::EndTooltip();
				}
				if (ImGui::MenuItem("Export .bsp MODEL(true collision)", 0, false, !app->isLoading)) {
					if (app->pickInfo.modelIdx)
					{
						ExportModel(map, app->pickInfo.modelIdx, 0);
					}
				}
				/*if (ImGui::MenuItem("Export .bsp WATER(true collision)", 0, false, !app->isLoading)) {
					if (app->pickInfo.modelIdx)
					{
						ExportModel(map, app->pickInfo.modelIdx, 2);
					}
				}*/
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Create .bsp file with single model. It can be imported to another map.");
					ImGui::EndTooltip();
				}
			}

			if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "G")) {
				if (!app->movingEnt)
					app->grabEnt();
				else {
					app->ungrabEnt();
				}
			}
			if (ImGui::MenuItem("Transform", "Ctrl+M")) {
				showTransformWidget = !showTransformWidget;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Properties", "Alt+Enter")) {
				showKeyvalueWidget = !showKeyvalueWidget;
			}


			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("empty_context"))
		{
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, app->copiedEnt)) {
				app->pasteEnt(false);
			}
			if (ImGui::MenuItem("Paste at original origin", 0, false, app->copiedEnt)) {
				app->pasteEnt(true);
			}

			ImGui::EndPopup();
		}
	}
	else if (app->pickMode == PICK_FACE) {
		if (map && ImGui::BeginPopup("face_context"))
		{
			if (ImGui::MenuItem("Copy texture", "Ctrl+C")) {
				copyTexture();
			}
			if (ImGui::MenuItem("Paste texture", "Ctrl+V", false, copiedMiptex >= 0 && (unsigned int)copiedMiptex < map->textureCount)) {
				pasteTexture();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Copy lightmap", "(WIP)")) {
				copyLightmap();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Only works for faces with matching sizes/extents,\nand the lightmap might get shifted.");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Paste lightmap", "", false, copiedLightmapFace >= 0 && (unsigned int)copiedLightmapFace < map->faceCount)) {
				pasteLightmap();
			}

			ImGui::EndPopup();
		}
	}
}

bool ExportWad(Bsp* map)
{
	bool retval = true;
	if (map->textureCount > 0)
	{
		Wad* tmpWad = new Wad(map->path);
		std::vector<WADTEX*> tmpWadTex;
		for (unsigned int i = 0; i < map->textureCount; i++) {
			int oldOffset = ((int*)map->textures)[i + 1];
			BSPMIPTEX* bspTex = (BSPMIPTEX*)(map->textures + oldOffset);
			if (bspTex->nOffsets[0] == -1 || bspTex->nOffsets[0] == 0)
				continue;
			WADTEX* oldTex = new WADTEX(bspTex);
			tmpWadTex.push_back(oldTex);
		}
		if (tmpWadTex.size() > 0)
		{
			createDir(GetWorkDir());
			tmpWad->write(GetWorkDir() + map->name + ".wad", &tmpWadTex[0], tmpWadTex.size());
		}
		else
		{
			retval = false;
			logf("Not found any textures in bsp file.");
		}
		for (int i = 0; i < tmpWadTex.size(); i++) {
			if (tmpWadTex[i])
				delete tmpWadTex[i];
		}
		tmpWadTex.clear();
		delete tmpWad;
	}
	else
	{
		retval = false;
		logf("No textures for export.\n");
	}
	return retval;
}

void ImportWad(Bsp* map, Renderer* app, std::string path)
{
	Wad tmpWad = Wad(path);

	if (!tmpWad.readInfo())
	{
		logf("Parsing wad file failed!\n");
	}
	else
	{
		for (int i = 0; i < tmpWad.numTex; i++)
		{
			WADTEX* wadTex = tmpWad.readTexture(i);
			int lastMipSize = (wadTex->nWidth / 8) * (wadTex->nHeight / 8);

			COLOR3* palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + 2 - 40);
			unsigned char* src = wadTex->data;

			int sz = wadTex->nWidth * wadTex->nHeight;
			COLOR3* imageData = new COLOR3[sz];

			for (int k = 0; k < sz; k++) {
				imageData[k] = palette[src[k]];
			}
			map->add_texture(wadTex->szName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);

			delete[] imageData;
			delete wadTex;
		}
		for (int i = 0; i < app->mapRenderers.size(); i++) {
			app->mapRenderers[i]->reloadTextures();
		}
	}
}


void Gui::drawMenuBar() {
	ImGuiContext& g = *GImGui;
	ImGui::BeginMainMenuBar();
	Bsp* map = app->getSelectedMap();


	if (ifd::FileDialog::Instance().IsDone("WadOpenDialog")) {
		if (ifd::FileDialog::Instance().HasResult()) {
			std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
			for (int i = 0; i < map->ents.size(); i++) {
				if (map->ents[i]->keyvalues["classname"] == "worldspawn") {
					std::vector<std::string> wadNames = splitString(map->ents[i]->keyvalues["wad"], ";");
					std::string newWadNames;
					for (int k = 0; k < wadNames.size(); k++) {
						if (wadNames[k].find(res.filename().string()) == std::string::npos)
							newWadNames += wadNames[k] + ";";
					}
					map->ents[i]->keyvalues["wad"] = newWadNames;
					break;
				}
			}
			app->updateEnts();
			ImportWad(map, app, res.string());
			app->reloadBspModels();
			g_settings.lastdir = res.parent_path().string();
		}
		ifd::FileDialog::Instance().Close();
	}

	if (ifd::FileDialog::Instance().IsDone("MapOpenDialog")) {
		if (ifd::FileDialog::Instance().HasResult()) {
			std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
			this->app->clearMaps();
			this->app->addMap(new Bsp(res.string()));
			g_settings.lastdir = res.parent_path().string();
		}
		ifd::FileDialog::Instance().Close();
	}

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", NULL, false, !app->isLoading)) {

			if (map)
			{
				map->update_ent_lump();
				map->update_lump_pointers();
				map->write(map->path);
			}
		}

		if (ImGui::MenuItem("Open", NULL, false, !app->isLoading)) {
			filterNeeded = true;
			ifd::FileDialog::Instance().Open("MapOpenDialog", "Open a map", "Map file (*.bsp){.bsp},.*", false, g_settings.lastdir);
		}


		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Open map in new window.");
			ImGui::EndTooltip();
		}


		if (ImGui::MenuItem("Close All")) {
			filterNeeded = true;
			if (map)
			{
				this->app->clearMaps();
				app->pickInfo = PickInfo();
			}
		}

		if (ImGui::BeginMenu("Export", !app->isLoading)) {
			if (ImGui::MenuItem("Entity file", NULL)) {
				if (map)
				{
					logf("Export entities: %s%s\n", GetWorkDir().c_str(), (map->name + ".ent").c_str());
					createDir(GetWorkDir());
					std::ofstream entFile(GetWorkDir() + (map->name + ".ent"), std::ios::trunc);
					map->update_ent_lump();
					if (map->header.lump[LUMP_ENTITIES].nLength > 0)
					{
						std::string entities = std::string(map->lumps[LUMP_ENTITIES], map->lumps[LUMP_ENTITIES] + map->header.lump[LUMP_ENTITIES].nLength - 1);
						entFile.write(entities.c_str(), entities.size());
					}
				}
			}
			if (ImGui::MenuItem("Embedded textures (.wad)", NULL)) {
				if (map)
				{
					logf("Export wad: %s%s\n", GetWorkDir().c_str(), (map->name + ".wad").c_str());
					if (ExportWad(map))
					{
						logf("Remove all embedded textures\n");
						map->delete_embedded_textures();
						if (map->ents.size())
						{
							std::string wadstr = map->ents[0]->keyvalues["wad"];
							if (wadstr.find(map->name + ".wad" + ";") == std::string::npos)
							{
								map->ents[0]->keyvalues["wad"] += map->name + ".wad" + ";";
							}
						}
					}
				}
			}

			if (ImGui::MenuItem("Wavefront (.obj) [WIP]", NULL)) {
				if (map)
				{
					map->ExportToObjWIP(GetWorkDir());
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export map geometry without textures");
				ImGui::EndTooltip();
			}

			if (ImGui::BeginMenu(".bsp MODEL with true collision")) {
				if (map)
				{
					if (ImGui::BeginMenu((std::string("Map ") + map->name + ".bsp").c_str())) {
						for (unsigned int i = 0; i < map->modelCount; i++)
						{
							if (ImGui::MenuItem(("Export Model" + std::to_string(i) + ".bsp").c_str(), NULL, app->pickInfo.modelIdx == i))
							{
								ExportModel(map, i, 0);
							}
						}
						ImGui::EndMenu();
					}
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Import", !app->isLoading)) {

			if (ImGui::MenuItem(".bsp model as func_breakable", NULL)) {
				showImportMapWidget_Type = SHOW_IMPORT_MODEL;
				showImportMapWidget = !showImportMapWidget;
			}

			if (ImGui::MenuItem("Entity file", NULL)) {
				if (map)
				{
					logf("Import entities from: %s%s\n", GetWorkDir().c_str(), (map->name + ".ent").c_str());
					if (fileExists(GetWorkDir() + (map->name + ".ent")))
					{
						int len;
						char* newlump = loadFile(GetWorkDir() + (map->name + ".ent"), len);
						map->replace_lump(LUMP_ENTITIES, newlump, len);
						map->load_ents();
						for (int i = 0; i < app->mapRenderers.size(); i++) {
							BspRenderer* mapRender = app->mapRenderers[i];
							mapRender->reload();
						}
					}
					else
					{
						logf("Error! No file!\n");
					}
				}
			}



			if (ImGui::MenuItem("Merge with .wad", NULL)) {
				if (map)
				{
					ifd::FileDialog::Instance().Open("WadOpenDialog", "Open a wad", "Wad file (*.wad){.wad},.*", false, g_settings.lastdir);
				}

				if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
					ImGui::BeginTooltip();
					char embtextooltip[256];
					snprintf(embtextooltip, sizeof(embtextooltip), "Embeds textures from %s%s", GetWorkDir().c_str(), (map->name + ".wad").c_str());
					ImGui::TextUnformatted(embtextooltip);
					ImGui::EndTooltip();
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Test")) {
			if (!map || !dirExists(g_settings.gamedir + "/svencoop_addon/maps/"))
			{
				logf("Failed. No svencoop directory found.\n");
			}
			else
			{
				std::string mapPath = g_settings.gamedir + "/svencoop_addon/maps/" + map->name + ".bsp";
				std::string entPath = g_settings.gamedir + "/svencoop_addon/scripts/maps/bspguy/maps/" + map->name + ".ent";

				map->update_ent_lump(true); // strip nodes before writing (to skip slow node graph generation)
				map->write(mapPath);
				map->update_ent_lump(false); // add the nodes back in for conditional loading in the ent file

				std::ofstream entFile(entPath, std::ios::trunc);
				if (entFile.is_open()) {
					logf("Writing %s\n", entPath.c_str());
					entFile.write((const char*)map->lumps[LUMP_ENTITIES], map->header.lump[LUMP_ENTITIES].nLength - 1);
				}
				else {
					logf("Failed to open ent file for writing:\n%s\n", entPath.c_str());
					logf("Check that the directories in the path exist, and that you have permission to write in them.\n");
				}
			}
		}

		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Saves the .bsp and .ent file to your svencoop_addon folder.\n\nAI nodes will be stripped to skip node graph generation.\n");
			ImGui::EndTooltip();
		}

		if (ImGui::MenuItem("Reload", 0, false, !app->isLoading)) {
			app->reloadMaps();
		}
		if (ImGui::MenuItem("Validate")) {
			if (map)
			{
				logf("Validating %s\n", map->name.c_str());
				map->validate();
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Settings", NULL)) {
			if (!showSettingsWidget) {
				reloadSettings = true;
			}
			showSettingsWidget = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Exit", NULL)) {
			g_settings.save();
			glfwTerminate();
			std::exit(0);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit")) {
		Command* undoCmd = !app->undoHistory.empty() ? app->undoHistory[app->undoHistory.size() - 1] : NULL;
		Command* redoCmd = !app->redoHistory.empty() ? app->redoHistory[app->redoHistory.size() - 1] : NULL;
		std::string undoTitle = undoCmd ? "Undo " + undoCmd->desc : "Can't undo";
		std::string redoTitle = redoCmd ? "Redo " + redoCmd->desc : "Can't redo";
		bool canUndo = undoCmd && (!app->isLoading || undoCmd->allowedDuringLoad);
		bool canRedo = redoCmd && (!app->isLoading || redoCmd->allowedDuringLoad);
		bool entSelected = app->pickInfo.ent;
		bool mapSelected = map;
		bool nonWorldspawnEntSelected = entSelected && app->pickInfo.entIdx != 0;

		if (ImGui::MenuItem(undoTitle.c_str(), "Ctrl+Z", false, canUndo)) {
			app->undo();
		}
		else if (ImGui::MenuItem(redoTitle.c_str(), "Ctrl+Y", false, canRedo)) {
			app->redo();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Cut", "Ctrl+X", false, nonWorldspawnEntSelected)) {
			app->cutEnt();
		}
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, nonWorldspawnEntSelected)) {
			app->copyEnt();
		}
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, mapSelected && app->copiedEnt)) {
			app->pasteEnt(false);
		}
		if (ImGui::MenuItem("Paste at original origin", 0, false, entSelected && app->copiedEnt)) {
			app->pasteEnt(true);
		}
		if (ImGui::MenuItem("Delete", "Del", false, nonWorldspawnEntSelected)) {
			app->deleteEnt();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Duplicate BSP model", 0, false, !app->isLoading && nonWorldspawnEntSelected)) {
			DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", app->pickInfo);
			command->execute();
			app->pushUndoCommand(command);
		}
		if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "G", false, nonWorldspawnEntSelected)) {
			if (!app->movingEnt)
				app->grabEnt();
			else {
				app->ungrabEnt();
			}
		}
		if (ImGui::MenuItem("Transform", "Ctrl+M", false, entSelected)) {
			showTransformWidget = !showTransformWidget;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Properties", "Alt+Enter", false, entSelected)) {
			showKeyvalueWidget = !showKeyvalueWidget;
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Map"))
	{
		if (ImGui::MenuItem("Entity Report", NULL)) {
			showEntityReport = true;
		}

		if (ImGui::MenuItem("Show Limits", NULL)) {
			showLimitsWidget = true;
		}

		ImGui::Separator();


		if (ImGui::MenuItem("Clean", 0, false, !app->isLoading && map)) {
			CleanMapCommand* command = new CleanMapCommand("Clean " + map->name, app->getSelectedMapId(), app->undoLumpState);
			g_app->saveLumpState(map, 0xffffffff, false);
			command->execute();
			app->pushUndoCommand(command);
		}

		if (ImGui::MenuItem("Optimize", 0, false, !app->isLoading && map)) {
			OptimizeMapCommand* command = new OptimizeMapCommand("Optimize " + map->name, app->getSelectedMapId(), app->undoLumpState);
			g_app->saveLumpState(map, 0xffffffff, false);
			command->execute();
			app->pushUndoCommand(command);
		}

		ImGui::Separator();

		bool hasAnyCollision = anyHullValid[1] || anyHullValid[2] || anyHullValid[3];

		if (ImGui::BeginMenu("Delete Hull", hasAnyCollision && !app->isLoading && map)) {
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), NULL, false, anyHullValid[i])) {
					//for (int k = 0; k < app->mapRenderers.size(); k++) {
					//	Bsp* map = app->mapRenderers[k]->map;
					map->delete_hull(i, -1);
					map->getBspRender()->reloadClipnodes();
					//	app->mapRenderers[k]->reloadClipnodes();
					logf("Deleted hull %d in map %s\n", i, map->name.c_str());
					//}
					checkValidHulls();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Redirect Hull", hasAnyCollision && !app->isLoading && map)) {
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				if (ImGui::BeginMenu(("Hull " + std::to_string(i)).c_str())) {
					for (int k = 1; k < MAX_MAP_HULLS; k++) {
						if (i == k)
							continue;
						if (ImGui::MenuItem(("Hull " + std::to_string(k)).c_str(), "", false, anyHullValid[k])) {
							//for (int j = 0; j < app->mapRenderers.size(); j++) {
							//	Bsp* map = app->mapRenderers[j]->map;
							map->delete_hull(i, k);
							map->getBspRender()->reloadClipnodes();
							//	app->mapRenderers[j]->reloadClipnodes();
							logf("Redirected hull %d to hull %d in map %s\n", i, k, map->name.c_str());
							//}
							checkValidHulls();
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Create"))
	{
		Bsp* map = app->getSelectedMap();

		if (ImGui::MenuItem("Entity", 0, false, map)) {
			Entity* newEnt = new Entity();
			vec3 origin = (app->cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "info_player_deathmatch");

			CreateEntityCommand* createCommand = new CreateEntityCommand("Create Entity", app->getSelectedMapId(), newEnt);
			delete newEnt;
			createCommand->execute();
			app->pushUndoCommand(createCommand);
		}

		if (ImGui::MenuItem("BSP Model", 0, false, !app->isLoading && map)) {
			vec3 origin = app->cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "func_wall");

			float snapSize = pow(2.0f, g_app->gridSnapLevel * 1.0f);
			if (snapSize < 16) {
				snapSize = 16;
			}

			CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->getSelectedMapId(), newEnt, snapSize);
			command->execute();
			delete newEnt;
			app->pushUndoCommand(command);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Widgets"))
	{
		if (ImGui::MenuItem("Debug", NULL, showDebugWidget)) {
			showDebugWidget = !showDebugWidget;
		}
		if (ImGui::MenuItem("Keyvalue Editor", "Alt+Enter", showKeyvalueWidget)) {
			showKeyvalueWidget = !showKeyvalueWidget;
		}
		if (ImGui::MenuItem("Transform", "Ctrl+M", showTransformWidget)) {
			showTransformWidget = !showTransformWidget;
		}
		if (ImGui::MenuItem("Go to", "Ctrl+G", showGOTOWidget)) {
			showGOTOWidget = !showGOTOWidget;
			showGOTOWidget_update = true;
		}

		if (ImGui::MenuItem("Face Properties", "", showTextureWidget)) {
			showTextureWidget = !showTextureWidget;
		}
		if (ImGui::MenuItem("LightMap Editor (WIP)", "", showLightmapEditorWidget)) {
			showLightmapEditorWidget = !showLightmapEditorWidget;
			showLightmapEditorUpdate = true;
		}
		if (ImGui::MenuItem("Map merge", "", showMergeMapWidget)) {
			showMergeMapWidget = !showMergeMapWidget;
		}
		if (ImGui::MenuItem("Log", "", showLogWidget)) {
			showLogWidget = !showLogWidget;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("View help")) {
			showHelpWidget = true;
		}
		if (ImGui::MenuItem("About")) {
			showAboutWidget = true;
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

void Gui::drawToolbar() {
	ImVec2 window_pos = ImVec2(10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin("toolbar", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImGuiContext& g = *GImGui;
		ImVec4 dimColor = style.Colors[ImGuiCol_FrameBg];
		ImVec4 selectColor = style.Colors[ImGuiCol_FrameBgActive];
		float iconWidth = (fontSize / 22.0f) * 32;
		ImVec2 iconSize = ImVec2(iconWidth, iconWidth);
		ImVec4 testColor = ImVec4(1, 0, 0, 1);
		selectColor.x *= selectColor.w;
		selectColor.y *= selectColor.w;
		selectColor.z *= selectColor.w;
		selectColor.w = 1;

		dimColor.x *= dimColor.w;
		dimColor.y *= dimColor.w;
		dimColor.z *= dimColor.w;
		dimColor.w = 1;

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_OBJECT ? selectColor : dimColor);
		if (ImGui::ImageButton((void*)(uint64_t)objectIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1), 4)) {
			app->deselectFaces();
			app->deselectObject();
			app->pickMode = PICK_OBJECT;
			showTextureWidget = false;
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Object selection mode");
			ImGui::EndTooltip();
		}

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_FACE ? selectColor : dimColor);
		ImGui::SameLine();
		if (ImGui::ImageButton((void*)(uint64_t)faceIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1), 4)) {
			if (app->pickInfo.modelIdx > 0 && app->pickMode == PICK_FACE) {
				Bsp* map = app->getSelectedMap();
				if (map)
				{
					BspRenderer* mapRenderer = map->getBspRender();
					BSPMODEL& model = map->models[app->pickInfo.modelIdx];
					for (int i = 0; i < model.nFaces; i++) {
						int faceIdx = model.iFirstFace + i;
						mapRenderer->highlightFace(faceIdx, true);
						app->selectedFaces.push_back(faceIdx);
					}
				}
			}
			if (app->pickMode != PICK_FACE)
				app->deselectObject();
			app->pickMode = PICK_FACE;
			app->pickCount++; // force texture tool refresh
			showTextureWidget = true;
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Face selection mode");
			ImGui::EndTooltip();
		}
	}
	ImGui::End();
}

void Gui::drawFpsOverlay() {
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 window_pos = ImVec2(io.DisplaySize.x - 10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
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

void Gui::drawStatusMessage() {
	static float windowWidth = 32;
	static float loadingWindowWidth = 32;
	static float loadingWindowHeight = 32;

	bool showStatus = app->invalidSolid || !app->isTransformableSolid || badSurfaceExtents || lightmapTooLarge || app->modelUsesSharedStructures;
	if (showStatus) {
		ImVec2 window_pos = ImVec2((app->windowWidth - windowWidth) / 2.f, app->windowHeight - 10.f);
		ImVec2 window_pos_pivot = ImVec2(0.0f, 1.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

		if (ImGui::Begin("status", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			if (app->modelUsesSharedStructures) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "SHARED DATA");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Model shares planes/clipnodes with other models.\n\nDuplicate the model to enable model editing.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (!app->isTransformableSolid) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "CONCAVE SOLID");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Scaling and vertex manipulation don't work with concave solids yet\n";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (app->invalidSolid) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "INVALID SOLID");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"The selected solid is not convex or has non-planar faces.\n\n"
						"Transformations will be reverted unless you fix this.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (badSurfaceExtents) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "BAD SURFACE EXTENTS");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"One or more of the selected faces contain too many texture pixels on some axis.\n\n"
						"This will crash the game. Increase texture scale to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (lightmapTooLarge) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "LIGHTMAP TOO LARGE");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"One or more of the selected faces contain too many texture pixels.\n\n"
						"This will crash the game. Increase texture scale to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			windowWidth = ImGui::GetWindowWidth();
		}
		ImGui::End();
	}

	if (app->isLoading) {
		ImVec2 window_pos = ImVec2((app->windowWidth - loadingWindowWidth) / 2,
			(app->windowHeight - loadingWindowHeight) / 2);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

		if (ImGui::Begin("loader", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			static clock_t lastTick = clock();
			static int loadTick = 0;

			if (clock() - lastTick / (float)CLOCKS_PER_SEC > 0.05f) {
				loadTick = (loadTick + 1) % 8;
				lastTick = clock();
			}

			ImGui::PushFont(consoleFontLarge);
			switch (loadTick) {
			case 0: ImGui::Text("Loading |"); break;
			case 1: ImGui::Text("Loading /"); break;
			case 2: ImGui::Text("Loading -"); break;
			case 3: ImGui::Text("Loading \\"); break;
			case 4: ImGui::Text("Loading |"); break;
			case 5: ImGui::Text("Loading /"); break;
			case 6: ImGui::Text("Loading -"); break;
			case 7: ImGui::Text("Loading \\"); break;
			default:  break;
			}
			ImGui::PopFont();

		}
		loadingWindowWidth = ImGui::GetWindowWidth();
		loadingWindowHeight = ImGui::GetWindowHeight();

		ImGui::End();
	}
}

void Gui::drawDebugWidget() {
	ImGui::SetNextWindowBgAlpha(0.75f);

	ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 100.f), ImVec2(FLT_MAX, app->windowHeight * 1.0f));
	if (ImGui::Begin("Debug info", &showDebugWidget, ImGuiWindowFlags_AlwaysAutoResize)) {

		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Origin: %d %d %d", (int)app->cameraOrigin.x, (int)app->cameraOrigin.y, (int)app->cameraOrigin.z);
			ImGui::Text("Angles: %d %d %d", (int)app->cameraAngles.x, (int)app->cameraAngles.y, (int)app->cameraAngles.z);
		}

		Bsp* map = app->getSelectedMap();
		Entity* ent = app->pickInfo.ent;
		if (!map || !ent)
		{
			ImGui::Text("No map selected");
		}
		else
		{
			if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Name: %s", map->name.c_str());
			}

			if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Entity ID: %d", app->pickInfo.entIdx);

				if (app->pickInfo.modelIdx > 0) {
					ImGui::Checkbox("Debug clipnodes", &app->debugClipnodes);
					ImGui::SliderInt("Clipnode", &app->debugInt, 0, app->debugIntMax);

					ImGui::Checkbox("Debug nodes", &app->debugNodes);
					ImGui::SliderInt("Node", &app->debugNode, 0, app->debugNodeMax);
				}

				if (app->pickInfo.faceIdx != -1) {
					BSPMODEL& model = map->models[app->pickInfo.modelIdx];
					BSPFACE& face = map->faces[app->pickInfo.faceIdx];

					ImGui::Text("Model ID: %d", app->pickInfo.modelIdx);
					ImGui::Text("Model polies: %d", model.nFaces);

					ImGui::Text("Face ID: %d", app->pickInfo.faceIdx);
					ImGui::Text("Plane ID: %d", face.iPlane);

					if (face.iTextureInfo < map->texinfoCount) {
						BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
						int texOffset = ((int*)map->textures)[info.iMiptex + 1];
						BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
						ImGui::Text("Texinfo ID: %d", face.iTextureInfo);
						ImGui::Text("Texture ID: %d", info.iMiptex);
						ImGui::Text("Texture: %s (%dx%d)", tex.szName, tex.nWidth, tex.nHeight);
					}
					ImGui::Text("Lightmap Offset: %d", face.nLightmapOffset);
				}
			}
		}

		std::string bspTreeTitle = "BSP Tree";
		if (app->pickInfo.modelIdx >= 0) {
			bspTreeTitle += " (Model " + std::to_string(app->pickInfo.modelIdx) + ")";
		}
		if (ImGui::CollapsingHeader((bspTreeTitle + "##bsptree").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			if (app->pickInfo.modelIdx >= 0) {
				if (!map)
				{
					ImGui::Text("No map selected");
				}
				else
				{
					vec3 localCamera = app->cameraOrigin - map->getBspRender()->mapOffset;

					static ImVec4 hullColors[] = {
						ImVec4(1, 1, 1, 1),
						ImVec4(0.3f, 1, 1, 1),
						ImVec4(1, 0.3f, 1, 1),
						ImVec4(1, 1, 0.3f, 1),
					};

					for (int i = 0; i < MAX_MAP_HULLS; i++) {
						std::vector<int> nodeBranch;
						int leafIdx;
						int childIdx = -1;
						int headNode = map->models[app->pickInfo.modelIdx].iHeadnodes[i];
						int contents = map->pointContents(headNode, localCamera, i, nodeBranch, leafIdx, childIdx);

						ImGui::PushStyleColor(ImGuiCol_Text, hullColors[i]);
						if (ImGui::TreeNode(("HULL " + std::to_string(i)).c_str()))
						{
							ImGui::Indent();
							ImGui::Text("Contents: %s", map->getLeafContentsName(contents));
							if (i == 0) {
								ImGui::Text("Leaf: %d", leafIdx);
							}
							ImGui::Text("Parent Node: %d (child %d)",
								nodeBranch.size() ? nodeBranch[nodeBranch.size() - 1] : headNode,
								childIdx);
							ImGui::Text("Head Node: %d", headNode);
							ImGui::Text("Depth: %d", nodeBranch.size());

							ImGui::Unindent();
							ImGui::TreePop();
						}
						ImGui::PopStyleColor();
					}
				}
			}
			else {
				ImGui::Text("No model selected");
			}
		}
		else {
			ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen);
		}

		if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("DebugVec0 %6.2f %6.2f %6.2f", app->debugVec0.x, app->debugVec0.y, app->debugVec0.z);
			ImGui::Text("DebugVec1 %6.2f %6.2f %6.2f", app->debugVec1.x, app->debugVec1.y, app->debugVec1.z);
			ImGui::Text("DebugVec2 %6.2f %6.2f %6.2f", app->debugVec2.x, app->debugVec2.y, app->debugVec2.z);
			ImGui::Text("DebugVec3 %6.2f %6.2f %6.2f", app->debugVec3.x, app->debugVec3.y, app->debugVec3.z);

			float mb = app->undoMemoryUsage / (1024.0f * 1024.0f);
			ImGui::Text("Undo Memory Usage: %.2f MB", mb);


			bool isScalingObject = g_app->transformMode == TRANSFORM_SCALE && g_app->transformTarget == TRANSFORM_OBJECT;
			bool isMovingOrigin = g_app->transformMode == TRANSFORM_MOVE && g_app->transformTarget == TRANSFORM_ORIGIN && g_app->originSelected;
			bool isTransformingValid = ((g_app->isTransformableSolid && !g_app->modelUsesSharedStructures) || !isScalingObject) && g_app->transformTarget != TRANSFORM_ORIGIN;
			bool isTransformingWorld = g_app->pickInfo.entIdx == 0 && g_app->transformTarget != TRANSFORM_OBJECT;

			ImGui::Text("isScalingObject %d", isScalingObject);
			ImGui::Text("isMovingOrigin %d", isMovingOrigin);
			ImGui::Text("isTransformingValid %d", isTransformingValid);
			ImGui::Text("isTransformingWorld %d", isTransformingWorld);

			ImGui::Text("showDragAxes %d\nmovingEnt %d\ncanTransform %d",
				g_app->showDragAxes, g_app->movingEnt, g_app->canTransform);


		}
	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor() {
	//ImGui::SetNextWindowBgAlpha(0.75f);

	ImGui::SetNextWindowSize(ImVec2(610.f, 610.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	//ImGui::SetNextWindowContentSize(ImVec2(550, 0.0f));
	if (ImGui::Begin("Keyvalue Editor", &showKeyvalueWidget, 0)) {
		if (app->pickInfo.ent && app->fgd
			&& !app->isLoading && !app->isModelsReloading && !app->reloading) {
			Bsp* map = app->getSelectedMap();
			Entity* ent = app->pickInfo.ent;
			BSPMODEL& model = map->models[app->pickInfo.modelIdx];
			BSPFACE& face = map->faces[app->pickInfo.faceIdx];
			std::string cname = ent->keyvalues["classname"];
			FgdClass* fgdClass = app->fgd->getFgdClass(cname);

			ImGui::PushFont(largeFont);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Class:"); ImGui::SameLine();
			if (cname != "worldspawn") {
				if (ImGui::Button((" " + cname + " ").c_str()))
					ImGui::OpenPopup("classname_popup");
			}
			else {
				ImGui::Text(cname.c_str());
			}
			ImGui::PopFont();

			if (fgdClass) {
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted((fgdClass->description).c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
			}


			if (ImGui::BeginPopup("classname_popup"))
			{
				ImGui::Text("Change Class");
				ImGui::Separator();

				std::vector<FgdGroup>* targetGroup = &app->fgd->pointEntGroups;

				if (ent->hasKey("model")) {
					targetGroup = &app->fgd->solidEntGroups;
				}

				for (int i = 0; i < targetGroup->size(); i++) {
					FgdGroup& group = targetGroup->at(i);

					if (ImGui::BeginMenu(group.groupName.c_str())) {
						for (int k = 0; k < group.classes.size(); k++) {
							if (ImGui::MenuItem(group.classes[k]->name.c_str())) {
								ent->setOrAddKeyvalue("classname", group.classes[k]->name);
								map->getBspRender()->refreshEnt(app->pickInfo.entIdx);
								app->pushEntityUndoState("Change Class");
							}
						}

						ImGui::EndMenu();
					}
				}

				ImGui::EndPopup();
			}

			ImGui::Dummy(ImVec2(0, 10));

			if (ImGui::BeginTabBar("##tabs"))
			{
				if (ImGui::BeginTabItem("Attributes")) {
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_SmartEditTab(ent);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Flags")) {
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_FlagsTab(ent);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Raw Edit")) {
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_RawEditTab(ent);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();

		}
		else {
			if (!app->pickInfo.ent)
				ImGui::Text("No entity selected");
			else
				ImGui::Text("No fgd loaded");
		}

	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor_SmartEditTab(Entity* ent) {
	Bsp* map = app->getSelectedMap();
	if (!map)
		return;

	std::string cname = ent->keyvalues["classname"];
	FgdClass* fgdClass = app->fgd->getFgdClass(cname);
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild("SmartEditWindow");

	ImGui::Columns(2, "smartcolumns", false); // 4-ways, with border

	static char keyNames[MAX_KEYS_PER_ENT][MAX_KEY_LEN];
	static char keyValues[MAX_KEYS_PER_ENT][MAX_VAL_LEN];

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - (paddingx * 2)) * 0.5f;

	// needed if autoresize is true
	if (ImGui::GetScrollMaxY() > 0)
		inputWidth -= style.ScrollbarSize * 0.5f;

	struct InputData {
		std::string key;
		std::string defaultValue;
		Entity* entRef;
		int entIdx;
		BspRenderer* bspRenderer;
	};

	if (fgdClass) {

		static InputData inputData[128];
		static int lastPickCount = 0;

		if (ent->hasKey("model"))
		{
			bool foundmodel = false;
			for (int i = 0; i < fgdClass->keyvalues.size(); i++) {
				KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
				std::string key = keyvalue.name;
				if (key == "model")
				{
					foundmodel = true;
				}
			}
			if (!foundmodel)
			{
				KeyvalueDef keyvalue = KeyvalueDef();
				keyvalue.name = "model";
				keyvalue.description = "Model";
				keyvalue.iType = FGD_KEY_STRING;
				fgdClass->keyvalues.push_back(keyvalue);
			}
		}

		for (int i = 0; i < fgdClass->keyvalues.size() && i < 128; i++) {
			KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
			std::string key = keyvalue.name;
			if (key == "spawnflags") {
				continue;
			}
			std::string value = ent->keyvalues[key];
			std::string niceName = keyvalue.description;

			if (value.empty() && keyvalue.defaultValue.length()) {
				value = keyvalue.defaultValue;
			}
			if (niceName.size() >= MAX_KEY_LEN)
				niceName = niceName.substr(0, MAX_KEY_LEN - 1);
			if (value.size() >= MAX_VAL_LEN)
				value = value.substr(0, MAX_VAL_LEN - 1);

			memcpy(keyNames[i], niceName.c_str(), niceName.size() + 1);
			memcpy(keyValues[i], value.c_str(), value.size() + 1);

			inputData[i].key = key;
			inputData[i].defaultValue = keyvalue.defaultValue;
			inputData[i].entIdx = app->pickInfo.entIdx;
			inputData[i].entRef = ent;
			inputData[i].bspRenderer = map->getBspRender();

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(keyNames[i]); ImGui::NextColumn();

			ImGui::SetNextItemWidth(inputWidth);

			if (keyvalue.iType == FGD_KEY_CHOICES && keyvalue.choices.size() > 0) {
				std::string selectedValue = keyvalue.choices[0].name;
				int ikey = atoi(value.c_str());

				for (int k = 0; k < keyvalue.choices.size(); k++) {
					KeyvalueChoice& choice = keyvalue.choices[k];

					if ((choice.isInteger && ikey == choice.ivalue) ||
						(!choice.isInteger && value == choice.svalue)) {
						selectedValue = choice.name;
					}
				}

				if (ImGui::BeginCombo(("##val" + std::to_string(i)).c_str(), selectedValue.c_str()))
				{
					for (int k = 0; k < keyvalue.choices.size(); k++) {
						KeyvalueChoice& choice = keyvalue.choices[k];
						bool selected = choice.svalue == value || (value.empty() && choice.svalue == keyvalue.defaultValue);

						if (ImGui::Selectable(choice.name.c_str(), selected)) {
							ent->setOrAddKeyvalue(key, choice.svalue);
							map->getBspRender()->refreshEnt(app->pickInfo.entIdx);
							app->updateEntConnections();
							app->pushEntityUndoState("Edit Keyvalue");
						}
					}

					ImGui::EndCombo();
				}
			}
			else {
				struct InputChangeCallback {
					static int keyValueChanged(ImGuiInputTextCallbackData* data) {
						if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
							if (data->EventChar < 256) {
								if (strchr("-0123456789", (char)data->EventChar))
									return 0;
							}
							return 1;
						}

						InputData* linputData = (InputData*)data->UserData;
						Entity* ent = linputData->entRef;

						std::string newVal = data->Buf;

						bool needReloadModel = false;

						if (!g_app->reloading && !g_app->isModelsReloading && linputData->key == "model")
						{
							if (ent->hasKey("model") && ent->keyvalues["model"] != newVal)
							{
								needReloadModel = true;
							}
						}
						if (newVal.empty()) {
							if (linputData->defaultValue.length()) {
								ent->setOrAddKeyvalue(linputData->key, linputData->defaultValue);
							}
							else {
								ent->removeKeyvalue(linputData->key);
							}
						}
						else {
							ent->setOrAddKeyvalue(linputData->key, newVal);
						}

						linputData->bspRenderer->refreshEnt(linputData->entIdx);
						if (needReloadModel)
							g_app->reloadBspModels();
						g_app->updateEntConnections();

						return 1;
					}
				};

				if (keyvalue.iType == FGD_KEY_INTEGER) {
					ImGui::InputText(("##val" + std::to_string(i) + "_" + std::to_string(app->pickCount)).c_str(), keyValues[i], 64,
						ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways,
						InputChangeCallback::keyValueChanged, &inputData[i]);
				}
				else {
					ImGui::InputText(("##val" + std::to_string(i) + "_" + std::to_string(app->pickCount)).c_str(), keyValues[i], 64,
						ImGuiInputTextFlags_CallbackAlways, InputChangeCallback::keyValueChanged, &inputData[i]);
				}


			}

			ImGui::NextColumn();
		}

		lastPickCount = app->pickCount;
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_FlagsTab(Entity* ent) {
	ImGui::BeginChild("FlagsWindow");

	unsigned int spawnflags = strtoul(ent->keyvalues["spawnflags"].c_str(), NULL, 10);
	FgdClass* fgdClass = app->fgd->getFgdClass(ent->keyvalues["classname"]);

	ImGui::Columns(2, "keyvalcols", true);

	static bool checkboxEnabled[32];

	for (int i = 0; i < 32; i++) {
		if (i == 16) {
			ImGui::NextColumn();
		}
		std::string name;
		if (fgdClass) {
			name = fgdClass->spawnFlagNames[i];
		}

		checkboxEnabled[i] = spawnflags & (1 << i);

		if (ImGui::Checkbox((name + "##flag" + std::to_string(i)).c_str(), &checkboxEnabled[i])) {
			if (!checkboxEnabled[i]) {
				spawnflags &= ~(1U << i);
			}
			else {
				spawnflags |= (1U << i);
			}
			if (spawnflags != 0)
				ent->setOrAddKeyvalue("spawnflags", std::to_string(spawnflags));
			else
				ent->removeKeyvalue("spawnflags");

			app->pushEntityUndoState(checkboxEnabled[i] ? "Enable Flag" : "Disable Flag");
		}
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_RawEditTab(Entity* ent) {
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::Columns(4, "keyvalcols", false);

	float butColWidth = smallFont->CalcTextSizeA(GImGui->FontSize, 100, 100, " X ").x + style.FramePadding.x * 4;
	float textColWidth = (ImGui::GetWindowWidth() - (butColWidth + style.FramePadding.x * 2) * 2) * 0.5f;

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	ImGui::NextColumn();
	ImGui::Text("  Key"); ImGui::NextColumn();
	ImGui::Text("Value"); ImGui::NextColumn();
	ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::BeginChild("RawValuesWindow");

	ImGui::Columns(4, "keyvalcols2", false);

	textColWidth -= style.ScrollbarSize; // double space to prevent accidental deletes

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	static char keyNames[MAX_KEYS_PER_ENT][MAX_KEY_LEN];
	static char keyValues[MAX_KEYS_PER_ENT][MAX_VAL_LEN];

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - paddingx * 2) * 0.5f;

	struct InputData {
		int idx;
		int entIdx;
		Entity* entRef;
		BspRenderer* bspRenderer;
	};

	struct TextChangeCallback {
		static int keyNameChanged(ImGuiInputTextCallbackData* data) {
			InputData* inputData = (InputData*)data->UserData;
			Entity* ent = inputData->entRef;

			std::string key = ent->keyOrder[inputData->idx];
			if (key != data->Buf) {
				ent->renameKey(inputData->idx, data->Buf);
				inputData->bspRenderer->refreshEnt(inputData->entIdx);
				if (key == "model" || std::string(data->Buf) == "model") {
					g_app->reloadBspModels();
					inputData->bspRenderer->preRenderEnts();
					g_app->saveLumpState(inputData->bspRenderer->map, 0xffffffff, false);
				}
				g_app->updateEntConnections();
			}

			return 1;
		}

		static int keyValueChanged(ImGuiInputTextCallbackData* data) {
			InputData* inputData = (InputData*)data->UserData;
			Entity* ent = inputData->entRef;
			std::string key = ent->keyOrder[inputData->idx];

			if (ent->keyvalues[key] != data->Buf) {
				ent->setOrAddKeyvalue(key, data->Buf);
				inputData->bspRenderer->refreshEnt(inputData->entIdx);
				if (key == "model") {
					g_app->reloadBspModels();
					inputData->bspRenderer->preRenderEnts();
					g_app->saveLumpState(inputData->bspRenderer->map, 0xffffffff, false);
				}
				g_app->updateEntConnections();
			}

			return 1;
		}
	};

	static InputData keyIds[MAX_KEYS_PER_ENT];
	static InputData valueIds[MAX_KEYS_PER_ENT];
	static int lastPickCount = -1;
	static std::string dragNames[MAX_KEYS_PER_ENT];
	static const char* dragIds[MAX_KEYS_PER_ENT];

	if (dragNames[0].empty()) {
		for (int i = 0; i < MAX_KEYS_PER_ENT; i++) {
			std::string name = "::##drag" + std::to_string(i);
			dragNames[i] = std::move(name);
		}
	}

	if (lastPickCount != app->pickCount) {
		for (int i = 0; i < MAX_KEYS_PER_ENT; i++) {
			dragIds[i] = dragNames[i].c_str();
		}
	}

	ImVec4 dragColor = style.Colors[ImGuiCol_FrameBg];
	dragColor.x *= 2;
	dragColor.y *= 2;
	dragColor.z *= 2;

	ImVec4 dragButColor = style.Colors[ImGuiCol_Header];

	static bool hoveredDrag[MAX_KEYS_PER_ENT];
	static int ignoreErrors = 0;

	static bool wasKeyDragging = false;
	bool keyDragging = false;

	Bsp* map = app->getSelectedMap();

	float startY = 0;
	for (int i = 0; i < ent->keyOrder.size() && i < MAX_KEYS_PER_ENT; i++) {
		const char* item = dragIds[i];

		{
			style.SelectableTextAlign.x = 0.5f;
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Header, hoveredDrag[i] ? dragColor : dragButColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, dragColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, dragColor);
			ImGui::Selectable(item, true);
			ImGui::PopStyleColor(3);
			style.SelectableTextAlign.x = 0.0f;

			hoveredDrag[i] = ImGui::IsItemActive();
			if (hoveredDrag[i]) {
				keyDragging = true;
			}


			if (i == 0) {
				startY = ImGui::GetItemRectMin().y;
			}

			if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
			{
				int n_next = (int)((ImGui::GetMousePos().y - startY) / (ImGui::GetItemRectSize().y + style.FramePadding.y * 2));
				if (n_next >= 0 && n_next < ent->keyOrder.size() && n_next < MAX_KEYS_PER_ENT)
				{
					dragIds[i] = dragIds[n_next];
					dragIds[n_next] = item;

					std::string temp = ent->keyOrder[i];
					ent->keyOrder[i] = ent->keyOrder[n_next];
					ent->keyOrder[n_next] = temp;

					// fix false-positive error highlight
					ignoreErrors = 2;

					ImGui::ResetMouseDragDelta();
				}
			}

			ImGui::NextColumn();
		}

		std::string key = ent->keyOrder[i];
		std::string value = ent->keyvalues[key];

		{
			bool invalidKey = ignoreErrors == 0 && lastPickCount == app->pickCount && key != keyNames[i];


			if (key.size() >= MAX_KEY_LEN)
				key = key.substr(0, MAX_KEY_LEN - 1);

			memcpy(keyNames[i], key.c_str(), key.size() + 1);


			keyIds[i].idx = i;
			keyIds[i].entIdx = app->pickInfo.entIdx;
			keyIds[i].entRef = ent;
			keyIds[i].bspRenderer = map->getBspRender();

			if (invalidKey) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			}
			else if (hoveredDrag[i]) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##key" + std::to_string(i) + "_" + std::to_string(app->pickCount)).c_str(), keyNames[i], MAX_KEY_LEN, ImGuiInputTextFlags_CallbackAlways,
				TextChangeCallback::keyNameChanged, &keyIds[i]);


			if (invalidKey || hoveredDrag[i]) {
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{
			if (value.size() >= MAX_VAL_LEN)
				value = value.substr(0,MAX_VAL_LEN - 1);

			memcpy(keyValues[i], value.c_str(), value.size() + 1);

			valueIds[i].idx = i;
			valueIds[i].entIdx = app->pickInfo.entIdx;
			valueIds[i].entRef = ent;
			valueIds[i].bspRenderer = map->getBspRender();

			if (hoveredDrag[i]) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##val" + std::to_string(i) + std::to_string(app->pickCount)).c_str(), keyValues[i], MAX_VAL_LEN, ImGuiInputTextFlags_CallbackAlways,
				TextChangeCallback::keyValueChanged, &valueIds[i]);



			if (strcmp(keyNames[i], "angles") == 0)
			{
				ImGui::SetNextItemWidth(inputWidth);
				if (IsEntNotSupportAngles(ent->keyvalues["classname"]))
				{
					ImGui::TextUnformatted("ANGLES NOT SUPPORTED");
				}
				else if (ent->keyvalues["classname"] == "env_sprite")
				{
					ImGui::TextUnformatted("ANGLES PARTIALLY SUPPORT");
				}
				else if (ent->keyvalues["classname"] == "func_breakable")
				{
					ImGui::TextUnformatted("ANGLES Y NOT SUPPORT");
				}
			}


			if (hoveredDrag[i]) {
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{

			ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
			if (ImGui::Button((" X ##del" + std::to_string(i)).c_str())) {
				ent->removeKeyvalue(key);
				map->getBspRender()->refreshEnt(app->pickInfo.entIdx);
				if (key == "model")
					map->getBspRender()->preRenderEnts();
				ignoreErrors = 2;
				g_app->updateEntConnections();
				g_app->pushEntityUndoState("Delete Keyvalue");
			}
			ImGui::PopStyleColor(3);
			ImGui::NextColumn();
		}
	}

	if (!keyDragging && wasKeyDragging) {
		app->pushEntityUndoState("Move Keyvalue");
	}

	wasKeyDragging = keyDragging;

	lastPickCount = app->pickCount;

	ImGui::Columns(1);

	ImGui::Dummy(ImVec2(0, style.FramePadding.y));
	ImGui::Dummy(ImVec2(butColWidth, 0)); ImGui::SameLine();
	if (ImGui::Button(" Add ")) {
		std::string baseKeyName = "NewKey";
		std::string keyName = "NewKey";
		for (int i = 0; i < 128; i++) {
			if (!ent->hasKey(keyName)) {
				break;
			}
			keyName = baseKeyName + "#" + std::to_string(i + 2);
		}
		ent->addKeyvalue(keyName, "");
		map->getBspRender()->refreshEnt(app->pickInfo.entIdx);
		app->updateEntConnections();
		ignoreErrors = 2;
		app->pushEntityUndoState("Add Keyvalue");
	}

	if (ignoreErrors > 0) {
		ignoreErrors--;
	}

	ImGui::EndChild();
}

void Gui::drawGOTOWidget() {
	ImGui::SetNextWindowSize(ImVec2(410.f, 200.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(410.f, 200.f), ImVec2(410.f, 200.f));
	static vec3 coordinates = vec3();
	static vec3 angles = vec3();
	float angles_y = 0.0f;
	if (ImGui::Begin("Go to coordinates:", &showGOTOWidget, 0)) {
		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
		if (showGOTOWidget_update)
		{
			coordinates = app->cameraOrigin;
			angles = app->cameraAngles;
			showGOTOWidget_update = false;
		}
		ImGui::Text("Coordinates");
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat("##xpos", &coordinates.x, 0.1f, 0, 0, "X: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##ypos", &coordinates.y, 0.1f, 0, 0, "Y: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##zpos", &coordinates.z, 0.1f, 0, 0, "Z: %.0f");
		ImGui::PopItemWidth();
		ImGui::Text("Angles");
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat("##xangles", &angles.x, 0.1f, 0, 0, "X: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##yangles", &angles_y, 0.1f, 0, 0, "Y: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##zangles", &angles.z, 0.1f, 0, 0, "Z: %.0f");
		ImGui::PopItemWidth();

		ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
		if (ImGui::Button("Go to"))
		{
			app->cameraOrigin = coordinates;
			app->cameraAngles = angles;
			makeVectors(angles, app->cameraForward, app->cameraRight, app->cameraUp);
		}
		ImGui::PopStyleColor(3);

	}

	ImGui::End();
}
void Gui::drawTransformWidget() {

	bool transformingEnt = false;
	Entity* ent = NULL;
	Bsp* map = app->getSelectedMap();
	if (map)
	{
		ent = app->pickInfo.ent;
		transformingEnt = app->pickInfo.entIdx > 0;
	}

	ImGui::SetNextWindowSize(ImVec2(430.f, 380.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));


	static float x, y, z;
	static float fx, fy, fz;
	static float last_fx, last_fy, last_fz;
	static float sx, sy, sz;

	static int lastPickCount = -1;
	static int lastVertPickCount = -1;
	static bool oldSnappingEnabled = app->gridSnappingEnabled;
	static int oldTransformTarget = -1;


	if (ImGui::Begin("Transformation", &showTransformWidget, 0)) {
		if (!ent)
		{
			ImGui::Text("No entity selected");
		}
		else
		{
			ImGuiStyle& style = ImGui::GetStyle();

			bool shouldUpdateUi = lastPickCount != app->pickCount ||
				app->draggingAxis != -1 ||
				app->movingEnt ||
				oldSnappingEnabled != app->gridSnappingEnabled ||
				lastVertPickCount != app->vertPickCount ||
				oldTransformTarget != app->transformTarget;

			TransformAxes& activeAxes = *(app->transformMode == TRANSFORM_SCALE ? &app->scaleAxes : &app->moveAxes);

			if (shouldUpdateUi) {
				if (transformingEnt) {
					if (app->transformTarget == TRANSFORM_VERTEX) {
						x = fx = last_fx = activeAxes.origin.x;
						y = fy = last_fy = activeAxes.origin.y;
						z = fz = last_fz = activeAxes.origin.z;
					}
					else {
						vec3 ori = ent->hasKey("origin") ? parseVector(ent->keyvalues["origin"]) : vec3();
						if (app->originSelected) {
							ori = app->transformedOrigin;
						}
						x = fx = ori.x;
						y = fy = ori.y;
						z = fz = ori.z;
					}

				}
				else {
					x = fx = 0.f;
					y = fy = 0.f;
					z = fz = 0.f;
				}
				sx = sy = sz = 1;
			}

			oldTransformTarget = app->transformTarget;
			oldSnappingEnabled = app->gridSnappingEnabled;
			lastVertPickCount = app->vertPickCount;
			lastPickCount = app->pickCount;

			bool scaled = false;
			bool originChanged = false;
			guiHoverAxis = -1;

			float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
			float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
			float inputWidth4 = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.25f;

			static bool inputsWereDragged = false;
			bool inputsAreDragging = false;

			ImGui::Text("Move");
			ImGui::PushItemWidth(inputWidth);

			if (app->gridSnappingEnabled) {
				if (ImGui::DragFloat("##xpos", &x, 0.1f, 0, 0, "X: %d")) { originChanged = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 0;
				if (ImGui::IsItemActive())
					inputsAreDragging = true;
				ImGui::SameLine();

				if (ImGui::DragFloat("##ypos", &y, 0.1f, 0, 0, "Y: %d")) { originChanged = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 1;
				if (ImGui::IsItemActive())
					inputsAreDragging = true;
				ImGui::SameLine();

				if (ImGui::DragFloat("##zpos", &z, 0.1f, 0, 0, "Z: %d")) { originChanged = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 2;
				if (ImGui::IsItemActive())
					inputsAreDragging = true;
			}
			else {
				if (ImGui::DragFloat("##xpos", &fx, 0.1f, 0, 0, "X: %.2f")) { originChanged = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 0;
				if (ImGui::IsItemActive())
					inputsAreDragging = true;
				ImGui::SameLine();

				if (ImGui::DragFloat("##ypos", &fy, 0.1f, 0, 0, "Y: %.2f")) { originChanged = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 1;
				if (ImGui::IsItemActive())
					inputsAreDragging = true;
				ImGui::SameLine();

				if (ImGui::DragFloat("##zpos", &fz, 0.1f, 0, 0, "Z: %.2f")) { originChanged = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 2;
				if (ImGui::IsItemActive())
					inputsAreDragging = true;
			}

			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));

			ImGui::Text("Scale");
			ImGui::PushItemWidth(inputWidth);

			if (ImGui::DragFloat("##xscale", &sx, 0.002f, 0, 0, "X: %.3f")) { scaled = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat("##yscale", &sy, 0.002f, 0, 0, "Y: %.3f")) { scaled = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat("##zscale", &sz, 0.002f, 0, 0, "Z: %.3f")) { scaled = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;

			if (inputsWereDragged && !inputsAreDragging) {
				if (app->undoEntityState->getOrigin() != ent->getOrigin()) {
					app->pushEntityUndoState("Move Entity");
				}

				if (transformingEnt) {
					app->applyTransform(true);

					if (app->gridSnappingEnabled) {
						fx = last_fx = x;
						fy = last_fy = y;
						fz = last_fz = z;
					}
					else {
						x = last_fx = fx;
						y = last_fy = fy;
						z = last_fz = fz;
					}
				}
			}

			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 3));
			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));
			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));


			ImGui::Columns(4, 0, false);
			ImGui::SetColumnWidth(0, inputWidth4);
			ImGui::SetColumnWidth(1, inputWidth4);
			ImGui::SetColumnWidth(2, inputWidth4);
			ImGui::SetColumnWidth(3, inputWidth4);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Target: "); ImGui::NextColumn();

			ImGui::RadioButton("Object", &app->transformTarget, TRANSFORM_OBJECT); ImGui::NextColumn();
			ImGui::RadioButton("Vertex", &app->transformTarget, TRANSFORM_VERTEX); ImGui::NextColumn();
			ImGui::RadioButton("Origin", &app->transformTarget, TRANSFORM_ORIGIN); ImGui::NextColumn();

			ImGui::Text("3D Axes: "); ImGui::NextColumn();
			if (ImGui::RadioButton("Hide", &app->transformMode, TRANSFORM_NONE))
				app->showDragAxes = false;

			ImGui::NextColumn();
			if (ImGui::RadioButton("Move", &app->transformMode, TRANSFORM_MOVE))
				app->showDragAxes = true;

			ImGui::NextColumn();
			if (ImGui::RadioButton("Scale", &app->transformMode, TRANSFORM_SCALE))
				app->showDragAxes = true;

			ImGui::Columns(1);

			const int grid_snap_modes = 11;
			const char* element_names[grid_snap_modes] = { "0", "1", "2", "4", "8", "16", "32", "64", "128", "256", "512" };
			static int current_element = app->gridSnapLevel + 1;

			ImGui::Columns(2, 0, false);
			ImGui::SetColumnWidth(0, inputWidth4);
			ImGui::SetColumnWidth(1, inputWidth4 * 3);
			ImGui::Text("Grid Snap:"); ImGui::NextColumn();
			ImGui::SetNextItemWidth(inputWidth4 * 3);
			if (ImGui::SliderInt("##gridsnap", &current_element, 0, grid_snap_modes - 1, element_names[current_element])) {
				app->gridSnapLevel = current_element - 1;
				app->gridSnappingEnabled = current_element != 0;
				originChanged = true;
			}
			ImGui::Columns(1);

			ImGui::PushItemWidth(inputWidth);
			ImGui::Checkbox("Texture lock", &app->textureLock);
			ImGui::SameLine();
			ImGui::TextDisabled("(WIP)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted("Doesn't work for angled faces yet. Applies to scaling only.");
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
			ImGui::Text(("Size: " + app->selectionSize.toKeyvalueString(false, "w ", "l ", "h")).c_str());

			if (transformingEnt) {
				if (originChanged) {
					if (app->transformTarget == TRANSFORM_VERTEX) {
						vec3 delta;
						if (app->gridSnappingEnabled) {
							delta = vec3(x - last_fx, y - last_fy, z - last_fz);
						}
						else {
							delta = vec3(fx - last_fx, fy - last_fy, fz - last_fz);
						}

						app->moveSelectedVerts(delta);
					}
					else if (app->transformTarget == TRANSFORM_OBJECT) {
						vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
						newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

						if (app->gridSnappingEnabled) {
							fx = x;
							fy = y;
							fz = z;
						}
						else {
							x = fx;
							y = fy;
							z = fz;
						}

						ent->setOrAddKeyvalue("origin", newOrigin.toKeyvalueString(!app->gridSnappingEnabled));
						map->getBspRender()->refreshEnt(app->pickInfo.entIdx);
						app->updateEntConnectionPositions();
					}
					else if (app->transformTarget == TRANSFORM_ORIGIN) {
						vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
						newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

						app->transformedOrigin = newOrigin;
					}
				}
				if (scaled && ent->isBspModel() && app->isTransformableSolid && !app->modelUsesSharedStructures) {
					if (app->transformTarget == TRANSFORM_VERTEX) {
						app->scaleSelectedVerts(sx, sy, sz);
					}
					else if (app->transformTarget == TRANSFORM_OBJECT) {
						int modelIdx = ent->getBspModelIdx();
						app->scaleSelectedObject(sx, sy, sz);
						map->getBspRender()->refreshModel(ent->getBspModelIdx());
					}
					else if (app->transformTarget == TRANSFORM_ORIGIN) {
						logf("Scaling has no effect on origins\n");
					}
				}
			}

			inputsWereDragged = inputsAreDragging;
		}
	}
	ImGui::End();
}

void Gui::clearLog()
{
	Buf.clear();
	LineOffsets.clear();
	LineOffsets.push_back(0);
}

void Gui::addLog(const char* s)
{
	int old_size = Buf.size();
	Buf.append(s);
	for (int new_size = Buf.size(); old_size < new_size; old_size++)
		if (Buf[old_size] == '\n')
			LineOffsets.push_back(old_size + 1);
}

void Gui::loadFonts() {
	// data copied to new array so that ImGui doesn't delete static data
	unsigned char* smallFontData = new unsigned char[sizeof(robotomedium)];
	unsigned char* largeFontData = new unsigned char[sizeof(robotomedium)];
	unsigned char* consoleFontData = new unsigned char[sizeof(robotomono)];
	unsigned char* consoleFontLargeData = new unsigned char[sizeof(robotomono)];
	memcpy(smallFontData, robotomedium, sizeof(robotomedium));
	memcpy(largeFontData, robotomedium, sizeof(robotomedium));
	memcpy(consoleFontData, robotomono, sizeof(robotomono));
	memcpy(consoleFontLargeData, robotomono, sizeof(robotomono));

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	smallFont = io.Fonts->AddFontFromMemoryTTF((void*)smallFontData, sizeof(robotomedium), fontSize);
	largeFont = io.Fonts->AddFontFromMemoryTTF((void*)largeFontData, sizeof(robotomedium), fontSize * 1.1f);
	consoleFont = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontData, sizeof(robotomono), fontSize);
	consoleFontLarge = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontLargeData, sizeof(robotomono), fontSize * 1.1f);
}

void Gui::drawLog() {

	ImGui::SetNextWindowSize(ImVec2(750.f, 300.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	if (!ImGui::Begin("Log", &showLogWidget))
	{
		ImGui::End();
		return;
	}

	g_log_mutex.lock();
	for (int i = 0; i < g_log_buffer.size(); i++) {
		addLog(g_log_buffer[i].c_str());
	}
	g_log_buffer.clear();
	g_log_mutex.unlock();

	static int i = 0;

	ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	bool copy = false;
	bool toggledAutoScroll = false;
	if (ImGui::BeginPopupContextWindow())
	{
		if (ImGui::MenuItem("Copy")) {
			copy = true;
		}
		if (ImGui::MenuItem("Clear")) {
			clearLog();
		}
		if (ImGui::MenuItem("Auto-scroll", NULL, &AutoScroll)) {
			toggledAutoScroll = true;
		}
		ImGui::EndPopup();
	}

	ImGui::PushFont(consoleFont);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();

	if (copy) ImGui::LogBegin(ImGuiLogType_Clipboard, 0);

	ImGuiListClipper clipper;
	clipper.Begin(LineOffsets.Size);
	while (clipper.Step())
	{
		for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
		{
			const char* line_start = buf + LineOffsets[line_no];
			const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
			ImGui::TextUnformatted(line_start, line_end);
		}
	}
	clipper.End();

	if (copy) ImGui::LogFinish();

	ImGui::PopFont();
	ImGui::PopStyleVar();

	if (AutoScroll && (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() || toggledAutoScroll))
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
	ImGui::End();

}

void Gui::drawSettings() {

	ImGui::SetNextWindowSize(ImVec2(790.f, 350.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Settings", &showSettingsWidget))
	{
		ImGuiContext& g = *GImGui;
		const int settings_tabs = 5;
		static const char* tab_titles[settings_tabs] = {
			"General",
			"FGDs",
			"Asset Paths",
			"Rendering",
			"Controls"
		};

		// left
		ImGui::BeginChild("left pane", ImVec2(150, 0), true);

		for (int i = 0; i < settings_tabs; i++) {
			if (ImGui::Selectable(tab_titles[i], settingsTab == i))
				settingsTab = i;
		}

		ImGui::EndChild();
		ImGui::SameLine();

		// right

		ImGui::BeginGroup();
		float footerHeight = settingsTab <= 2 ? ImGui::GetFrameHeightWithSpacing() + 4.f : 0.f;
		ImGui::BeginChild("item view", ImVec2(0, -footerHeight)); // Leave room for 1 line below us
		ImGui::Text(tab_titles[settingsTab]);
		ImGui::Separator();

		static char gamedir[MAX_PATH];
		static char workingdir[MAX_PATH];
		static size_t numFgds = 0;
		static size_t numRes = 0;

		static std::vector<std::string> tmpFgdPaths;
		static std::vector<std::string> tmpResPaths;

		if (reloadSettings) {
			if (g_settings.gamedir.size() >= MAX_PATH)
				g_settings.gamedir = g_settings.gamedir.substr(0, MAX_PATH - 1);
			if (g_settings.gamedir.size() >= MAX_PATH)
				g_settings.gamedir = g_settings.gamedir.substr(0, MAX_PATH - 1);

			memcpy(gamedir, g_settings.gamedir.c_str(), g_settings.gamedir.size() + 1);
			memcpy(workingdir, g_settings.workingdir.c_str(), g_settings.gamedir.size() + 1);
			tmpFgdPaths = g_settings.fgdPaths;
			tmpResPaths = g_settings.resPaths;

			numFgds = tmpFgdPaths.size();
			numRes = tmpResPaths.size();

			reloadSettings = false;
		}

		float pathWidth = ImGui::GetWindowWidth() - 60.f;
		float delWidth = 50.f;



		if (ifd::FileDialog::Instance().IsDone("GameDir")) {
			if (ifd::FileDialog::Instance().HasResult()) {
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				snprintf(gamedir, sizeof(gamedir), "%s", res.parent_path().string().c_str());
			}
			ifd::FileDialog::Instance().Close();
		}
		if (ifd::FileDialog::Instance().IsDone("WorkingDir")) {
			if (ifd::FileDialog::Instance().HasResult()) {
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				snprintf(workingdir, sizeof(workingdir), "%s", res.parent_path().string().c_str());
			}
			ifd::FileDialog::Instance().Close();
		}

		ImGui::BeginChild("right pane content");
		if (settingsTab == 0) {
			ImGui::Text("Game Directory:");
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText("##gamedir", gamedir, 256);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			if (ImGui::Button("...##gamedir"))
			{
				ifd::FileDialog::Instance().Open("GameDir", "Select game dir", std::string(), false, g_settings.lastdir);
			}
			ImGui::Text("Import/Export Directory:");
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText("##workdir", workingdir, 256);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			if (ImGui::Button("...##workdir"))
			{
				ifd::FileDialog::Instance().Open("WorkingDir", "Select working dir", std::string(), false, g_settings.lastdir);
			}
			if (ImGui::DragFloat("Font Size", &fontSize, 0.1f, 8, 48, "%d pixels")) {
				shouldReloadFonts = true;
			}
			ImGui::DragInt("Undo Levels", &app->undoLevels, 0.05f, 0, 64);
			ImGui::Checkbox("Verbose Logging", &g_verbose);
			ImGui::Checkbox("Make map backup", &g_settings.backUpMap);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Creates a backup of the BSP file when saving for the first time.");
				ImGui::EndTooltip();
			}

			ImGui::Checkbox("Preserve map CRC", &g_settings.preserveCrc32);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Hack original map CRC after anything edited.");
				ImGui::EndTooltip();
			}
		}
		else if (settingsTab == 1) {
			for (int i = 0; i < numFgds; i++) {
				ImGui::SetNextItemWidth(pathWidth);
				tmpFgdPaths[i].resize(256);
				ImGui::InputText(("##fgd" + std::to_string(i)).c_str(), &tmpFgdPaths[i][0], 256);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##del_fgd" + std::to_string(i)).c_str())) {
					tmpFgdPaths.erase(tmpFgdPaths.begin() + i);
					numFgds--;
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button("Add fgd path")) {
				numFgds++;
				tmpFgdPaths.emplace_back(std::string());
			}
		}
		else if (settingsTab == 2) {
			for (int i = 0; i < numRes; i++) {
				ImGui::SetNextItemWidth(pathWidth);
				tmpResPaths[i].resize(256);
				ImGui::InputText(("##res" + std::to_string(i)).c_str(), &tmpResPaths[i][0], 256);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##del_res" + std::to_string(i)).c_str())) {
					tmpResPaths.erase(tmpResPaths.begin() + i);
					numRes--;
				}
				ImGui::PopStyleColor(3);

			}

			if (ImGui::Button("Add res path")) {
				numRes++;
				tmpResPaths.emplace_back(std::string());
			}
		}
		else if (settingsTab == 3) {
			ImGui::Text("Viewport:");
			if (ImGui::Checkbox("VSync", &vsync)) {
				glfwSwapInterval(vsync ? 1 : 0);
			}
			ImGui::DragFloat("Field of View", &app->fov, 0.1f, 1.0f, 150.0f, "%.1f degrees");
			ImGui::DragFloat("Back Clipping plane", &app->zFar, 10.0f, FLT_MIN_COORD, FLT_MAX_COORD, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::Separator();

			bool renderTextures = g_render_flags & RENDER_TEXTURES;
			bool renderLightmaps = g_render_flags & RENDER_LIGHTMAPS;
			bool renderWireframe = g_render_flags & RENDER_WIREFRAME;
			bool renderEntities = g_render_flags & RENDER_ENTS;
			bool renderSpecial = g_render_flags & RENDER_SPECIAL;
			bool renderSpecialEnts = g_render_flags & RENDER_SPECIAL_ENTS;
			bool renderPointEnts = g_render_flags & RENDER_POINT_ENTS;
			bool renderOrigin = g_render_flags & RENDER_ORIGIN;
			bool renderWorldClipnodes = g_render_flags & RENDER_WORLD_CLIPNODES;
			bool renderEntClipnodes = g_render_flags & RENDER_ENT_CLIPNODES;
			bool renderEntConnections = g_render_flags & RENDER_ENT_CONNECTIONS;

			ImGui::Text("Render Flags:");

			ImGui::Columns(2, 0, false);

			if (ImGui::Checkbox("Textures", &renderTextures)) {
				g_render_flags ^= RENDER_TEXTURES;
			}
			if (ImGui::Checkbox("Lightmaps", &renderLightmaps)) {
				g_render_flags ^= RENDER_LIGHTMAPS;
				for (int i = 0; i < app->mapRenderers.size(); i++)
					app->mapRenderers[i]->updateModelShaders();
			}
			if (ImGui::Checkbox("Wireframe", &renderWireframe)) {
				g_render_flags ^= RENDER_WIREFRAME;
			}
			if (ImGui::Checkbox("Origin", &renderOrigin)) {
				g_render_flags ^= RENDER_ORIGIN;
			}
			if (ImGui::Checkbox("Entity Links", &renderEntConnections)) {
				g_render_flags ^= RENDER_ENT_CONNECTIONS;
				if (g_render_flags & RENDER_ENT_CONNECTIONS) {
					app->updateEntConnections();
				}
			}

			ImGui::NextColumn();

			if (ImGui::Checkbox("Point Entities", &renderPointEnts)) {
				g_render_flags ^= RENDER_POINT_ENTS;
			}
			if (ImGui::Checkbox("Normal Solid Entities", &renderEntities)) {
				g_render_flags ^= RENDER_ENTS;
			}
			if (ImGui::Checkbox("Special Solid Entities", &renderSpecialEnts)) {
				g_render_flags ^= RENDER_SPECIAL_ENTS;
			}
			if (ImGui::Checkbox("Special World Faces", &renderSpecial)) {
				g_render_flags ^= RENDER_SPECIAL;
			}


			ImGui::Columns(1);

			ImGui::Separator();

			ImGui::Text("Clipnode Rendering:");

			ImGui::Columns(2, 0, false);
			ImGui::RadioButton("Auto", &app->clipnodeRenderHull, -1);
			ImGui::RadioButton("0 - Point", &app->clipnodeRenderHull, 0);
			ImGui::RadioButton("1 - Human", &app->clipnodeRenderHull, 1);
			ImGui::RadioButton("2 - Large", &app->clipnodeRenderHull, 2);
			ImGui::RadioButton("3 - Head", &app->clipnodeRenderHull, 3);

			ImGui::NextColumn();

			if (ImGui::Checkbox("World Leaves", &renderWorldClipnodes)) {
				g_render_flags ^= RENDER_WORLD_CLIPNODES;
			}
			if (ImGui::Checkbox("Entity Leaves", &renderEntClipnodes)) {
				g_render_flags ^= RENDER_ENT_CLIPNODES;
			}
			static bool transparentNodes = true;
			if (ImGui::Checkbox("Transparency", &transparentNodes)) {
				for (int i = 0; i < g_app->mapRenderers.size(); i++) {
					g_app->mapRenderers[i]->updateClipnodeOpacity(transparentNodes ? 128 : 255);
				}
			}

			ImGui::Columns(1);
		}
		else if (settingsTab == 4) {
			ImGui::DragFloat("Movement speed", &app->moveSpeed, 0.1f, 0.1f, 1000, "%.1f");
			ImGui::DragFloat("Rotation speed", &app->rotationSpeed, 0.01f, 0.1f, 100, "%.1f");
		}

		ImGui::EndChild();
		ImGui::EndChild();

		if (settingsTab <= 2) {
			ImGui::Separator();

			if (ImGui::Button("Apply Changes")) {
				g_settings.gamedir = std::string(gamedir);
				g_settings.workingdir = std::string(workingdir);
				/* fixup gamedir */
				fixupPath(g_settings.gamedir, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE);
				snprintf(gamedir, sizeof(gamedir), "%s", g_settings.gamedir.c_str());

				if (g_settings.workingdir.find(':') == std::string::npos)
				{
					/* fixup workingdir */
					fixupPath(g_settings.workingdir, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
					snprintf(workingdir, sizeof(workingdir), "%s", g_settings.workingdir.c_str());
				}
				else
				{
					fixupPath(g_settings.workingdir, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
					snprintf(workingdir, sizeof(workingdir), "%s", g_settings.workingdir.c_str());
				}

				g_settings.fgdPaths.clear();
				for (auto& s : tmpFgdPaths)
				{
					std::string s2 = s.c_str();
					if (s2.find(':') == std::string::npos)
					{
						fixupPath(s2, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE);
					}
					else
					{
						fixupPath(s2, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE);
					}
					g_settings.fgdPaths.push_back(s2);
					s = s2;
				}
				g_settings.resPaths.clear();
				for (auto& s : tmpResPaths)
				{
					std::string s2 = s.c_str();
					if (s2.find(':') == std::string::npos)
					{
						fixupPath(s2, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
					}
					else
					{
						fixupPath(s2, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
					}
					g_settings.resPaths.push_back(s2);
					s = s2;
				}
				app->reloading = true;
				app->reloadingGameDir = true;
				app->loadFgds();
				app->postLoadFgds();
				app->reloadingGameDir = false;
				app->reloading = false;
				g_settings.save();
			}
		}


		ImGui::EndGroup();
	}
	ImGui::End();
}

void Gui::drawHelp() {
	ImGui::SetNextWindowSize(ImVec2(600.f, 400.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Help", &showHelpWidget)) {

		if (ImGui::BeginTabBar("##tabs"))
		{
			if (ImGui::BeginTabItem("UI Controls")) {
				ImGui::Dummy(ImVec2(0, 10));

				// user guide from the demo
				ImGuiIO& io = ImGui::GetIO();
				ImGui::BulletText("Click and drag on lower corner to resize window\n(double-click to auto fit window to its contents).");
				ImGui::BulletText("While adjusting numeric inputs:\n");
				ImGui::Indent();
				ImGui::BulletText("Hold SHIFT/ALT for faster/slower edit.");
				ImGui::BulletText("Double-click or CTRL+click to input value.");
				ImGui::Unindent();
				ImGui::BulletText("While inputing text:\n");
				ImGui::Indent();
				ImGui::BulletText("CTRL+A or double-click to select all.");
				ImGui::BulletText("CTRL+X/C/V to use clipboard cut/copy/paste.");
				ImGui::BulletText("CTRL+Z,CTRL+Y to undo/redo.");
				ImGui::BulletText("You can apply arithmetic operators +,*,/ on numerical values.\nUse +- to subtract.");
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("3D Controls")) {
				ImGui::Dummy(ImVec2(0, 10));

				ImGuiIO& io = ImGui::GetIO();
				ImGui::BulletText("WASD to move (hold SHIFT/CTRL for faster/slower movement).");
				ImGui::BulletText("Hold right mouse button to rotate view.");
				ImGui::BulletText("Left click to select objects/entities. Right click for options.");
				ImGui::BulletText("While grabbing an entity:\n");
				ImGui::Indent();
				ImGui::BulletText("Mouse wheel to push/pull (hold SHIFT/CTRL for faster/slower).");
				ImGui::BulletText("Click outside of the entity or press G to let go.");
				ImGui::Unindent();
				ImGui::BulletText("While grabbing 3D transform axes:\n");
				ImGui::Indent();
				ImGui::BulletText("Hold SHIFT/CTRL for faster/slower adjustments");
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Vertex Manipulation")) {
				ImGui::Dummy(ImVec2(0, 10));

				ImGuiIO& io = ImGui::GetIO();
				ImGui::BulletText("Press F to split a face while 2 edges are selected.");
				ImGui::Unindent();

				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void Gui::drawAbout() {
	ImGui::SetNextWindowSize(ImVec2(500.f, 140.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("About", &showAboutWidget)) {
		ImGui::InputText("Version", g_version_string, strlen(g_version_string), ImGuiInputTextFlags_ReadOnly);

		static char author[] = "w00tguy";
		ImGui::InputText("Author", author, strlen(author), ImGuiInputTextFlags_ReadOnly);

		static char url[] = "https://github.com/wootguy/bspguy";
		ImGui::InputText("Contact", url, strlen(url), ImGuiInputTextFlags_ReadOnly);
	}

	ImGui::End();
}

void Gui::drawMergeWindow() {
	ImGui::SetNextWindowSize(ImVec2(500.f, 240.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(500.f, 240.f), ImVec2(500.f, 240.f));
	static char Path[256];
	static bool DeleteUnusedInfo = true;
	static bool Optimize = false;
	static bool DeleteHull2 = false;
	static bool NoRipent = false;
	static bool NoScript = true;

	if (ImGui::Begin("Merge maps", &showMergeMapWidget)) {
		ImGui::InputText("output .bsp file", Path, 256);
		ImGui::Checkbox("Delete unused info", &DeleteUnusedInfo);
		ImGui::Checkbox("Optimize", &Optimize);
		ImGui::Checkbox("No hull 2", &DeleteHull2);
		ImGui::Checkbox("No ripent", &NoRipent);
		ImGui::Checkbox("No script", &NoScript);

		if (ImGui::Button("Merge maps", ImVec2(120, 0)))
		{
			std::vector<Bsp*> maps;
			for (auto const& s : g_app->mapRenderers)
			{
				if (!s->map->is_model)
					maps.push_back(s->map);
			}
			if (maps.size() < 2)
			{
				logf("ERROR: at least 2 input maps are required\n");
			}
			else
			{
				for (int i = 0; i < maps.size(); i++) {
					logf("Preprocessing %s:\n", maps[i]->name.c_str());
					if (DeleteUnusedInfo)
					{
						logf("    Deleting unused data...\n");
						STRUCTCOUNT removed = maps[i]->remove_unused_model_structures();
						g_progress.clear();
						removed.print_delete_stats(2);
					}

					if (DeleteHull2 || (Optimize && !maps[i]->has_hull2_ents())) {
						logf("    Deleting hull 2...\n");
						maps[i]->delete_hull(2, 1);
						maps[i]->remove_unused_model_structures().print_delete_stats(2);
					}

					if (Optimize) {
						logf("    Optmizing...\n");
						maps[i]->delete_unused_hulls().print_delete_stats(2);
					}

					logf("\n");
				}

				BspMerger merger;
				Bsp* result = merger.merge(maps, vec3(), Path, NoRipent, NoScript);

				logf("\n");
				if (result->isValid()) result->write(Path);
				logf("\n");
				result->print_info(false, 0, 0);

				g_app->clearMaps();

				fixupPath(Path, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);

				if (fileExists(Path))
				{
					result = new Bsp(Path);
					g_app->addMap(result);
				}
				else
				{
					logf("Error while map merge!\n");
					g_app->addMap(new Bsp());
				}
			}
			showMergeMapWidget = false;
		}
	}

	ImGui::End();
}

void Gui::drawImportMapWidget() {
	ImGui::SetNextWindowSize(ImVec2(500.f, 140.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(500.f, 140.f), ImVec2(500.f, 140.f));
	static char Path[256];
	const char* title = "Import .bsp model as func_breakable entity";

	if (showImportMapWidget_Type == SHOW_IMPORT_OPEN)
	{
		title = "Open map";
	}
	else if (showImportMapWidget_Type == SHOW_IMPORT_ADD_NEW)
	{
		title = "Add map to renderer";
	}

	if (ImGui::Begin(title, &showImportMapWidget)) {
		ImGui::InputText(".bsp file", Path, 256);
		if (ImGui::Button("Load", ImVec2(120, 0)))
		{
			fixupPath(Path, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);
			if (fileExists(Path))
			{
				logf("Loading new map file from %s path.\n", Path);
				showImportMapWidget = false;
				if (showImportMapWidget_Type == SHOW_IMPORT_ADD_NEW)
				{
					g_app->addMap(new Bsp(Path));
				}
				else if (showImportMapWidget_Type == SHOW_IMPORT_OPEN)
				{
					g_app->clearMaps();
					g_app->addMap(new Bsp(Path));
				}
				else
				{
					if (g_app->mapRenderers.size() && g_app->mapRenderers[0]->map)
					{
						Bsp* model = new Bsp(Path);
						if (!model->ents.size())
						{
							logf("Error! No worldspawn found!\n");
						}
						else
						{
							Bsp* map = g_app->getSelectedMap();
							logf("Binding .bsp model to func_breakable.\n");
							Entity* tmpEnt = new Entity("func_breakable");
							tmpEnt->setOrAddKeyvalue("gibmodel", std::string("models/") + basename(Path));
							tmpEnt->setOrAddKeyvalue("model", std::string("models/") + basename(Path));
							tmpEnt->setOrAddKeyvalue("spawnflags", "1");
							tmpEnt->setOrAddKeyvalue("origin", g_app->cameraOrigin.toKeyvalueString());
							map->ents.push_back(tmpEnt);
							map->update_ent_lump();
							logf("Success! Now you needs to copy model to path: %s\n", (std::string("models/") + basename(Path)).c_str());
							app->updateEnts();
							app->reloadBspModels();
						}
						delete model;
					}
				}
			}
			else
			{
				logf("No file found! Try again!\n");
			}
		}
	}
	ImGui::End();
}

void Gui::drawLimits() {
	ImGui::SetNextWindowSize(ImVec2(550.f, 630.f), ImGuiCond_FirstUseEver);

	Bsp* map = app->getSelectedMap();
	std::string title = map ? "Limits - " + map->name : "Limits";

	if (ImGui::Begin((title + "###limits").c_str(), &showLimitsWidget)) {

		if (!map) {
			ImGui::Text("No map selected");
		}
		else {
			if (ImGui::BeginTabBar("##tabs"))
			{
				if (ImGui::BeginTabItem("Summary")) {

					if (!loadedStats) {
						stats.clear();
						stats.push_back(calcStat("models", map->modelCount, MAX_MAP_MODELS, false));
						stats.push_back(calcStat("planes", map->planeCount, MAX_MAP_PLANES, false));
						stats.push_back(calcStat("vertexes", map->vertCount, MAX_MAP_VERTS, false));
						stats.push_back(calcStat("nodes", map->nodeCount, MAX_MAP_NODES, false));
						stats.push_back(calcStat("texinfos", map->texinfoCount, MAX_MAP_TEXINFOS, false));
						stats.push_back(calcStat("faces", map->faceCount, MAX_MAP_FACES, false));
						stats.push_back(calcStat("clipnodes", map->clipnodeCount, MAX_MAP_CLIPNODES, false));
						stats.push_back(calcStat("leaves", map->leafCount, MAX_MAP_LEAVES, false));
						stats.push_back(calcStat("marksurfaces", map->marksurfCount, MAX_MAP_MARKSURFS, false));
						stats.push_back(calcStat("surfedges", map->surfedgeCount, MAX_MAP_SURFEDGES, false));
						stats.push_back(calcStat("edges", map->edgeCount, MAX_MAP_EDGES, false));
						stats.push_back(calcStat("textures", map->textureCount, MAX_MAP_TEXTURES, false));
						stats.push_back(calcStat("lightdata", map->lightDataLength, MAX_MAP_LIGHTDATA, true));
						stats.push_back(calcStat("visdata", map->visDataLength, MAX_MAP_VISDATA, true));
						stats.push_back(calcStat("entities", (unsigned int) map->ents.size(), MAX_MAP_ENTS, false));
						loadedStats = true;
					}

					ImGui::BeginChild("content");
					ImGui::Dummy(ImVec2(0, 10));
					ImGui::PushFont(consoleFontLarge);

					float midWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "    Current / Max    ").x;
					float otherWidth = (ImGui::GetWindowWidth() - midWidth) / 2;
					ImGui::Columns(3);
					ImGui::SetColumnWidth(0, otherWidth);
					ImGui::SetColumnWidth(1, midWidth);
					ImGui::SetColumnWidth(2, otherWidth);

					ImGui::Text("Data Type"); ImGui::NextColumn();
					ImGui::Text(" Current / Max"); ImGui::NextColumn();
					ImGui::Text("Fullness"); ImGui::NextColumn();

					ImGui::Columns(1);
					ImGui::Separator();
					ImGui::BeginChild("chart");
					ImGui::Columns(3);
					ImGui::SetColumnWidth(0, otherWidth);
					ImGui::SetColumnWidth(1, midWidth);
					ImGui::SetColumnWidth(2, otherWidth);

					for (int i = 0; i < stats.size(); i++) {
						ImGui::TextColored(stats[i].color, stats[i].name.c_str()); ImGui::NextColumn();

						std::string val = stats[i].val + " / " + stats[i].max;
						ImGui::TextColored(stats[i].color, val.c_str());
						ImGui::NextColumn();

						ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5f, 0.4f, 0, 1));
						ImGui::ProgressBar(stats[i].progress, ImVec2(-1, 0), stats[i].fullness.c_str());
						ImGui::PopStyleColor(1);
						ImGui::NextColumn();
					}

					ImGui::Columns(1);
					ImGui::EndChild();
					ImGui::PopFont();
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Clipnodes")) {
					drawLimitTab(map, SORT_CLIPNODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Nodes")) {
					drawLimitTab(map, SORT_NODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Faces")) {
					drawLimitTab(map, SORT_FACES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Vertices")) {
					drawLimitTab(map, SORT_VERTS);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}
	}

	ImGui::End();
}

void Gui::drawLimitTab(Bsp* map, int sortMode) {

	int maxCount;
	const char* countName = "None";
	switch (sortMode) {
	case SORT_VERTS:		maxCount = map->vertCount; countName = "Vertexes";  break;
	case SORT_NODES:		maxCount = map->nodeCount; countName = "Nodes";  break;
	case SORT_CLIPNODES:	maxCount = map->clipnodeCount; countName = "Clipnodes";  break;
	case SORT_FACES:		maxCount = map->faceCount; countName = "Faces";  break;
	}

	if (!loadedLimit[sortMode]) {
		std::vector<STRUCTUSAGE*> modelInfos = map->get_sorted_model_infos(sortMode);

		limitModels[sortMode].clear();
		for (int i = 0; i < modelInfos.size(); i++) {

			int val;
			switch (sortMode) {
			case SORT_VERTS:		val = modelInfos[i]->sum.verts; break;
			case SORT_NODES:		val = modelInfos[i]->sum.nodes; break;
			case SORT_CLIPNODES:	val = modelInfos[i]->sum.clipnodes; break;
			case SORT_FACES:		val = modelInfos[i]->sum.faces; break;
			}

			ModelInfo stat = calcModelStat(map, modelInfos[i], val, maxCount, false);
			limitModels[sortMode].push_back(stat);
			delete modelInfos[i];
		}
		loadedLimit[sortMode] = true;
	}
	std::vector<ModelInfo>& modelInfos = limitModels[sortMode];

	ImGui::BeginChild("content");
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::PushFont(consoleFontLarge);

	float valWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, " Clipnodes ").x;
	float usageWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "  Usage   ").x;
	float modelWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, " Model ").x;
	float bigWidth = ImGui::GetWindowWidth() - (valWidth + usageWidth + modelWidth);
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	ImGui::Text("Classname"); ImGui::NextColumn();
	ImGui::Text("Model"); ImGui::NextColumn();
	ImGui::Text(countName); ImGui::NextColumn();
	ImGui::Text("Usage"); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild("chart");
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	int selected = app->pickInfo.entIdx;

	for (int i = 0; i < limitModels[sortMode].size(); i++) {

		if (modelInfos[i].val == "0") {
			break;
		}

		std::string cname = modelInfos[i].classname + "##" + "select" + std::to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(cname.c_str(), selected == modelInfos[i].entIdx, flags)) {
			selected = i;
			int entIdx = modelInfos[i].entIdx;
			if (entIdx < map->ents.size()) {
				Entity* ent = map->ents[entIdx];
				app->pickInfo.ent = ent;
				app->pickInfo.entIdx = entIdx;
				app->pickInfo.modelIdx = map->ents[entIdx]->getBspModelIdx();
				// map should already be valid if limits are showing

				if (ImGui::IsMouseDoubleClicked(0)) {
					app->goToEnt(map, entIdx);
				}
			}
		}
		ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].model.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].model.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].val.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].val.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].usage.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].usage.c_str()); ImGui::NextColumn();
	}


	ImGui::Columns(1);
	ImGui::EndChild();

	ImGui::PopFont();
	ImGui::EndChild();
}

void Gui::drawEntityReport() {
	ImGui::SetNextWindowSize(ImVec2(550.f, 630.f), ImGuiCond_FirstUseEver);

	Bsp* map = app->getSelectedMap();

	std::string title = map ? "Entity Report - " + map->name : "Entity Report";

	if (ImGui::Begin((title + "###entreport").c_str(), &showEntityReport)) {
		if (!map) {
			ImGui::Text("No map selected");
		}
		else {
			ImGui::BeginGroup();
			static int lastmapidx = -1;
			const int MAX_FILTERS = 1;
			static char keyFilter[MAX_FILTERS][MAX_KEY_LEN];
			static char valueFilter[MAX_FILTERS][MAX_VAL_LEN];
			static int lastSelect = -1;
			static std::string classFilter = "(none)";
			static bool partialMatches = true;
			static std::vector<int> visibleEnts;
			static std::vector<bool> selectedItems;

			const ImGuiKeyModFlags expected_key_mod_flags = ImGui::GetMergedKeyModFlags();

			float footerHeight = ImGui::GetFrameHeightWithSpacing() * 5.f + 16.f;
			ImGui::BeginChild("entlist", ImVec2(0.f, -footerHeight));

			filterNeeded = app->getSelectedMapId() != lastmapidx;
			lastmapidx = app->getSelectedMapId();

			if (filterNeeded) {
				visibleEnts.clear();
				for (int i = 1; i < map->ents.size(); i++) {
					Entity* ent = map->ents[i];
					std::string cname = ent->keyvalues["classname"];

					bool visible = true;

					if (!classFilter.empty() && classFilter != "(none)") {
						if (toLowerCase(cname) != toLowerCase(classFilter)) {
							visible = false;
						}
					}

					for (int k = 0; k < MAX_FILTERS; k++) {
						if (keyFilter[k][0] != '\0') {
							std::string searchKey = trimSpaces(toLowerCase(keyFilter[k]));

							bool foundKey = false;
							std::string actualKey;
							for (int c = 0; c < ent->keyOrder.size(); c++) {
								std::string key = toLowerCase(ent->keyOrder[c]);
								if (key == searchKey || (partialMatches && key.find(searchKey) != std::string::npos)) {
									foundKey = true;
									actualKey = std::move(key);
									break;
								}
							}
							if (!foundKey) {
								visible = false;
								break;
							}

							std::string searchValue = trimSpaces(toLowerCase(valueFilter[k]));
							if (!searchValue.empty()) {
								if ((partialMatches && ent->keyvalues[actualKey].find(searchValue) == std::string::npos) ||
									(!partialMatches && ent->keyvalues[actualKey] != searchValue)) {
									visible = false;
									break;
								}
							}
						}
						else if (valueFilter[k][0] != '\0') {
							std::string searchValue = trimSpaces(toLowerCase(valueFilter[k]));
							bool foundMatch = false;
							for (int c = 0; c < ent->keyOrder.size(); c++) {
								std::string val = toLowerCase(ent->keyvalues[ent->keyOrder[c]]);
								if (val == searchValue || (partialMatches && val.find(searchValue) != std::string::npos)) {
									foundMatch = true;
									break;
								}
							}
							if (!foundMatch) {
								visible = false;
								break;
							}
						}
					}
					if (visible) {
						visibleEnts.push_back(i);
					}
				}

				selectedItems.clear();
				selectedItems.resize(visibleEnts.size());
				for (int k = 0; k < selectedItems.size(); k++)
					selectedItems[k] = false;
			}
			filterNeeded = false;

			ImGuiListClipper clipper;
			clipper.Begin((int)visibleEnts.size());

			while (clipper.Step())
			{
				for (int line = clipper.DisplayStart; line < clipper.DisplayEnd && line < visibleEnts.size() && visibleEnts[line] < map->ents.size(); line++)
				{
					int i = line;
					int entIdx = visibleEnts[i];
					Entity* ent = map->ents[entIdx];
					std::string cname = ent->hasKey("classname") ? ent->keyvalues["classname"] : "";

					if (cname.length() && ImGui::Selectable((cname + "##ent" + std::to_string(i)).c_str(), selectedItems[i], ImGuiSelectableFlags_AllowDoubleClick)) {
						if (expected_key_mod_flags & ImGuiKeyModFlags_Ctrl) {
							selectedItems[i] = !selectedItems[i];
							lastSelect = i;
						}
						else if (expected_key_mod_flags & ImGuiKeyModFlags_Shift) {
							if (lastSelect >= 0) {
								int begin = i > lastSelect ? lastSelect : i;
								int end = i > lastSelect ? i : lastSelect;
								for (int k = 0; k < selectedItems.size(); k++)
									selectedItems[k] = false;
								for (int k = begin; k < end; k++)
									selectedItems[k] = true;
								selectedItems[lastSelect] = true;
								selectedItems[i] = true;
							}
						}
						else {
							for (int k = 0; k < selectedItems.size(); k++)
								selectedItems[k] = false;
							selectedItems[i] = true;
							lastSelect = i;
						}

						if (ImGui::IsMouseDoubleClicked(0)) {
							app->goToEnt(map, entIdx);
						}

						g_app->selectEnt(map, entIdx);
					}

					if (selectedItems[i] && ImGui::IsItemHovered() && ImGui::IsMouseReleased(1)) {
						ImGui::OpenPopup("ent_report_context");
					}
				}
			}
			clipper.End();
			if (ImGui::BeginPopup("ent_report_context"))
			{
				if (ImGui::MenuItem("Delete")) {
					std::vector<Entity*> newEnts;

					std::set<int> selectedEnts;
					for (int i = 0; i < selectedItems.size(); i++) {
						if (selectedItems[i])
							selectedEnts.insert(visibleEnts[i]);
					}

					for (int i = 0; i < map->ents.size(); i++) {
						if (selectedEnts.find(i) != selectedEnts.end()) {
							delete map->ents[i];
						}
						else {
							newEnts.push_back(map->ents[i]);
						}
					}
					map->ents = std::move(newEnts);
					app->deselectObject();
					map->getBspRender()->preRenderEnts();
					reloadLimits();
					filterNeeded = true;
				}

				ImGui::EndPopup();
			}

			ImGui::EndChild();

			ImGui::BeginChild("filters");

			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, 8));

			static std::vector<std::string> usedClasses;
			static std::set<std::string> uniqueClasses;

			static bool comboWasOpen = false;

			ImGui::Text("Classname Filter");
			if (ImGui::BeginCombo("##classfilter", classFilter.c_str()))
			{
				if (!comboWasOpen) {
					comboWasOpen = true;

					usedClasses.clear();
					uniqueClasses.clear();
					usedClasses.push_back("(none)");

					for (int i = 1; i < map->ents.size(); i++) {
						Entity* ent = map->ents[i];
						std::string cname = ent->keyvalues["classname"];

						if (uniqueClasses.find(cname) == uniqueClasses.end()) {
							usedClasses.push_back(cname);
							uniqueClasses.insert(cname);
						}
					}
					sort(usedClasses.begin(), usedClasses.end());

				}
				for (int k = 0; k < usedClasses.size(); k++) {
					bool selected = usedClasses[k] == classFilter;
					if (ImGui::Selectable(usedClasses[k].c_str(), selected)) {
						classFilter = usedClasses[k];
						filterNeeded = true;
					}
				}

				ImGui::EndCombo();
			}
			else {
				comboWasOpen = false;
			}

			ImGui::Dummy(ImVec2(0, 8));
			ImGui::Text("Keyvalue Filter");

			ImGuiStyle& style = ImGui::GetStyle();
			float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
			float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.5f;
			inputWidth -= smallFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " = ").x;

			for (int i = 0; i < MAX_FILTERS; i++) {
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Key" + std::to_string(i)).c_str(), keyFilter[i], 64)) {
					filterNeeded = true;
				}
				ImGui::SameLine();
				ImGui::Text(" = "); ImGui::SameLine();
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Value" + std::to_string(i)).c_str(), valueFilter[i], 64)) {
					filterNeeded = true;
				}
			}

			if (ImGui::Checkbox("Partial Matching", &partialMatches)) {
				filterNeeded = true;
			}

			ImGui::EndChild();

			ImGui::EndGroup();
		}
	}

	ImGui::End();
}


static bool ColorPicker(float* col, bool alphabar)
{
	const int    EDGE_SIZE = 200; // = int( ImGui::GetWindowWidth() * 0.75f );
	const ImVec2 SV_PICKER_SIZE = ImVec2(EDGE_SIZE, EDGE_SIZE);
	const float  SPACING = ImGui::GetStyle().ItemInnerSpacing.x;
	const float  HUE_PICKER_WIDTH = 20.f;
	const float  CROSSHAIR_SIZE = 7.0f;

	ImColor color(col[0], col[1], col[2]);
	bool value_changed = false;

	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	// setup

	ImVec2 picker_pos = ImGui::GetCursorScreenPos();

	float hue, saturation, value;
	ImGui::ColorConvertRGBtoHSV(
		color.Value.x, color.Value.y, color.Value.z, hue, saturation, value);

	// draw hue bar

	ImColor colors[] = { ImColor(255, 0, 0),
		ImColor(255, 255, 0),
		ImColor(0, 255, 0),
		ImColor(0, 255, 255),
		ImColor(0, 0, 255),
		ImColor(255, 0, 255),
		ImColor(255, 0, 0) };

	for (int i = 0; i < 6; ++i)
	{
		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING, picker_pos.y + i * (SV_PICKER_SIZE.y / 6)),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING + HUE_PICKER_WIDTH,
				picker_pos.y + (i + 1) * (SV_PICKER_SIZE.y / 6)),
			colors[i],
			colors[i],
			colors[i + 1],
			colors[i + 1]);
	}

	draw_list->AddLine(
		ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING - 2, picker_pos.y + hue * SV_PICKER_SIZE.y),
		ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING + 2 + HUE_PICKER_WIDTH, picker_pos.y + hue * SV_PICKER_SIZE.y),
		ImColor(255, 255, 255));

	// draw alpha bar

	if (alphabar) {
		float alpha = col[3];

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * SPACING + HUE_PICKER_WIDTH, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * SPACING + 2 * HUE_PICKER_WIDTH, picker_pos.y + SV_PICKER_SIZE.y),
			ImColor(0, 0, 0), ImColor(0, 0, 0), ImColor(255, 255, 255), ImColor(255, 255, 255));

		draw_list->AddLine(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * (SPACING - 2) + HUE_PICKER_WIDTH, picker_pos.y + alpha * SV_PICKER_SIZE.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * (SPACING + 2) + 2 * HUE_PICKER_WIDTH, picker_pos.y + alpha * SV_PICKER_SIZE.y),
			ImColor(255.f - alpha, 255.f, 255.f));
	}

	// draw color matrix

	{
		const ImU32 c_oColorBlack = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, 1.f));
		const ImU32 c_oColorBlackTransparent = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, 0.f));
		const ImU32 c_oColorWhite = ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, 1.f));

		ImVec4 cHueValue(1, 1, 1, 1);
		ImGui::ColorConvertHSVtoRGB(hue, 1, 1, cHueValue.x, cHueValue.y, cHueValue.z);
		ImU32 oHueColor = ImGui::ColorConvertFloat4ToU32(cHueValue);

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x, picker_pos.y + SV_PICKER_SIZE.y),
			c_oColorWhite,
			oHueColor,
			oHueColor,
			c_oColorWhite
		);

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x, picker_pos.y + SV_PICKER_SIZE.y),
			c_oColorBlackTransparent,
			c_oColorBlackTransparent,
			c_oColorBlack,
			c_oColorBlack
		);
	}

	// draw cross-hair

	float x = saturation * SV_PICKER_SIZE.x;
	float y = (1 - value) * SV_PICKER_SIZE.y;
	ImVec2 p(picker_pos.x + x, picker_pos.y + y);
	draw_list->AddLine(ImVec2(p.x - CROSSHAIR_SIZE, p.y), ImVec2(p.x - 2, p.y), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x + CROSSHAIR_SIZE, p.y), ImVec2(p.x + 2, p.y), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x, p.y + CROSSHAIR_SIZE), ImVec2(p.x, p.y + 2), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x, p.y - CROSSHAIR_SIZE), ImVec2(p.x, p.y - 2), ImColor(255, 255, 255));

	// color matrix logic

	ImGui::InvisibleButton("saturation_value_selector", SV_PICKER_SIZE);

	if (ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0])
	{
		ImVec2 mouse_pos_in_canvas = ImVec2(
			ImGui::GetIO().MousePos.x - picker_pos.x, ImGui::GetIO().MousePos.y - picker_pos.y);

		/**/ if (mouse_pos_in_canvas.x < 0) mouse_pos_in_canvas.x = 0;
		else if (mouse_pos_in_canvas.x >= SV_PICKER_SIZE.x - 1) mouse_pos_in_canvas.x = SV_PICKER_SIZE.x - 1;

		/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
		else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

		value = 1 - (mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1));
		saturation = mouse_pos_in_canvas.x / (SV_PICKER_SIZE.x - 1);
		value_changed = true;
	}

	// hue bar logic

	ImGui::SetCursorScreenPos(ImVec2(picker_pos.x + SPACING + SV_PICKER_SIZE.x, picker_pos.y));
	ImGui::InvisibleButton("hue_selector", ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

	if (ImGui::GetIO().MouseDown[0] && (ImGui::IsItemHovered() || ImGui::IsItemActive()))
	{
		ImVec2 mouse_pos_in_canvas = ImVec2(
			ImGui::GetIO().MousePos.x - picker_pos.x, ImGui::GetIO().MousePos.y - picker_pos.y);

		/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
		else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

		hue = mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1);
		value_changed = true;
	}

	// alpha bar logic

	if (alphabar) {

		ImGui::SetCursorScreenPos(ImVec2(picker_pos.x + SPACING * 2 + HUE_PICKER_WIDTH + SV_PICKER_SIZE.x, picker_pos.y));
		ImGui::InvisibleButton("alpha_selector", ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

		if (ImGui::GetIO().MouseDown[0] && (ImGui::IsItemHovered() || ImGui::IsItemActive()))
		{
			ImVec2 mouse_pos_in_canvas = ImVec2(
				ImGui::GetIO().MousePos.x - picker_pos.x, ImGui::GetIO().MousePos.y - picker_pos.y);

			/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
			else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

			float alpha = mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1);
			col[3] = alpha;
			value_changed = true;
		}

	}

	// R,G,B or H,S,V color editor

	color = ImColor::HSV(hue >= 1.f ? hue - 10.f * (float)1e-6 : hue, saturation > 0.f ? saturation : 10.f * (float)1e-6, value > 0.f ? value : (float)1e-6);
	col[0] = color.Value.x;
	col[1] = color.Value.y;
	col[2] = color.Value.z;

	bool widget_used;
	ImGui::PushItemWidth((alphabar ? SPACING + HUE_PICKER_WIDTH : 0) +
		SV_PICKER_SIZE.x + SPACING + HUE_PICKER_WIDTH - 2 * ImGui::GetStyle().FramePadding.x);
	widget_used = alphabar ? ImGui::ColorEdit4("", col) : ImGui::ColorEdit3("", col);
	ImGui::PopItemWidth();

	// try to cancel hue wrap (after ColorEdit), if any
	{
		float new_hue, new_sat, new_val;
		ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], new_hue, new_sat, new_val);
		if (new_hue <= 0 && hue > 0) {
			if (new_val <= 0 && value != new_val) {
				color = ImColor::HSV(hue, saturation, new_val <= 0 ? value * 0.5f : new_val);
				col[0] = color.Value.x;
				col[1] = color.Value.y;
				col[2] = color.Value.z;
			}
			else
				if (new_sat <= 0) {
					color = ImColor::HSV(hue, new_sat <= 0 ? saturation * 0.5f : new_sat, new_val);
					col[0] = color.Value.x;
					col[1] = color.Value.y;
					col[2] = color.Value.z;
				}
		}
	}
	return value_changed || widget_used;
}

bool ColorPicker3(float col[3]) {
	return ColorPicker(col, false);
}

bool ColorPicker4(float col[4]) {
	return ColorPicker(col, true);
}


int ArrayXYtoId(int width, int x, int y)
{
	return  width * y + x;
}

std::vector<COLOR3> colordata;

int max_x_width = 512;

void DrawImageAtOneBigLightMap(COLOR3* img, int w, int h, int x, int y)
{
	for (int x1 = 0; x1 < w; x1++)
	{
		for (int y1 = 0; y1 < h; y1++)
		{
			int offset = ArrayXYtoId(w, x1, y1);
			int offset2 = ArrayXYtoId(max_x_width, x + x1, y + y1);
			while (offset2 >= colordata.size())
			{
				colordata.push_back(COLOR3(0, 0, 255));
			}
			colordata[offset2] = img[offset];
		}
	}
}

void DrawOneBigLightMapAtImage(COLOR3* img, unsigned int len, int w, int h, int x, int y)
{
	for (int x1 = 0; x1 < w; x1++)
	{
		for (int y1 = 0; y1 < h; y1++)
		{
			int offset = ArrayXYtoId(w, x1, y1);
			int offset2 = ArrayXYtoId(max_x_width, x + x1, y + y1);
			img[offset] = colordata[offset2];
		}
	}
}

void ExportOneBigLightmapFile(const char* path, int x, int y)
{
	std::vector<COLOR3> copycolordata;
	std::copy(colordata.begin(), colordata.end(), std::back_inserter(copycolordata));
	lodepng_encode24_file(path, (unsigned char*)(&copycolordata[0]), x, y);
}


void ImportOneBigLightmapFile(Bsp* map)
{
	char fileNam[256];
	colordata = std::vector<COLOR3>();

	int current_x = 0;
	int current_y = 0;
	int max_y_found = 0;

	std::vector<int> faces_to_import;

	if (g_app->selectedFaces.size() > 1)
	{
		faces_to_import = g_app->selectedFaces;
	}
	else
	{
		for (unsigned int faceIdx = 0; faceIdx < map->faceCount; faceIdx++)
		{
			faces_to_import.push_back(faceIdx);
		}
	}

	for (int lightId = 0; lightId < MAXLIGHTMAPS; lightId++)
	{
		snprintf(fileNam, sizeof(fileNam), "%s%sFull%dStyle.png", GetWorkDir().c_str(), "lightmap", lightId);
		unsigned char* image_bytes;
		unsigned int w2, h2;
		auto error = lodepng_decode24_file(&image_bytes, &w2, &h2, fileNam);
		if (error == 0 && image_bytes)
		{
			colordata.resize(w2 * h2);
			memcpy(&colordata[0], image_bytes, w2 * h2 * sizeof(COLOR3));
			for (int faceIdx : faces_to_import)
			{
				int size[2];
				GetFaceLightmapSize(map, faceIdx, size);
				int x_width = size[0], y_height = size[1];
				if (map->faces[faceIdx].nLightmapOffset < 0 || map->faces[faceIdx].nStyles[lightId] == 255)
					continue;
				int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
				int offset = map->faces[faceIdx].nLightmapOffset + lightId * lightmapSz;
				if (y_height > max_y_found)
					max_y_found = y_height;
				if (current_x + x_width > max_x_width)
				{
					current_y += max_y_found;
					max_y_found = 0;
					current_x = 0;
				}
				current_x += x_width;
				unsigned char* lightmapData = new unsigned char[lightmapSz];
				DrawOneBigLightMapAtImage((COLOR3*)(lightmapData), lightmapSz, x_width, y_height, current_x, current_y);
				memcpy((unsigned char*)(map->lightdata + offset), lightmapData, lightmapSz);
				delete[] lightmapData;
			}
		}
	}
}

float RandomFloat(float a, float b) {
	float random = ((float)rand()) / (float)RAND_MAX;
	float diff = b - a;
	float r = random * diff;
	return a + r;
}

std::map<float, float> mapx;
std::map<float, float> mapy;
std::map<float, float> mapz;

void ExportOneBigLightmap(Bsp* map)
{
	char fileNam[256];

	colordata = std::vector<COLOR3>();


	int current_x = 0;
	int current_y = 0;

	int max_y_found = 0;

	std::vector<int> faces_to_export;

	if (g_app->selectedFaces.size() > 1)
	{
		faces_to_export = g_app->selectedFaces;
	}
	else
	{
		for (unsigned int faceIdx = 0; faceIdx < map->faceCount; faceIdx++)
		{
			faces_to_export.push_back(faceIdx);
		}
	}

	/*std::vector<vec3> verts;
	for (int i = 0; i < map->vertCount; i++)
	{
		verts.push_back(map->verts[i]);
	}
	std::reverse(verts.begin(), verts.end());
	for (int i = 0; i < map->vertCount; i++)
	{
		map->verts[i] = verts[i];
	}*/
	/*for (int i = 0; i < map->vertCount; i++)
	{
		vec3* vector = &map->verts[i];
		vector->y *= -1;
		vector->x *= -1;
		/*if (mapz.find(vector->z) == mapz.end())
			mapz[vector->z] = RandomFloat(-100, 100);
		vector->z -= mapz[vector->z];*/

		/*if (mapx.find(vector->x) == mapx.end())
			mapx[vector->x] = RandomFloat(-50, 50);
		vector->x += mapx[vector->x];

		if (mapy.find(vector->y) == mapy.end())
			mapy[vector->y] = RandomFloat(-50, 50);
		vector->y -= mapy[vector->y];


		/*vector->x *= static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
		vector->y *= static_cast <float> (rand()) / static_cast <float> (RAND_MAX);*/
	/* }

	map->update_lump_pointers();*/


	for (int lightId = 0; lightId < MAXLIGHTMAPS; lightId++)
	{
		for (int faceIdx : faces_to_export)
		{
			int size[2];
			GetFaceLightmapSize(map, faceIdx, size);
			int x_width = size[0], y_height = size[1];
			if (map->faces[faceIdx].nLightmapOffset < 0 || map->faces[faceIdx].nStyles[lightId] == 255)
				continue;
			int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
			int offset = map->faces[faceIdx].nLightmapOffset + lightId * lightmapSz;
			if (y_height > max_y_found)
				max_y_found = y_height;

			if (current_x + x_width > max_x_width)
			{
				current_y += max_y_found;
				max_y_found = 0;
				current_x = 0;
			}
			current_x += x_width;
			DrawImageAtOneBigLightMap((COLOR3*)(map->lightdata + offset), x_width, y_height, current_x, current_y);
		}
		snprintf(fileNam, sizeof(fileNam), "%s%sFull%dStyle.png", GetWorkDir().c_str(), "lightmap", lightId);
		ExportOneBigLightmapFile(fileNam, max_x_width, current_y + max_y_found);
	}

}

void ExportLightmaps(BSPFACE face, int faceIdx, Bsp* map)
{
	int size[2];
	GetFaceLightmapSize(map, faceIdx, size);
	char fileNam[256];

	for (int i = 0; i < MAXLIGHTMAPS; i++) {
		if (face.nStyles[i] == 255)
			continue;
		int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
		int offset = face.nLightmapOffset + i * lightmapSz;
		snprintf(fileNam, sizeof(fileNam), "%s%s_FACE%i-STYLE%i.png", GetWorkDir().c_str(), "lightmap", faceIdx, i);
		logf("Exporting %s\n", fileNam);
		createDir(GetWorkDir());
		lodepng_encode24_file(fileNam, (unsigned char*)(map->lightdata + offset), size[0], size[1]);
	}
}

void ImportLightmaps(BSPFACE face, int faceIdx, Bsp* map)
{
	char fileNam[256];
	int size[2];
	GetFaceLightmapSize(map, faceIdx, size);
	for (int i = 0; i < MAXLIGHTMAPS; i++) {
		if (face.nStyles[i] == 255)
			continue;
		int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
		int offset = face.nLightmapOffset + i * lightmapSz;
		snprintf(fileNam, sizeof(fileNam), "%s%s_FACE%i-STYLE%i.png", GetWorkDir().c_str(), "lightmap", faceIdx, i);
		unsigned int w = size[0], h = size[1];
		unsigned int w2 = 0, h2 = 0;
		logf("Importing %s\n", fileNam);
		unsigned char* image_bytes = NULL;
		auto error = lodepng_decode24_file(&image_bytes, &w2, &h2, fileNam);
		if (error == 0 && image_bytes)
		{
			if (w == w2 && h == h2)
			{
				memcpy((unsigned char*)(map->lightdata + offset), image_bytes, lightmapSz);
			}
			else
			{
				logf("Invalid lightmap size! Need %dx%d 24bit png!\n", w, h);
			}
			free(image_bytes);
		}
		else
		{
			logf("Invalid lightmap image format. Need 24bit png!\n");
		}
	}
}

void Gui::drawLightMapTool() {
	static float colourPatch[3];
	static Texture* currentlightMap[MAXLIGHTMAPS] = { NULL };
	static float windowWidth = 550;
	static float windowHeight = 520;
	static int lightmaps = 0;
	const char* light_names[] =
	{
		"OFF",
		"Main light",
		"Light 1",
		"Light 2",
		"Light 3"
	};
	static int type = 0;

	ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, windowHeight), ImVec2(windowWidth, FLT_MAX));

	if (ImGui::Begin("LightMap Editor (WIP)", &showLightmapEditorWidget)) {
		ImGui::Dummy(ImVec2(windowWidth / 2.45f, 10.0f));
		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Almost always breaks lightmaps if changed.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		Bsp* map = app->getSelectedMap();
		if (map && app->selectedFaces.size())
		{
			int faceIdx = app->selectedFaces[0];
			BSPFACE& face = map->faces[faceIdx];
			int size[2];
			GetFaceLightmapSize(map, faceIdx, size);
			if (showLightmapEditorUpdate)
			{
				lightmaps = 0;

				{
					for (int i = 0; i < MAXLIGHTMAPS; i++)
					{
						if (currentlightMap[i])
							delete currentlightMap[i];
						currentlightMap[i] = NULL;
					}
					for (int i = 0; i < MAXLIGHTMAPS; i++) {
						if (face.nStyles[i] == 255)
							continue;
						currentlightMap[i] = new Texture(size[0], size[1]);
						int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
						int offset = face.nLightmapOffset + i * lightmapSz;
						memcpy(currentlightMap[i]->data, map->lightdata + offset, lightmapSz);
						currentlightMap[i]->upload(GL_RGB, true);
						lightmaps++;
						//logf("upload %d style at offset %d\n", i, offset);
					}
				}

				windowWidth = lightmaps > 1 ? 550.f : 250.f;
				showLightmapEditorUpdate = false;
			}
			ImVec2 imgSize = ImVec2(200, 200);
			for (int i = 0; i < lightmaps; i++)
			{
				if (i == 0)
				{
					ImGui::Separator();
					ImGui::Dummy(ImVec2(50, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[1]);
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(120, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[2]);
				}

				if (i == 2)
				{
					ImGui::Separator();
					ImGui::Dummy(ImVec2(50, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[3]);
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(150, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[4]);
				}

				if (i == 1 || i > 2)
				{
					ImGui::SameLine();
				}
				else if (i == 2)
				{
					ImGui::Separator();
				}

				if (!currentlightMap[i])
				{
					ImGui::Dummy(ImVec2(200, 200));
					continue;
				}

				if (ImGui::ImageButton((void*)(uint64_t)currentlightMap[i]->id, imgSize, ImVec2(0, 0), ImVec2(1, 1), 0)) {
					ImVec2 picker_pos = ImGui::GetCursorScreenPos();
					if (i == 1 || i == 3)
					{
						picker_pos.x += 208;
					}
					ImVec2 mouse_pos_in_canvas = ImVec2(ImGui::GetIO().MousePos.x - picker_pos.x, 205 + ImGui::GetIO().MousePos.y - picker_pos.y);


					float image_x = currentlightMap[i]->width / 200.0f * (ImGui::GetIO().MousePos.x - picker_pos.x);
					float image_y = currentlightMap[i]->height / 200.0f * (205.f + ImGui::GetIO().MousePos.y - picker_pos.y);
					if (image_x < 0)
					{
						image_x = 0;
					}
					if (image_y < 0)
					{
						image_y = 0;
					}
					if (image_x > (float)currentlightMap[i]->width)
					{
						image_x = (float)currentlightMap[i]->width;
					}
					if (image_y > (float)currentlightMap[i]->height)
					{
						image_y = (float)currentlightMap[i]->height;
					}

					int offset = (int)((currentlightMap[i]->width * sizeof(COLOR3) * image_y) + (image_x * sizeof(COLOR3)));
					if (offset >= currentlightMap[i]->width * currentlightMap[i]->height * sizeof(COLOR3))
						offset = (currentlightMap[i]->width * currentlightMap[i]->height * sizeof(COLOR3)) - 1;
					if (offset < 0)
						offset = 0;

					currentlightMap[i]->data[offset + 0] = (unsigned char)(colourPatch[0] * 255.f);
					currentlightMap[i]->data[offset + 1] = (unsigned char)(colourPatch[1] * 255.f);
					currentlightMap[i]->data[offset + 2] = (unsigned char)(colourPatch[2] * 255.f);
					currentlightMap[i]->upload(GL_RGB, true);
					//logf("%f %f %f %f %d %d = %d \n", picker_pos.x, picker_pos.y, mouse_pos_in_canvas.x, mouse_pos_in_canvas.y, image_x, image_y, i);
				}
			}
			ImGui::Separator();
			ImGui::Text("Lightmap width:%d height:%d", size[0], size[1]);
			ImGui::Separator();
			ColorPicker3(colourPatch);
			ImGui::Separator();
			ImGui::SetNextItemWidth(100.f);
			ImGui::Combo(" Disable light", &type, light_names, IM_ARRAYSIZE(light_names));
			map->getBspRender()->showLightFlag = type - 1;
			ImGui::Separator();
			if (ImGui::Button("Save", ImVec2(120, 0)))
			{
				for (int i = 0; i < MAXLIGHTMAPS; i++) {
					if (face.nStyles[i] == 255 || !currentlightMap[i])
						continue;
					int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
					int offset = face.nLightmapOffset + i * lightmapSz;
					memcpy(map->lightdata + offset, currentlightMap[i]->data, lightmapSz);
				}
				map->getBspRender()->reloadLightmaps();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload", ImVec2(120, 0)))
			{
				showLightmapEditorUpdate = true;
			}

			ImGui::Separator();
			if (ImGui::Button("Export", ImVec2(120, 0)))
			{
				logf("Export lightmaps to png files...\n");
				ExportLightmaps(face, faceIdx, map);
			}
			ImGui::SameLine();
			if (ImGui::Button("Import", ImVec2(120, 0)))
			{
				logf("Import lightmaps from png files...\n");
				ImportLightmaps(face, faceIdx, map);
				showLightmapEditorUpdate = true;
				map->getBspRender()->reloadLightmaps();
			}
			ImGui::Separator();
			ImGui::Text("WARNING! SAVE MAP BEFORE NEXT ACTION!");
			ImGui::Separator();
			if (ImGui::Button("Export ALL", ImVec2(120, 0)))
			{
				logf("Export lightmaps to png files...\n");
				//for (int z = 0; z < map->faceCount; z++)
				//{
				//	lightmaps = 0;
				//	ExportLightmaps(map->faces[z], z, map);
				//}
				ExportOneBigLightmap(map);
			}
			ImGui::SameLine();
			if (ImGui::Button("Import ALL", ImVec2(120, 0)))
			{
				logf("Import lightmaps from png files...\n");
				//for (int z = 0; z < map->faceCount; z++)
				//{
				//	lightmaps = 0;
				//	ImportLightmaps(map->faces[z], z, map);
				//}

				ImportOneBigLightmapFile(map);
				map->getBspRender()->reloadLightmaps();
			}
		}
		else
		{
			ImGui::Text("No face selected");
		}

	}
	ImGui::End();
}
void Gui::drawTextureTool() {


	ImGui::SetNextWindowSize(ImVec2(300.f, 570.f), ImGuiCond_FirstUseEver);
	//ImGui::SetNextWindowSize(ImVec2(400, 600));
	if (ImGui::Begin("Face Editor", &showTextureWidget)) {
		static float scaleX, scaleY, shiftX, shiftY;
		static bool isSpecial;
		static float width, height;
		static ImTextureID textureId = NULL; // OpenGL ID
		static char textureName[16];
		static int lastPickCount = -1;
		static bool validTexture = true;

		Bsp* map = app->getSelectedMap();
		if (!map || app->pickMode != PICK_FACE || app->selectedFaces.size() == 0)
		{
			ImGui::Text("No face selected");
			ImGui::End();
			return;
		}
		BspRenderer* mapRenderer = map->getBspRender();
		if (!mapRenderer || !mapRenderer->texturesLoaded)
		{
			ImGui::Text("Loading textures...");
			ImGui::End();
			return;
		}
		if (lastPickCount != app->pickCount && app->pickMode == PICK_FACE) {
			if (app->selectedFaces.size()) {
				int faceIdx = app->selectedFaces[0];
				if (faceIdx >= 0)
				{
					BSPFACE& face = map->faces[faceIdx];
					BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
					int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];

					width = height = 0;
					if (texOffset != -1) {
						BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
						width = tex.nWidth * 1.0f;
						height = tex.nHeight * 1.0f;
						memcpy(textureName, tex.szName, MAXTEXTURENAME);
					}
					else {
						textureName[0] = '\0';
					}

					int miptex = texinfo.iMiptex;

					scaleX = 1.0f / texinfo.vS.length();
					scaleY = 1.0f / texinfo.vT.length();
					shiftX = texinfo.shiftS;
					shiftY = texinfo.shiftT;
					isSpecial = texinfo.nFlags & TEX_SPECIAL;

					textureId = (void*)(uint64_t)mapRenderer->getFaceTextureId(faceIdx);
					validTexture = true;

					// show default values if not all faces share the same values
					for (int i = 1; i < app->selectedFaces.size(); i++) {
						int faceIdx2 = app->selectedFaces[i];
						BSPFACE& face2 = map->faces[faceIdx2];
						BSPTEXTUREINFO& texinfo2 = map->texinfos[face2.iTextureInfo];

						if (scaleX != 1.0f / texinfo2.vS.length()) scaleX = 1.0f;
						if (scaleY != 1.0f / texinfo2.vT.length()) scaleY = 1.0f;
						if (shiftX != texinfo2.shiftS) shiftX = 0;
						if (shiftY != texinfo2.shiftT) shiftY = 0;
						if (isSpecial != (texinfo2.nFlags & TEX_SPECIAL)) isSpecial = false;
						if (texinfo2.iMiptex != miptex) {
							validTexture = false;
							textureId = NULL;
							width = 0.f;
							height = 0.f;
							textureName[0] = '\0';
						}
					}
				}
			}
			else {
				scaleX = scaleY = shiftX = shiftY = width = height = 0.f;
				textureId = NULL;
				textureName[0] = '\0';
			}

			checkFaceErrors();
		}
		lastPickCount = app->pickCount;

		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.5f;

		static bool scaledX = false;
		static bool scaledY = false;
		static bool shiftedX = false;
		static bool shiftedY = false;
		static bool textureChanged = false;
		static bool toggledFlags = false;

		ImGui::PushItemWidth(inputWidth);
		ImGui::Text("Scale");

		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Almost always breaks lightmaps if changed.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat("##scalex", &scaleX, 0.001f, 0, 0, "X: %.3f") && scaleX != 0) {
			scaledX = true;
		}
		ImGui::SameLine();
		if (ImGui::DragFloat("##scaley", &scaleY, 0.001f, 0, 0, "Y: %.3f") && scaleY != 0) {
			scaledY = true;
		}

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text("Shift");

		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Sometimes breaks lightmaps if changed.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat("##shiftx", &shiftX, 0.1f, 0, 0, "X: %.3f")) {
			shiftedX = true;
		}
		ImGui::SameLine();
		if (ImGui::DragFloat("##shifty", &shiftY, 0.1f, 0, 0, "Y: %.3f")) {
			shiftedY = true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Flags");
		if (ImGui::Checkbox("Special", &isSpecial)) {
			toggledFlags = true;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Used with invisible faces to bypass the surface extent limit."
				"\nLightmaps may break in strange ways if this is used on a normal face.");
			ImGui::EndTooltip();
		}

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text("Texture");
		ImGui::SetNextItemWidth(inputWidth);
		if (!validTexture) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		}
		if (ImGui::InputText("##texname", textureName, MAXTEXTURENAME)) {
			textureChanged = true;
		}
		if (refreshSelectedFaces) {
			textureChanged = true;
			refreshSelectedFaces = false;
			int texOffset = ((int*)map->textures)[copiedMiptex + 1];
			BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
			memcpy(textureName, tex.szName, MAXTEXTURENAME);
			textureName[15] = '\0';
		}
		if (!validTexture) {
			ImGui::PopStyleColor();
		}
		ImGui::SameLine();
		ImGui::Text("%.0fx%.0f", width, height);

		if (!ImGui::IsMouseDown(ImGuiMouseButton_::ImGuiMouseButton_Left) && (scaledX || scaledY || shiftedX || shiftedY || textureChanged || refreshSelectedFaces || toggledFlags)) {
			unsigned int newMiptex = 0;

			app->saveLumpState(map, 0xffffffff, false);
			if (textureChanged) {
				validTexture = false;

				unsigned int totalTextures = ((unsigned int*)map->textures)[0];

				for (unsigned int i = 0; i < totalTextures; i++) {
					int texOffset = ((int*)map->textures)[i + 1];
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					if (strcmp(tex.szName, textureName) == 0) {
						validTexture = true;
						newMiptex = i;
						break;
					}
				}

				if (!validTexture)
				{
					for (auto& s : mapRenderer->wads)
					{
						if (s->hasTexture(textureName))
						{

							WADTEX* wadTex = s->readTexture(textureName);
							int lastMipSize = (wadTex->nWidth / 8) * (wadTex->nHeight / 8);

							COLOR3* palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + 2 - 40);
							unsigned char* src = wadTex->data;

							COLOR3* imageData = new COLOR3[wadTex->nWidth * wadTex->nHeight];

							int sz = wadTex->nWidth * wadTex->nHeight;

							for (int k = 0; k < sz; k++) {
								imageData[k] = palette[src[k]];
							}
							map->add_texture(textureName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);
							BspRenderer* mapRenderer = map->getBspRender();
							mapRenderer->ReuploadTextures();
							delete[] imageData;
							delete wadTex;
							ImGui::End();
							return;
						}
					}
				}
			}
			std::set<int> modelRefreshes;
			for (int i = 0; i < app->selectedFaces.size(); i++) {
				int faceIdx = app->selectedFaces[i];
				BSPTEXTUREINFO* texinfo = map->get_unique_texinfo(faceIdx);

				if (scaledX) {
					texinfo->vS = texinfo->vS.normalize(1.0f / scaleX);
				}
				if (scaledY) {
					texinfo->vT = texinfo->vT.normalize(1.0f / scaleY);
				}
				if (shiftedX) {
					texinfo->shiftS = shiftX;
				}
				if (shiftedY) {
					texinfo->shiftT = shiftY;
				}
				if (toggledFlags) {
					texinfo->nFlags = isSpecial ? TEX_SPECIAL : 0;
				}
				if ((textureChanged || toggledFlags) && validTexture) {
					if (textureChanged)
						texinfo->iMiptex = newMiptex;
					modelRefreshes.insert(map->get_model_from_face(faceIdx));
				}

				mapRenderer->updateFaceUVs(faceIdx);
			}
			if ((textureChanged || toggledFlags) && app->selectedFaces.size() && app->selectedFaces[0] >= 0) {
				textureId = (void*)(uint64_t)mapRenderer->getFaceTextureId(app->selectedFaces[0]);
				for (auto it = modelRefreshes.begin(); it != modelRefreshes.end(); it++) {
					mapRenderer->refreshModel(*it);
				}
				for (int i = 0; i < app->selectedFaces.size(); i++) {
					mapRenderer->highlightFace(app->selectedFaces[i], true);
				}
			}
			checkFaceErrors();
			scaledX = false;
			scaledY = false;
			shiftedX = false;
			shiftedY = false;
			textureChanged = false;
			toggledFlags = false;

			app->pushModelUndoState("Edit Face", EDIT_MODEL_LUMPS);

			mapRenderer->updateLightmapInfos();
			mapRenderer->calcFaceMaths();
			app->updateModelVerts();

			reloadLimits();
		}

		refreshSelectedFaces = false;

		ImVec2 imgSize = ImVec2(inputWidth * 2 - 2, inputWidth * 2 - 2);
		if (ImGui::ImageButton(textureId, imgSize, ImVec2(0, 0), ImVec2(1, 1), 1)) {
			logf("Open browser!\n");

			ImGui::OpenPopup("Not Implemented");
		}

		if (ImGui::BeginPopupModal("Not Implemented", NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{

			ImGui::Text("TODO: Texture browser\n\n");
			ImGui::Separator();

			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::EndPopup();
		}
	}

	ImGui::End();
}

StatInfo Gui::calcStat(std::string name, unsigned int val, unsigned int max, bool isMem) {
	StatInfo stat;
	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	ImVec4 color;

	if (val > max) {
		color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	}
	else if (percent >= 90) {
		color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
	}
	else if (percent >= 75) {
		color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	}
	else {
		color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	static char tmp[256];

	//std::string out;

	stat.name = std::move(name);

	if (isMem) {
		snprintf(tmp, sizeof(tmp), "%8.2f", val / meg);
		stat.val = std::string(tmp);

		snprintf(tmp, sizeof(tmp), "%-5.2f MB", max / meg);
		stat.max = std::string(tmp);
	}
	else {
		snprintf(tmp, sizeof(tmp), "%8u", val);
		stat.val = std::string(tmp);

		snprintf(tmp, sizeof(tmp), "%-8u", max);
		stat.max = std::string(tmp);
	}
	snprintf(tmp, sizeof(tmp), "%3.1f%%", percent);
	stat.fullness = std::string(tmp);
	stat.color = color;

	stat.progress = (float)val / (float)max;

	return stat;
}

ModelInfo Gui::calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, unsigned int val, unsigned int max, bool isMem) {
	ModelInfo stat;

	std::string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	std::string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (int k = 0; k < map->ents.size(); k++) {
		if (map->ents[k]->getBspModelIdx() == modelInfo->modelIdx) {
			targetname = map->ents[k]->keyvalues["targetname"];
			classname = map->ents[k]->keyvalues["classname"];
			stat.entIdx = k;
		}
	}

	stat.classname = std::move(classname);
	stat.targetname = std::move(targetname);

	static char tmp[256];

	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	if (isMem) {
		snprintf(tmp, sizeof(tmp), "%8.1f", val / meg);
		stat.val = std::to_string(val);

		snprintf(tmp, sizeof(tmp), "%-5.1f MB", max / meg);
		stat.usage = tmp;
	}
	else {
		stat.model = "*" + std::to_string(modelInfo->modelIdx);
		stat.val = std::to_string(val);
	}
	if (percent >= 0.1f) {
		snprintf(tmp, sizeof(tmp), "%6.1f%%%%", percent);
		stat.usage = std::string(tmp);
	}

	return stat;
}

void Gui::reloadLimits() {
	for (int i = 0; i < SORT_MODES; i++) {
		loadedLimit[i] = false;
	}
	loadedStats = false;
}

void Gui::checkValidHulls() {
	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		anyHullValid[i] = false;
		for (int k = 0; k < app->mapRenderers.size() && !anyHullValid[i]; k++) {
			Bsp* map = app->mapRenderers[k]->map;

			for (unsigned int m = 0; m < map->modelCount; m++) {
				if (map->models[m].iHeadnodes[i] >= 0) {
					anyHullValid[i] = true;
					break;
				}
			}
		}
	}
}

void Gui::checkFaceErrors() {
	lightmapTooLarge = badSurfaceExtents = false;

	Bsp* map = app->getSelectedMap();
	if (!map)
		return;


	for (int i = 0; i < app->selectedFaces.size(); i++) {
		int size[2];
		if (!GetFaceLightmapSize(map, app->selectedFaces[i], size)) {
			badSurfaceExtents = true;
		}
		if (size[0] * size[1] > MAX_LUXELS) {
			lightmapTooLarge = true;
		}
	}
}

void Gui::refresh() {
	reloadLimits();
	checkValidHulls();
}