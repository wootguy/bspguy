#include "Widget.h"
#include "LeafNavMesh.h"
#include "Entity.h"
#include "render_utils.h"
#include "NavRenderer.h"

void DebugWidget::setup() {
	ImGui::SetNextWindowBgAlpha(0.75f);
}

void DebugWidget::drawSelectionDetails() {
	int modelIndex = app->pickInfo.getModelIndex();
	if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Entity ID: %d", app->pickInfo.getEntIndex());

		if (modelIndex > 0) {
			BSPMODEL& model = map->models[modelIndex];
			ImGui::Text("Model ID: %d", modelIndex);
			ImGui::Text("Model polys: %d", model.nFaces);
			ImGui::Text("Model leaves: %d", model.nVisLeafs);
			ImGui::Text("Model face offset: %d", model.iFirstFace);
			ImGui::Text("Model mins: %.2f %.2f %.2f", model.nMins.x, model.nMins.y, model.nMins.z);
			ImGui::Text("Model maxs: %.2f %.2f %.2f", model.nMaxs.x, model.nMaxs.y, model.nMaxs.z);
			ImGui::Text("Model origin: %.2f %.2f %.2f", model.vOrigin.x, model.vOrigin.y, model.vOrigin.z);

			ImGui::Checkbox("Debug clipnodes", &app->debugClipnodes);
			ImGui::SliderInt("Clipnode", &app->debugInt, 0, app->debugIntMax);

			ImGui::Checkbox("Debug nodes", &app->debugNodes);
			ImGui::SliderInt("Node", &app->debugNode, 0, app->debugNodeMax);
		}

		if (app->pickInfo.leaves.size()) {
			if (app->pickInfo.getLeafIndex() != -1) {
				BSPLEAF& leaf = map->leaves[app->pickInfo.getLeafIndex()];
				ImGui::Text("Leaf ID: %d", app->pickInfo.getLeafIndex());
				ImGui::Text("Leaf contents: %s (%d)", map->getLeafContentsName(leaf.nContents), leaf.nContents);
				ImGui::Text("Leaf faces: %d", leaf.nMarkSurfaces);
				ImGui::Text("Leaf first surf: %d", leaf.iFirstMarkSurface);
				ImGui::Text("Leaf VIS offset: %d", leaf.nVisOffset);
				ImGui::Text("Leaf ambient levels: %d %d %d %d", leaf.nAmbientLevels[0], leaf.nAmbientLevels[1], leaf.nAmbientLevels[2], leaf.nAmbientLevels[3]);
				ImGui::Text("Leaf mins: %d %d %d", (int)leaf.nMins[0], (int)leaf.nMins[1], (int)leaf.nMins[2]);
				ImGui::Text("Leaf maxs: %d %d %d", (int)leaf.nMaxs[0], (int)leaf.nMaxs[1], (int)leaf.nMaxs[2]);
			}
			else {
				unordered_set<int> uniqueFaces;
				for (int idx : app->pickInfo.leaves) {
					BSPLEAF& leaf = map->leaves[idx];
					for (int i = 0; i < leaf.nMarkSurfaces; i++) {
						uniqueFaces.insert(map->marksurfs[leaf.iFirstMarkSurface + i]);
					}
				}
				ImGui::Text("Leaf faces: %d", uniqueFaces.size());
			}
		}
		else if (app->pickInfo.getFaceIndex() != -1) {
			BSPMODEL& model = map->models[modelIndex];
			BSPFACE& face = *app->pickInfo.getFace();
			BSPPLANE& plane = map->planes[face.iPlane];

			ImGui::Text("Model ID: %d", modelIndex);
			ImGui::Text("Model polies: %d", model.nFaces);

			ImGui::Text("Face ID: %d", app->pickInfo.getFaceIndex());

			vec3 faceNormal = plane.vNormal * (face.nPlaneSide ? -1 : 1);

			if (face.iTextureInfo < map->texinfoCount) {
				BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
				ImGui::Text("Face S Axis: %.2f %.2f %.2f", info.vS.x, info.vS.y, info.vS.z);
				ImGui::Text("Face T Axis: %.2f %.2f %.2f", info.vT.x, info.vT.y, info.vT.z);
			}
			ImGui::Text("Face Normal: %.2f %.2f %.2f", faceNormal.x, faceNormal.y, faceNormal.z);

			ImGui::Text("Plane ID: %d", face.iPlane);

			if (face.iTextureInfo < map->texinfoCount) {
				BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
				BSPMIPTEX* tex = map->get_texture(info.iMiptex);
				ImGui::Text("Texinfo ID: %d", face.iTextureInfo);
				ImGui::Text("Texture ID: %d", info.iMiptex);
				if (tex) {
					ImGui::Text("Texture: %s (%dx%d)", tex->szName, tex->nWidth, tex->nHeight);
				}
				else {
					ImGui::Text("Texture: INVALID OFFSET");
				}
			}
			ImGui::Text("Lightmap Offset: %d", face.nLightmapOffset);
			ImGui::Text("Light Styles: [%d, %d, %d, %d]", face.nStyles[0], face.nStyles[1], face.nStyles[2], face.nStyles[3]);

			static int lastFaceIdx = -1;
			static string leafList;
			static int leafPick = 0;

			if (app->pickInfo.getFaceIndex() != lastFaceIdx) {
				lastFaceIdx = app->pickInfo.getFaceIndex();
				leafList = "";
				leafPick = -1;
				for (int i = 1; i < map->leafCount; i++) {
					BSPLEAF& leaf = map->leaves[i];
					for (int k = 0; k < leaf.nMarkSurfaces; k++) {
						if (map->marksurfs[leaf.iFirstMarkSurface + k] == app->pickInfo.getFaceIndex()) {
							leafList += " " + to_string(i);
							leafPick = i;
						}
					}
				}
			}

			const char* isVis = map->is_leaf_visible(leafPick, app->cameraOrigin) ? " (visible!)" : "";

			ImGui::Text("Leaf IDs:%s%s", leafList.c_str(), isVis);
		}
	}

	if (app->pickInfo.getFace()) {
		BSPFACE& face = *app->pickInfo.getFace();
		if (ImGui::CollapsingHeader(("Face Edges: " + to_string(face.nEdges)).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (int i = 0; i < face.nEdges; i++) {
				int32_t edgeIdx = map->surfedges[face.iFirstEdge + i];
				BSPEDGE& edge = map->edges[abs(edgeIdx)];
				ImGui::Text("Edge %d = [%d, %d] ", edgeIdx, edge.iVertex[0], edge.iVertex[1]);

				drawBox(map->verts[edge.iVertex[0]], 8, COLOR4(0, 128, 0, 255));
			}
		}
	}

	string bspTreeTitle = "BSP Tree";
	if (modelIndex >= 0) {
		bspTreeTitle += " (Model " + to_string(modelIndex) + ")";
	}
	if (ImGui::CollapsingHeader((bspTreeTitle + "##bsptree").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

		if (app->pickInfo.getMap() && modelIndex >= 0 && modelIndex < app->pickInfo.getMap()->modelCount) {
			Bsp* map = app->pickInfo.getMap();

			vec3 localCamera = app->cameraOrigin - app->mapRenderer->mapOffset;
			if (app->pickInfo.getEnt()) {
				localCamera -= app->pickInfo.getEnt()->getOrigin();
			}

			static ImVec4 hullColors[] = {
				ImVec4(1, 1, 1, 1),
				ImVec4(0.3, 1, 1, 1),
				ImVec4(1, 0.3, 1, 1),
				ImVec4(1, 1, 0.3, 1),
			};

			for (int i = 0; i < MAX_MAP_HULLS; i++) {
				vector<int> nodeBranch;
				int leafIdx;
				int childIdx = -1;
				int headNode = map->models[modelIndex].iHeadnodes[i];
				int contents = map->pointContents(headNode, localCamera, i, nodeBranch, leafIdx, childIdx);

				ImGui::PushStyleColor(ImGuiCol_Text, hullColors[i]);
				if (ImGui::TreeNode(("HULL " + to_string(i)).c_str()))
				{
					ImGui::Indent();
					ImGui::Text("Contents: %s", map->getLeafContentsName(contents));
					if (i == 0) {
						ImGui::Text("Leaf: %d", leafIdx);
					}
					else if (app->navRenderer->debugLeafNavMesh && i == app->navRenderer->debugLeafNavMesh->hull) {
						int leafNavIdx = app->navRenderer->debugLeafNavMesh->getNodeIdx(map, localCamera);

						ImGui::Text("Nav ID: %d", leafNavIdx);
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
		else {
			ImGui::Text("No model selected");
		}
	}
}

void DebugWidget::draw() {
	if (app->pickInfo.getMap()) {
		drawSelectionDetails();
	}
	else {
		ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen);
		ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen);
	}

	if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::TreeNodeEx("Render Stats", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();
			
			ImGui::Text("Objects: %d", g_renderStats.numObjects);
			ImGui::Text("Vertices: %d k", g_renderStats.numVerts / 1000);
			ImGui::Text("Matrix uploads: %d", g_renderStats.numMatrixUploads);
			ImGui::Text("Uniform uploads: %d", g_renderStats.numUniformsUploaded);
			ImGui::Text("Texture binds: %d", g_renderStats.numTextureBinds);
			ImGui::Text("Shader binds: %d", g_renderStats.numShaderBinds);
			g_renderStats.clear();

			ImGui::Unindent();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("VRAM", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			ImGui::Text("Vertices: %.2f MB", g_renderStats.vertMem / (1024.0f * 1024.0f));
			ImGui::Text("Textures: %.2f MB", g_renderStats.texMem / (1024.0f * 1024.0f));

			ImGui::Unindent();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("RAM (FPS killer)", 0))
		{
			ImGui::Indent();

			int entData = 0;
			for (Entity* ent : map->ents) {
				entData += ent->getMemoryUsage();
			}
			for (auto item : g_stringMap) {
				entData += item.first.size() * 2 + sizeof(string_t);
			}

			int fgdBytes = app->mergedFgd ? app->mergedFgd->calcMemoryUsage() : 0;
			for (Fgd* fgd : app->fgds) {
				fgdBytes += fgd->calcMemoryUsage();
			}

			int bspMem = app->mapRenderer->map->calcMemoryUsage();
			int bspRenderMem = app->mapRenderer->calcMemoryUsage();
			int guiMem = app->gui->calcMemUsage();

			int total = fgdBytes + bspMem + bspRenderMem + entData + app->undoMemoryUsage + guiMem;

			ImGui::Text("BSP: %.2f MB", bspMem / (1024.0f * 1024.0f));
			ImGui::Text("BSP Renderer: %.2f MB", bspRenderMem / (1024.0f * 1024.0f));
			ImGui::Text("FGDs: %.2f MB", fgdBytes / (1024.0f * 1024.0f));
			ImGui::Text("Entities: %.2f MB\n", entData / (1024.0f * 1024.0f));
#ifdef DEBUG_MODE
			ImGui::Text("GUI: %.2f MB\n", guiMem / (1024.0f * 1024.0f));
#endif
			ImGui::Text("Undo History: %.2f MB\n", app->undoMemoryUsage / (1024.0f * 1024.0f));
			ImGui::Text("TOTAL: %.2f MB\n", total / (1024.0f * 1024.0f));

			ImGui::Unindent();
			ImGui::TreePop();
		}
	}

#ifdef DEBUG_MODE
	if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("DebugVec0 %6.2f %6.2f %6.2f", app->debugVec0.x, app->debugVec0.y, app->debugVec0.z);
		ImGui::Text("DebugVec1 %6.2f %6.2f %6.2f", app->debugVec1.x, app->debugVec1.y, app->debugVec1.z);
		ImGui::Text("DebugVec2 %6.2f %6.2f %6.2f", app->debugVec2.x, app->debugVec2.y, app->debugVec2.z);
		ImGui::Text("DebugVec3 %6.2f %6.2f %6.2f", app->debugVec3.x, app->debugVec3.y, app->debugVec3.z);
	}
#endif
}
