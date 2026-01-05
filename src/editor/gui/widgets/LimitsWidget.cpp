#include "LimitsWidget.h"
#include "Entity.h"

void LimitsWidget::draw() {
	if (map == NULL) {
		ImGui::Text("No map selected");
		return;
	}

	if (ImGui::BeginTabBar("##tabs"))
	{
		if (ImGui::BeginTabItem("All Datatypes")) {
			drawLimitsSummary(map, false);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Clipnodes")) {
			gui->loadedStats = false;
			drawLimitTab(map, SORT_CLIPNODES);
			ImGui::EndTabItem();
		}

		/*
		if (ImGui::BeginTabItem("Nodes")) {
			loadedStats = false;
			drawLimitTab(map, SORT_NODES);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Faces")) {
			loadedStats = false;
			drawLimitTab(map, SORT_FACES);
			ImGui::EndTabItem();
		}
		*/

		if (ImGui::BeginTabItem("Vertices")) {
			gui->loadedStats = false;
			drawLimitTab(map, SORT_VERTS);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("AllocBlock")) {
			gui->loadedStats = false;
			drawAllocBlockLimitTab(map);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Extents")) {
			gui->loadedStats = false;
			drawFaceExtentsLimitTab();
			ImGui::EndTabItem();
		}
	}

	ImGui::EndTabBar();
}

void LimitsWidget::drawLimitsSummary(Bsp* map, bool modalMode) {
	if (!gui->loadedStats) {
		int worldleafCount = map->modelCount > 0 ? map->models[0].nVisLeafs : 0;

		stats.clear();
		stats.push_back(calcStat("AllocBlock", map->calc_allocblock_usage(), g_limits.max_allocblocks, false));
		stats.push_back(calcStat("clipnodes", map->clipnodeCount, g_limits.max_clipnodes, false));
		stats.push_back(calcStat("nodes", map->nodeCount, g_limits.max_nodes, false));
		stats.push_back(calcStat("leaves", map->leafCount, g_limits.max_leaves, false));
		stats.push_back(calcStat("worldleaves", worldleafCount, g_limits.max_worldleaves, false));
		stats.push_back(calcStat("models", map->modelCount, g_limits.max_models, false));
		stats.push_back(calcStat("faces", map->faceCount, g_limits.max_faces, false));
		stats.push_back(calcStat("texinfos", map->texinfoCount, g_limits.max_texinfos, false));
		stats.push_back(calcStat("textures", map->textureCount, g_limits.max_textures, false));
		stats.push_back(calcStat("planes", map->planeCount, g_limits.max_planes, false));
		stats.push_back(calcStat("vertexes", map->vertCount, g_limits.max_vertexes, false));
		stats.push_back(calcStat("edges", map->edgeCount, g_limits.max_edges, false));
		stats.push_back(calcStat("surfedges", map->surfedgeCount, g_limits.max_surfedges, false));
		stats.push_back(calcStat("marksurfaces", map->marksurfCount, g_limits.max_marksurfaces, false));
		stats.push_back(calcStat("lightstyles", map->lightstyle_count(), g_limits.max_lightstyles, false));
		stats.push_back(calcStat("lightdata", map->lightDataLength, g_limits.max_lightdata, true));
		stats.push_back(calcStat("entdata", map->header.lump[LUMP_ENTITIES].nLength, g_limits.max_entdata, true));
		stats.push_back(calcStat("visdata", map->visDataLength, g_limits.max_visdata, true));
		gui->loadedStats = true;
	}

	if (!modalMode)
		ImGui::BeginChild("##content");
	ImGui::Dummy(ImVec2(0, 10 * uiScale));
	ImGui::PushFont(gui->consoleFont, g_font_scale_base);

	int fontSize = g_font_scale_base;
	int midWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "    Current / Max    ").x;
	int nameWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "marksurfaces").x;

	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(roundf(2.0f * uiScale), 0));
	if (ImGui::BeginTable("StatsTable", 3, ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("Data Type", ImGuiTableColumnFlags_WidthFixed, nameWidth * uiScale);
		ImGui::TableSetupColumn(" Current / Max", ImGuiTableColumnFlags_WidthFixed, midWidth * uiScale);
		ImGui::TableSetupColumn("Fullness", ImGuiTableColumnFlags_WidthStretch);

		ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		ImGui::TableHeadersRow();
		ImGui::PopStyleColor(3);

		// manually create the bottom border
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(roundf(2.0f * uiScale), 0.0f));
		ImGui::TableNextRow(ImGuiTableRowFlags_None, 1.0f);
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Border));
		ImGui::PopStyleVar();

		for (int i = 0; i < stats.size(); i++) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(stats[i].color, stats[i].name.c_str());

			ImGui::TableNextColumn();
			string val = stats[i].val + " / " + stats[i].max;
			ImGui::TextColored(stats[i].color, val.c_str());

			ImGui::TableNextColumn();
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5f, 0.4f, 0, 1));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + roundf(2 * uiScale));
			ImGui::PushFont(gui->consoleFont, g_font_scale_base * 0.85f);
			ImGui::ProgressBar(stats[i].progress, ImVec2(-1, 0), stats[i].fullness.c_str());
			ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(1);
			ImGui::TableNextColumn();
		}

		ImGui::EndTable();
	}
	ImGui::PopStyleVar();

	ImGui::PopFont();
	if (!modalMode)
		ImGui::EndChild();
}

void LimitsWidget::drawLimitTab(Bsp* map, int sortMode) {

	int maxCount;
	const char* countName;
	switch (sortMode) {
	case SORT_VERTS:		maxCount = map->vertCount; countName = "Vertexes";  break;
	case SORT_NODES:		maxCount = map->nodeCount; countName = "Nodes";  break;
	case SORT_CLIPNODES:	maxCount = map->clipnodeCount; countName = "Clipnodes";  break;
	case SORT_FACES:		maxCount = map->faceCount; countName = "Faces";  break;
	}

	if (!gui->loadedLimit[sortMode]) {
		vector<STRUCTUSAGE*> modelInfos = map->get_sorted_model_infos(sortMode);

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
		gui->loadedLimit[sortMode] = true;
	}
	vector<ModelInfo>& modelInfos = limitModels[sortMode];

	ImGui::BeginChild("content");
	ImGui::Dummy(ImVec2(0, 10 * uiScale));
	ImGui::PushFont(gui->consoleFont, g_font_scale_base);

	int fontSize = g_font_scale_base;
	int valWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Clipnodes ").x;
	int usageWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "  Usage   ").x;
	int modelWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Model ").x;
	int bigWidth = ImGui::GetWindowWidth() - (valWidth + usageWidth + modelWidth) * uiScale;
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth * uiScale);
	ImGui::SetColumnWidth(2, valWidth * uiScale);
	ImGui::SetColumnWidth(3, usageWidth * uiScale);

	ImGui::Text("Classname"); ImGui::NextColumn();
	ImGui::Text("Model"); ImGui::NextColumn();
	ImGui::Text(countName); ImGui::NextColumn();
	ImGui::Text("Usage"); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild("chart");
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth * uiScale);
	ImGui::SetColumnWidth(2, valWidth * uiScale);
	ImGui::SetColumnWidth(3, usageWidth * uiScale);

	int selected = app->pickInfo.getEntIndex();

	for (int i = 0; i < limitModels[sortMode].size(); i++) {

		if (modelInfos[i].val == "0") {
			break;
		}

		string cname = modelInfos[i].classname + "##" + "select" + to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(cname.c_str(), selected == modelInfos[i].entIdx, flags)) {
			selected = i;
			int entIdx = modelInfos[i].entIdx;
			if (entIdx < map->ents.size()) {
				Entity* ent = map->ents[entIdx];
				app->pickInfo.deselect();
				app->pickInfo.selectEnt(entIdx);
				app->postSelectEnt();
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

bool sortAllocInfos(const AllocInfo& a, const AllocInfo& b) {
	return a.sort > b.sort;
}

void LimitsWidget::drawAllocBlockLimitTab(Bsp* map) {

	int maxCount;
	const int allocBlockSize = 128 * 128;

	if (!gui->loadedLimit[SORT_ALLOCBLOCK]) {
		limitAllocs.clear();

		struct AllocInfoInt {
			int faceCount = 0;
			int val = 0;
			int faceIdx = -1;
		};

		unordered_map<string, AllocInfoInt> infos;

		for (int i = 0; i < map->faceCount; i++) {
			BSPFACE& f = map->faces[i];
			BSPTEXTUREINFO& tinfo = map->texinfos[f.iTextureInfo];
			if (tinfo.nFlags & TEX_SPECIAL)
				continue; // does not use lightmaps

			int size[2];
			GetFaceLightmapSize(map, i, size);

			BSPMIPTEX* tex = map->get_texture(tinfo.iMiptex);

			string texname = tex ? tex->szName : "INVALID OFFSET";
			AllocInfoInt& info = infos[texname];
			info.faceCount++;
			info.val += size[0] * size[1];
			info.faceIdx = i;
		}

		static char tmp[256];

		for (auto item : infos) {
			AllocInfo info;
			info.texname = item.first;
			info.faceCount = to_string(item.second.faceCount);

			sprintf(tmp, "%.1f", item.second.val / (float)allocBlockSize);
			info.val = tmp;

			sprintf(tmp, "%.1f", (item.second.val / (float)(64 * allocBlockSize)) * 100);
			info.usage = std::string(tmp) + "%%";

			info.sort = item.second.val;
			info.faceIdx = item.second.faceIdx;

			limitAllocs.push_back(info);
		}

		sort(limitAllocs.begin(), limitAllocs.end(), sortAllocInfos);

		gui->loadedLimit[SORT_ALLOCBLOCK] = true;
	}
	vector<AllocInfo>& allocInfos = limitAllocs;

	ImGui::BeginChild("content");
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::PushFont(gui->consoleFont, g_font_scale_base);

	int fontSize = g_font_scale_base;
	int valWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Clipnodes ").x;
	int usageWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "  Usage   ").x;
	int modelWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Model ").x;
	int bigWidth = ImGui::GetWindowWidth() - (valWidth + usageWidth + modelWidth);
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	ImGui::Text("Texture"); ImGui::NextColumn();
	ImGui::Text("Faces"); ImGui::NextColumn();
	ImGui::Text("Blocks"); ImGui::NextColumn();
	ImGui::Text("Usage"); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild("chart");
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	int selected = app->pickInfo.getFaceIndex();

	for (int i = 0; i < limitAllocs.size(); i++) {

		if (limitAllocs[i].val == "0.0") {
			break;
		}

		string texname = limitAllocs[i].texname + "##" + "select" + to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(texname.c_str(), false, flags) && !g_settings.ripent_safe_mode) {
			selected = i;

			int faceIdx = limitAllocs[i].faceIdx;
			int modelIdx = 0;
			for (int i = 0; i < map->modelCount; i++) {
				BSPMODEL& model = map->models[i];
				if (model.iFirstFace <= faceIdx && model.iFirstFace + model.nFaces > faceIdx) {
					modelIdx = i;
					break;
				}
			}

			g_app->mapRenderer->highlightPickedFaces(false);
			app->deselectFaces();
			app->pickInfo.selectFace(faceIdx);
			g_app->mapRenderer->highlightPickedFaces(true);
			app->updateTextureAxes();
			app->pickMode = PICK_FACE;
			gui->widgets[WIDGET_FACE_EDITOR]->widgetVisible = true;
			app->pickCount++;
			app->goToFace(map, faceIdx);
		}
		ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitAllocs[i].faceCount.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitAllocs[i].faceCount.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitAllocs[i].val.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitAllocs[i].val.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitAllocs[i].usage.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitAllocs[i].usage.c_str()); ImGui::NextColumn();
	}


	ImGui::Columns(1);
	ImGui::EndChild();

	ImGui::PopFont();
	ImGui::EndChild();
}

void LimitsWidget::drawFaceExtentsLimitTab() {
	if (app->isLoading)
		return; // counting subdivisions messes with lumps so don't do that while loading

	Bsp* map = app->mapRenderer->map;

	int maxCount;
	const int allocBlockSize = 128 * 128;

	if (!gui->loadedLimit[SORT_EXTENTS]) {
		limitExtents.clear();

		unordered_set<int> bad_extent_mips;
		for (int fa = 0; fa < map->faceCount; fa++) {
			BSPFACE& face = map->faces[fa];
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];

			if (info.nFlags & TEX_SPECIAL)
				continue;

			int size[2];
			if (GetFaceLightmapSize(map, fa, size)) {
				continue;
			}

			bad_extent_mips.insert(info.iMiptex);
		}

		for (int mip : bad_extent_mips) {
			BSPMIPTEX* tex = map->get_texture(mip);
			if (!tex) {
				continue;
			}

			ExtentInfo info;
			info.texname = tex->szName;
			info.faceCount = to_string(map->count_faces_for_mip(mip));
			info.dimensions = to_string(tex->nWidth) + "x" + to_string(tex->nHeight);
			info.sort = map->get_subdivisions_needed_to_fix_mip_extents(mip);
			info.subsNeeded = to_string(info.sort);
			info.mip = mip;
			limitExtents.push_back(info);
		}
		sort(limitExtents.begin(), limitExtents.end(), [](const ExtentInfo& a, const ExtentInfo& b) {
			return a.sort > b.sort;
			});

		gui->loadedLimit[SORT_EXTENTS] = true;
	}
	vector<ExtentInfo>& allocInfos = limitExtents;

	ImGui::BeginChild("content");
	ImGui::Dummy(ImVec2(0, 10 * uiScale));
	ImGui::PushFont(gui->consoleFont, g_font_scale_base);

	int fontSize = g_font_scale_base;
	int facesWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Faces ").x;
	int subsWidth = gui->consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Subs Needed ").x;
	int bigWidth = ImGui::GetWindowWidth() - (facesWidth + subsWidth) * uiScale;
	ImGui::Columns(3);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, facesWidth * uiScale);
	ImGui::SetColumnWidth(2, subsWidth * uiScale);

	ImGui::Text("Texture"); ImGui::NextColumn();
	ImGui::Text("Faces"); ImGui::NextColumn();
	ImGui::Text("Subs Needed"); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild("chart");
	ImGui::Columns(3);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, facesWidth * uiScale);
	ImGui::SetColumnWidth(2, subsWidth * uiScale);

	int selected = app->pickInfo.getFaceIndex();

	for (int i = 0; i < limitExtents.size(); i++) {
		string texname = limitExtents[i].texname + "##" + "select" + to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(texname.c_str(), false, flags) && !g_settings.ripent_safe_mode) {
			selected = i;

			app->deselectFaces();

			for (int k = 0; k < map->faceCount; k++) {
				BSPFACE& face = map->faces[k];
				BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];

				if (info.iMiptex == limitExtents[i].mip) {
					app->pickInfo.selectFace(k);
				}
			}
			app->mapRenderer->highlightPickedFaces(true);

			app->updateTextureAxes();
			app->pickMode = PICK_FACE;
			gui->widgets[WIDGET_FACE_EDITOR]->widgetVisible = true;
			app->pickCount++;
		}

		ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitExtents[i].faceCount.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitExtents[i].faceCount.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitExtents[i].subsNeeded.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitExtents[i].subsNeeded.c_str()); ImGui::NextColumn();
	}


	ImGui::Columns(1);
	ImGui::EndChild();

	ImGui::PopFont();
	ImGui::EndChild();
}

StatInfo LimitsWidget::calcStat(string name, uint val, uint max, bool isMem) {
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
		color = style.Colors[ImGuiCol_Text];
	}

	static char tmp[256];

	string out;

	stat.name = name;

	if (isMem) {
		sprintf(tmp, "%8.2f", val / meg);
		stat.val = string(tmp);

		sprintf(tmp, "%-5.2f MB", max / meg);
		stat.max = string(tmp);
	}
	else {
		sprintf(tmp, "%8u", val);
		stat.val = string(tmp);

		sprintf(tmp, "%-8u", max);
		stat.max = string(tmp);
	}
	sprintf(tmp, "%3.1f%%", percent);
	stat.fullness = string(tmp);
	stat.color = color;

	stat.progress = (float)val / (float)max;

	return stat;
}

ModelInfo LimitsWidget::calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, uint val, uint max, bool isMem) {
	ModelInfo stat;

	string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (int k = 0; k < map->ents.size(); k++) {
		if (map->ents[k]->getBspModelIdx() == modelInfo->modelIdx) {
			targetname = map->ents[k]->getTargetname();
			classname = map->ents[k]->getClassname();
			stat.entIdx = k;
		}
	}

	stat.classname = classname;
	stat.targetname = targetname;

	static char tmp[256];

	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	string out;

	if (isMem) {
		sprintf(tmp, "%8.1f", val / meg);
		stat.val = to_string(val);

		sprintf(tmp, "%-5.1f MB", max / meg);
		stat.usage = tmp;
	}
	else {
		stat.model = "*" + to_string(modelInfo->modelIdx);
		stat.val = to_string(val);
	}
	if (percent >= 0.1f) {
		sprintf(tmp, "%6.1f%%%%", percent);
		stat.usage = string(tmp);
	}

	return stat;
}
