#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "rad.h"
#include "lodepng.h"
#include "Editor.h"
#include "Clipper.h"
#include "Polygon3D.h"
#include "NavMeshGenerator.h"
#include "LeafNavMeshGenerator.h"
#include "PointEntRenderer.h"
#include "Texture.h"
#include "TextureAtlas.h"
#include "Bsp.h"
#include "NavMesh.h"
#include "Entity.h"
#include "Wad.h"
#include "util.h"
#include "ShaderProgram.h"
#include "globals.h"
#include <iomanip>
#include <set>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <float.h>
#include "StudioMdlRenderer.h"
#include "TextureArray.h"
#include "tga.h"
#include "bmp.h"
#include "quant.h"


BspRenderer::BspRenderer(Bsp* map, PointEntRenderer* pointEntRenderer) {
	this->map = map;
	this->pointEntRenderer = pointEntRenderer;

	for (int i = 0; i < MAX_MAP_HULLS+1; i++) {
		megaRenderClipnodes.buffer[i] = NULL;
	}

	memset(skyboxTextures, 0, sizeof(skyboxTextures));
	memset(skyboxTexturesSwap, 0, sizeof(skyboxTexturesSwap));

	whiteTex = new Texture(1, 1);
	greyTex = new Texture(1, 1);
	redTex = new Texture(1, 1);
	blackTex = new Texture(1, 1);
	whiteTex3D = new Texture(1, 1, max(1, min(1024, g_max_texture_array_layers)));

	glTextureArray = new TextureArray();

	*((COLOR3*)(whiteTex->data)) = { 255, 255, 255 };
	*((COLOR3*)(redTex->data)) = { 110, 0, 0 };
	*((COLOR3*)(greyTex->data)) = { 64, 64, 64 };
	*((COLOR3*)(blackTex->data)) = { 0, 0, 0 };
	*((COLOR3*)(whiteTex3D->data)) = { 255, 255, 255 };

	for (int i = 0; i < whiteTex3D->depth; i++) {
		((COLOR3*)whiteTex3D->data)[i] = COLOR3(255, 255, 255);
	}

	whiteTex->upload(GL_RGB);
	redTex->upload(GL_RGB);
	greyTex->upload(GL_RGB);
	blackTex->upload(GL_RGB);

	if (g_use_texture_arrays)
		whiteTex3D->upload(GL_RGB); // only needed if texture arrays/3d textures are supported

	glCheckError("creating plain textures in BSP renderer");

	preloadTextures();
	//loadTextures();
	//loadLightmaps();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();

	numRenderClipnodes = map->modelCount;
	lightmapFuture = async(launch::async, &BspRenderer::loadLightmaps, this);
	texturesFuture = async(launch::async, &BspRenderer::loadTextures, this);
	clipnodesFuture = async(launch::async, &BspRenderer::loadClipnodes, this, false);

	if (0) {
		leavesThreadFinished = true;
		while (!isFinishedLoading()) {
			delayLoadData();
		}
	}

	if (g_app->pickMode == PICK_LEAF) {
		leavesFuture = async(launch::async, &BspRenderer::loadLeaves, this);
	}
	else {
		// leaves take a while to load, and aren't needed in most use cases
		leavesThreadFinished = true;
	}	

	//write_obj_file();
}



void BspRenderer::deleteRenderModel(RenderModel* renderModel) {
	if (renderModel == NULL || renderModel->renderGroups == NULL || renderModel->renderFaces == NULL) {
		return;
	}
	for (int k = 0; k < renderModel->groupCount; k++) {
		RenderGroup& group = renderModel->renderGroups[k];
		if (group.verts)
			delete[] group.verts;
		if (group.buffer)
			delete group.buffer;
		group.verts = NULL;
		group.buffer = NULL;
	}
	if (renderModel->renderGroups)
		delete[] renderModel->renderGroups;
	if (renderModel->renderFaces)
		delete[] renderModel->renderFaces;

	renderModel->renderGroups = NULL;
	renderModel->renderFaces = NULL;
}

void BspRenderer::deleteRenderClipnodes() {
	if (renderClipnodeDat != NULL) {
		for (int i = 0; i < numRenderClipnodes; i++) {
			deleteRenderModelClipnodes(&renderClipnodeDat[i]);
		}
		delete[] renderClipnodeDat;
	}

	renderClipnodeDat = NULL;
}

void BspRenderer::deleteRenderLeaves() {
	if (!leavesThreadFinished) {
		errorf("ERROR: Attempted leaves data delete during construction\n");
		return;
	}

	if (renderLeafDat) {
		if (renderLeafDat->leafBuffer) {
			delete renderLeafDat->leafBuffer;
			renderLeafDat->leafBuffer = NULL;
		}

		delete renderLeafDat;
		renderLeafDat = NULL;
	}

	if (leafNavMesh) {
		delete leafNavMesh;
		leafNavMesh = NULL;
	}

	leavesLoaded = false;
}

void BspRenderer::deleteRenderModelClipnodes(RenderClipnodes* renderClip) {
	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		if (renderClip->clipnodeBuffer[i]) {
			delete renderClip->clipnodeBuffer[i];
		}
		renderClip->clipnodeBuffer[i] = NULL;
	}
}

void BspRenderer::deleteRenderFaces() {
	if (renderModels != NULL) {
		for (int i = 0; i < numRenderModels; i++) {
			deleteRenderModel(&renderModels[i]);
		}
		delete[] renderModels;
	}

	facePolys.clear();

	renderModels = NULL;
}

void BspRenderer::deleteTextures() {
	if (glTextures != NULL) {
		for (int i = 0; i < numLoadedTextures; i++) {
			delete glTextures[i];
		}
		delete[] glTextures;
		glTextures = NULL;
	}
	for (int i = 0; i < 6; i++) {
		if (skyboxTextures[i]) {
			delete skyboxTextures[i];
			skyboxTextures[i] = NULL;
		}
	}
	if (glTextureAtlases) {
		for (int i = 0; i < numTextureAtlases; i++) {
			delete glTextureAtlases[i];
		}
		delete[] glTextureAtlases;
		glTextureAtlases = NULL;
	}
	if (glPalette) {
		delete glPalette;
		glPalette = NULL;
	}
	glCheckError("deleting textures");
}

void BspRenderer::deleteLightmapTextures() {
	if (glLightmapTextures != NULL) {
		for (int i = 0; i < numLightmapAtlases; i++) {
			if (glLightmapTextures[i])
				delete glLightmapTextures[i];
		}
		delete[] glLightmapTextures;
	}

	delete[] lightmapAtlasBlackArea;
	lightmapAtlasBlackArea = NULL;

	glLightmapTextures = NULL;
}

void BspRenderer::deleteFaceMaths() {
	if (faceMaths != NULL) {
		delete[] faceMaths;
	}
	faceMathVerts.clear();
	faceMathLocalVerts.clear();

	faceMaths = NULL;
}

void BspRenderer::write_obj_file() {
	int modelIdx = 0;
	BSPMODEL& model = map->models[modelIdx];
	vector<vec3> allVerts;

	for (int i = 0; i < model.nFaces; i++) {
		int faceIdx = model.iFirstFace + i;
		BSPFACE& face = map->faces[faceIdx];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

		if (texinfo.nFlags & TEX_SPECIAL) {
			continue;
		}

		vec3* verts = new vec3[face.nEdges];
		int vertCount = face.nEdges;

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3& vert = map->verts[vertIdx];
			verts[e].x = vert.x;
			verts[e].y = vert.z;
			verts[e].z = -vert.y;
		}

		// convert TRIANGLE_FAN verts to TRIANGLES so multiple faces can be drawn in a single draw call
		int idx = 0;
		for (int k = 2; k < face.nEdges; k++) {
			allVerts.push_back(verts[0]);
			allVerts.push_back(verts[k - 1]);
			allVerts.push_back(verts[k]);
		}
	}

	ofstream file(map->name + ".obj", ios::out | ios::trunc);
	for (int i = 0; i < allVerts.size(); i++) {
		vec3 v = allVerts[i];
		file << "v " << fixed << std::setprecision(2) << v.x << " " << v.y << " " << v.z << endl;
	}
	for (int i = 0; i < allVerts.size(); i += 3) {
		file << "f " << (i+3) << " " << (i+2) << " " << (i+1) << endl;
	}
	logf("Wrote %d verts\n", allVerts.size());
	file.close();
}

BspRenderer::~BspRenderer() {
	if (lightmapFuture.wait_for(chrono::milliseconds(0)) != future_status::ready ||
		texturesFuture.wait_for(chrono::milliseconds(0)) != future_status::ready ||
		clipnodesFuture.wait_for(chrono::milliseconds(0)) != future_status::ready ||
		(leavesFuture.valid() && leavesFuture.wait_for(chrono::milliseconds(0)) != future_status::ready)) {
		errorf("ERROR: Deleted bsp renderer while it was loading\n");
	}

	if (lightmaps != NULL) {
		delete[] lightmaps;
	}
	if (renderEnts != NULL) {
		delete[] renderEnts;
	}
	if (pointEnts != NULL) {
		delete pointEnts;
	}
	if (pvsDat) {
		delete pvsDat->wireframePvsBuffer;
		delete pvsDat;
	}
	if (skyBoxBuffer)
		delete skyBoxBuffer;
	for (MegaRenderGroup& mega : megaRenderGroups) {
		delete mega.group.buffer;
	}
	for (int i = 0; i < MAX_MAP_HULLS+1; i++) {
		delete megaRenderClipnodes.buffer[i];
	}

	deleteTextures();
	deleteLightmapTextures();
	deleteRenderFaces();
	deleteRenderClipnodes();
	deleteRenderLeaves();
	deleteFaceMaths();

	// TODO: share these with all renderers
	delete whiteTex;
	delete redTex;
	delete greyTex;
	delete blackTex;
	delete whiteTex3D;

	delete glTextureArray;

	if (map)
		delete map;
}



void BspRenderer::highlightPickedFaces(bool highlight) {
	unordered_set<RenderGroup*> uploadGroups;

	for (int i = 0; i < g_app->pickInfo.faces.size(); i++) {
		RenderFace* rface;
		RenderGroup* rgroup;
		if (!getRenderPointers(g_app->pickInfo.faces[i], &rface, &rgroup)) {
			logf("Bad face index for highlight %d\n", g_app->pickInfo.faces[i]);
			continue;
		}

		float r, g, b;
		r = g = b = 255;

		if (highlight) {
			r = 220;
			g = 0;
			b = 0;
		}

		for (int k = 0; k < rface->vertCount; k++) {
			rgroup->verts[rface->vertOffset + k].c.r = r;
			rgroup->verts[rface->vertOffset + k].c.g = g;
			rgroup->verts[rface->vertOffset + k].c.b = b;
		}

		uploadGroups.insert(rgroup);
	}

	for (RenderGroup* rgroup : uploadGroups) {
		rgroup->buffer->deleteBuffer();
		rgroup->buffer->upload();
	}
}

void BspRenderer::highlightPickedLeaves(bool highlight) {
	if (!leavesLoaded || !renderLeafDat->leafBuffer)
		return;

	clipnodeVert* verts = (clipnodeVert*)renderLeafDat->leafBuffer->data;

	if (!highlight) {
		for (int i = 0; i < renderLeafDat->leafBuffer->numVerts; i++) {
			COLOR4& og = renderLeafDat->originalColors[i];
			verts[i].c.r = og.r;
			verts[i].c.g = og.g;
			verts[i].c.b = og.b;
		}
		hideLeaves(true);
	}
	else {
		for (int i = 0; i < g_app->pickInfo.leaves.size(); i++) {
			uint16_t leafIdx = g_app->pickInfo.leaves[i];

			for (int idx : renderLeafDat->leafRanges[leafIdx]) {
				verts[idx].c.r = 255;
				verts[idx].c.g = 0;
				verts[idx].c.b = 0;
			}
		}
	}

	renderLeafDat->leafBuffer->deleteBuffer();
	renderLeafDat->leafBuffer->upload();
}

void BspRenderer::hideLeaves(bool hideNotUnhide) {
	if (!leavesLoaded || !renderLeafDat->leafBuffer)
		return;

	clipnodeVert* verts = (clipnodeVert*)renderLeafDat->leafBuffer->data;

	if (!hideNotUnhide) {
		for (int i = 0; i < renderLeafDat->leafBuffer->numVerts; i++) {
			verts[i].c.a = renderLeafDat->originalColors[i].a;
		}
	}
	else {
		for (auto leafIdx : g_app->hiddenLeaves) {
			for (int idx : renderLeafDat->leafRanges[leafIdx]) {
				verts[idx].c.a = 0;
			}
		}
	}

	renderLeafDat->leafBuffer->deleteBuffer();
	renderLeafDat->leafBuffer->upload();
}

void BspRenderer::hideFaces(bool hideNotUnhide) {
	unordered_set<RenderGroup*> uploadGroups;

	for (auto faceIdx : g_app->hiddenFaces) {
		RenderFace* rface;
		RenderGroup* rgroup;

		if (!getRenderPointers(faceIdx, &rface, &rgroup)) {
			logf("Bad face index for hide %d\n", g_app->pickInfo.faces[faceIdx]);
			continue;
		}

		float a = 0;
		
		if (!hideNotUnhide) {
			BSPFACE& face = map->faces[faceIdx];
			BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
			a = (texinfo.nFlags & TEX_SPECIAL) ? 128 : 255;
		}

		for (int k = 0; k < rface->vertCount; k++) {
			rgroup->verts[rface->vertOffset + k].c.a = a;
		}

		uploadGroups.insert(rgroup);
	}

	for (RenderGroup* rgroup : uploadGroups) {
		rgroup->buffer->deleteBuffer();
		rgroup->buffer->upload();
	}
}

void BspRenderer::updateFaceUVs(int faceIdx) {
	RenderFace* rface;
	RenderGroup* rgroup;
	if (!getRenderPointers(faceIdx, &rface, &rgroup)) {
		logf("Bad face index\n");
		return;
	}

	BSPFACE& face = map->faces[faceIdx];
	BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
	BSPMIPTEX* tex = map->get_texture(texinfo.iMiptex);
	if (!tex) {
		return;
	}

	for (int i = 0; i < rface->vertCount; i++) {
		lightmapVert& vert = rgroup->verts[rface->vertOffset + i];
		vec3 pos = vec3(vert.x, -vert.z, vert.y);

		float tw = 1.0f / (float)tex->nWidth;
		float th = 1.0f / (float)tex->nHeight;
		float fU = dotProduct(texinfo.vS, pos) + texinfo.shiftS;
		float fV = dotProduct(texinfo.vT, pos) + texinfo.shiftT;
		vert.u = fU * tw;
		vert.v = fV * th;
	}

	rgroup->buffer->deleteBuffer();
	rgroup->buffer->upload();

	if (faceIdx >= map->models[0].nFaces)
		reloadMegaBuffers(); // solid entities don't update until the megabuffer refreshes
}

bool BspRenderer::getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup) {
	int modelIdx = map->get_model_from_face(faceIdx);

	if (modelIdx == -1) {
		return false;
	}

	int relativeFaceIdx = faceIdx - map->models[modelIdx].iFirstFace;
	*renderFace = &renderModels[modelIdx].renderFaces[relativeFaceIdx];
	*renderGroup = &renderModels[modelIdx].renderGroups[(*renderFace)->group];

	return true;
}

Texture* BspRenderer::uploadTexture(WADTEX* tex) {
	int lastMipSize = (tex->nWidth / 8) * (tex->nHeight / 8);
	COLOR3* palette = (COLOR3*)(tex->data + tex->nOffsets[3] + lastMipSize + 2 - 40);
	byte* src = tex->data;

	COLOR4* imageData = new COLOR4[tex->nWidth * tex->nHeight];

	int sz = tex->nWidth * tex->nHeight;

	for (int k = 0; k < sz; k++) {
		imageData[k] = COLOR4(palette[src[k]], 255);
	}

	Texture* newTex = new Texture(tex->nWidth, tex->nHeight, imageData);
	newTex->upload(GL_RGBA);

	return newTex;
}

int BspRenderer::addTextureToMap(string textureName) {
	WADTEX* tex = NULL;
	for (int i = 0; i < wads.size(); i++) {
		if (wads[i]->hasTexture(textureName)) {
			tex = wads[i]->readTexture(textureName);
			break;
		}
	}

	if (!tex) {
		return -1;
	}

	int newMiptex = map->add_texture_from_wad(tex);

	reloadTextures(true);
	
	logf("Added new texture reference for %s\n", tex->szName);

	delete tex;
	return newMiptex;
}

bool BspRenderer::updateOrderEnt(OrderedEnt& orderEnt, int i) {
	Entity* ent = map->ents[i];
	orderEnt.modelIdx = ent->getBspModelIdx();
	if (orderEnt.modelIdx >= map->modelCount || orderEnt.modelIdx < 0) { // TODO: and -1?
		return false;
	}
	orderEnt.ent = ent;
	const mat4x4& rotMat = ent->getRotationMatrix(false);
	orderEnt.transform = renderEnts[i].modelMat * rotMat;

	orderEnt.transformWorld = renderEnts[i].modelMat;
	orderEnt.transformWorld.translate(renderOffset.x, renderOffset.y, renderOffset.z);
	orderEnt.transformWorld = orderEnt.transformWorld * rotMat;
	return true;
}

void BspRenderer::updateOrderEnts() {
	mapOffset = map->ents.size() ? map->ents[0]->getOrigin() : vec3();
	renderOffset = vec3(mapOffset.x, mapOffset.z, -mapOffset.y);

	bool shouldDoFullUpdate = lastOrderEntFullUpdatePickCount != g_app->pickCount;
	lastOrderEntFullUpdatePickCount = g_app->pickCount;

	if (shouldDoFullUpdate) {
		orderEnts.clear();
		orderEnts.reserve(map->ents.size());
		orderEntIndexes.resize(map->ents.size());

		for (int i = 0; i < map->ents.size(); i++) {
			map->ents[i]->highlighted = false;

			if (i == 251)
				logf("");

			OrderedEnt newEnt;
			if (updateOrderEnt(newEnt, i)) {
				newEnt.entIdx = i;
				orderEnts.push_back(newEnt);
			}
			orderEntIndexes[i] = -1;
		}
		for (Entity* ent : g_app->pickInfo.getEnts()) {
			ent->highlighted = true;
		}

		// draw highlighted ents last, or else overlapping models don't appear selected
		sort(orderEnts.begin(), orderEnts.end(), [](const OrderedEnt& a, const OrderedEnt& b) {
			return a.ent->highlighted < b.ent->highlighted;
		});

		for (int i = 0; i < orderEnts.size(); i++) {
			orderEntIndexes[orderEnts[i].entIdx] = i;
		}
	}
	else {
		// just update the selected ones that may be moving around
		for (int idx : g_app->pickInfo.ents) {
			int orderIdx = orderEntIndexes[idx];
			if (orderIdx >= 0) {
				OrderedEnt& orderEnt = orderEnts[orderIdx];
				if (idx == 251)
					logf("");
				updateOrderEnt(orderEnt, idx);
			}
		}
	}

	refreshMegaBuffers();
}



void BspRenderer::addPvsPoly(int faceIdx, vec3 faceOffset, vec3 viewOrigin, Frustum* frustum, bool makeBuffer, vector<vec3>& allVerts) {
	PvsPoly& poly = facePolys[faceIdx];

	BSPFACE& face = map->faces[faceIdx];
	if (map->texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL)
		return; // special faces not rendered

	float dist = dotProduct((viewOrigin - faceOffset) - poly.v0, poly.normal);
	if (dist < 0) {
		return; // back face culled
	}

	if (!isBoxInView(faceOffset + poly.mins, faceOffset + poly.maxs, *frustum, 0))
		return; // frustum culled

	pvsDat->wpoly++;

	if (!makeBuffer)
		return;

	vector<vec3> verts;
	map->get_face_verts(faceIdx, verts);

	for (int k = 0; k < verts.size(); k++) {
		allVerts.push_back((faceOffset + verts[k]).flip());
		allVerts.push_back((faceOffset + verts[(k + 1) % verts.size()]).flip());
	}
}

void BspRenderer::updatePvs(vec3 viewOrigin) {
	int ileaf = map->ents.size() ? map->get_leaf(viewOrigin, 0) : 0;

	if (pvsDat) {
		delete pvsDat->wireframePvsBuffer;
		delete pvsDat;
		pvsDat = NULL;
	}

	bool makeBuffer = g_settings.render_flags & RENDER_PVS;

	pvsDat = new RenderPvs();
	pvsDat->leaf = ileaf;
	pvsDat->wireframePvsBuffer = NULL;

	if (ileaf == 0 || map->ents.empty()) {
		return;
	}

	pvsDat->pvsLeaves = map->get_pvs(pvsDat->leaf);
	pvsDat->pvsFaces = map->get_leaf_faces(pvsDat->pvsLeaves);

	vector<vec3> allVerts;

	Frustum frustum = g_app->getCameraFrustum();

	pvsDat->wpoly = 0;

	if (!map->ents[0]->hidden) {
		for (int faceIdx : pvsDat->pvsFaces) {
			addPvsPoly(faceIdx, vec3(), viewOrigin, &frustum, makeBuffer, allVerts);
		}
	}

	if (g_settings.render_flags & RENDER_ENTS) {
		for (Entity* ent : map->ents) {
			if (ent->hidden)
				continue;

			int modelIdx = ent->getBspModelIdx();
			if (modelIdx < 0 || modelIdx >= map->modelCount)
				continue;

			vec3 ori = ent->getOrigin();
			BSPMODEL& model = map->models[modelIdx];
			vec3 entMin = model.nMins + ori;
			vec3 entMax = model.nMaxs + ori;

			if (!isBoxInView(entMin, entMax, frustum, 0))
				continue; // frustum culled model

			// TODO: configure this in the FGD or something
			static unordered_set<string> visibleClassnames = {
				"func_breakable",
				"func_button",
				"func_conveyor",
				"func_detail",
				"func_door",
				"func_door_rotating",
				"func_guntarget",
				"func_healthcharger",
				"func_illusionary",
				"func_pendulum",
				"func_plat",
				"func_platrot",
				"func_pushable",
				"func_recharge",
				"func_rot_button",
				"func_rotating",
				"func_tank",
				"func_tanklaser",
				"func_tankmortar",
				"func_tankrocket",
				"func_trackautochange",
				"func_trackchange",
				"func_tracktrain",
				"func_train",
				"func_wall",
				"func_wall_toggle",
				//"func_water",
				"momentary_door",
				"momentary_rot_button",
				"button_target",
			};

			if (!visibleClassnames.count(ent->getClassname()))
				continue;

			bool inPvs = false;
			for (int leafIdx : pvsDat->pvsLeaves) {
				BSPLEAF& leaf = map->leaves[leafIdx];
			
				if (boxesIntersect(leaf.nMins, leaf.nMaxs, entMin, entMax)) {
					inPvs = true;
					break;
				}
			}

			if (!inPvs)
				continue;

			for (int i = model.iFirstFace; i < model.iFirstFace + model.nFaces; i++) {
				addPvsPoly(i, ori, viewOrigin, &frustum, makeBuffer, allVerts);
			}
		}
	}

	if (makeBuffer && allVerts.size()) {
		vec3* vertDat = new vec3[allVerts.size()];
		memcpy(vertDat, &allVerts[0], allVerts.size() * sizeof(vec3));

		pvsDat->wireframePvsBuffer = new VertexBuffer(g_shaders.vec3, vertDat, allVerts.size(), true);
		pvsDat->wireframePvsBuffer->upload();
	}
}

void BspRenderer::pickFrustum(Frustum& frustum, unordered_set<int>& pickEnts,
	unordered_set<int>& pickFaces, unordered_set<int>& pickLeaves, int hullIdx) {
	vec3 pickOffset = vec3(mapOffset.x, mapOffset.y, mapOffset.z);
	frustum.origin -= mapOffset;

	if (!map || map->ents.size() == 0) {
		return;
	}

	unordered_set<int> pickFacesWorld;
	if (!map->ents[0]->hidden) {
		pickFrustumFaces(frustum, pickFacesWorld, vec3(), vec3(), 0, hullIdx, 0);
		for (int idx : pickFacesWorld)
			pickFaces.insert(idx);
	}
	if (g_app->pickMode == PICK_LEAF) {
		pickFrustumLeaves(frustum, pickLeaves);
	}

	bool renderSmallSprites = !(g_settings.render_flags & RENDER_RENDER_MODES) && !g_app->previewMode;

	for (int i = 0, sz = map->ents.size(); i < sz; i++) {
		Entity* ent = map->ents[i];
		if (ent->hidden)
			continue;

		int modelIdx = renderEnts[i].modelIdx;

		if (modelIdx >= 0 && modelIdx < map->modelCount && modelIdx < numRenderModels) {

			bool isSpecial = false;
			for (int k = 0; k < renderModels[modelIdx].groupCount; k++) {
				if (renderModels[modelIdx].renderGroups[k].transparent) {
					isSpecial = true;
					break;
				}
			}

			if (isSpecial && !(g_settings.render_flags & RENDER_SPECIAL_ENTS)) {
				continue;
			}
			else if (!isSpecial && !(g_settings.render_flags & RENDER_ENTS)) {
				continue;
			}

			vec3 angles = map->ents[i]->canRotate() ? renderEnts[i].angles : vec3();
			unordered_set<int> pickFacesOne;
			pickFrustumFaces(frustum, pickFacesOne, renderEnts[i].offset, angles, modelIdx, hullIdx, i);
			if (pickFacesOne.size()) {
				for (int idx : pickFacesOne) {
					pickFaces.insert(idx);
				}
				pickEnts.insert(i);
			}
		}
		else if (i > 0 && g_settings.render_flags & RENDER_POINT_ENTS) {
			vec3 mins = renderEnts[i].offset + renderEnts[i].pointEntCube->mins;
			vec3 maxs = renderEnts[i].offset + renderEnts[i].pointEntCube->maxs;

			g_app->debugVec0 = mins;
			g_app->debugVec1 = maxs;

			if (isBoxInView(mins, maxs, frustum, 0)) {
				pickEnts.insert(i);
			}
			else if (ent->cachedMdl && !ent->isIconSprite && !renderSmallSprites && ent->cachedMdl->pick(frustum, ent)) {
				pickEnts.insert(i);
			}
		}
	}
}

void BspRenderer::pickFrustumFaces(Frustum frustum, unordered_set<int>& pickFaces, vec3 offset,
	vec3 rot, int modelIdx, int hullIdx, int testEntidx) {
	BSPMODEL& model = map->models[modelIdx];

	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_WIREFRAME))) {
		return;
	}
	if (map->modelCount == 0)
		return;

	frustum.origin -= offset;

	bool foundBetterPick = false;
	bool skipSpecial = !(g_settings.render_flags & RENDER_SPECIAL) || g_app->previewMode;

	bool hasAngles = rot != vec3();
	mat4x4 angleTransform = map->ents[testEntidx]->getRotationMatrix(true);

	if (hasAngles) {
		angleTransform = angleTransform.invert();
		for (int i = 0; i < 4; i++) {
			frustum.planes[i] = angleTransform.multColMajor(vec4(frustum.planes[i], 0)).xyz();
		}
		frustum.origin = angleTransform.multColMajor(vec4(frustum.origin, 1)).xyz();
	}

	for (int k = 0; k < model.nFaces && model.iFirstFace + k < map->faceCount; k++) {
		if (g_app->hiddenFaces.count(model.iFirstFace + k))
			continue;

		FaceMath* faceMath = &faceMaths[model.iFirstFace + k];
		BSPFACE& face = map->faces[model.iFirstFace + k];

		if (skipSpecial && modelIdx == 0) {
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}
		}

		vec3* srcVerts = &faceMathVerts[faceMath->vertIdx];

		if (isPolyInView(faceMath, frustum, srcVerts)) {
			pickFaces.insert(model.iFirstFace + k);
		}
	}

	bool selectWorldClips = modelIdx == 0 && (g_settings.render_flags & RENDER_WORLD_CLIPNODES) && hullIdx != -1;
	bool selectEntClips = modelIdx > 0 && (g_settings.render_flags & RENDER_ENT_CLIPNODES);

	if (hullIdx == -1 && renderModels[modelIdx].groupCount == 0) {
		// clipnodes are visible for this model because it has no faces
		hullIdx = getBestClipnodeHull(modelIdx);
	}

	if (clipnodesLoaded && (selectWorldClips || selectEntClips) && hullIdx != -1) {
		for (int i = 0; i < renderClipnodeDat[modelIdx].faceMaths[hullIdx].size(); i++) {
			RenderClipnodes& renderClip = renderClipnodeDat[modelIdx];
			FaceMath* faceMath = &renderClip.faceMaths[hullIdx][i];

			vec3* srcVerts = &renderClip.faceMathVerts[faceMath->vertIdx];

			if (isPolyInView(faceMath, frustum, srcVerts)) {
				pickFaces.insert(-1); // face index doesn't matter for ent selection
			}
		}
	}
}

void BspRenderer::pickFrustumLeaves(Frustum frustum, unordered_set<int>& pickLeaves) {
	BSPMODEL& model = map->models[0];

	if (map->modelCount == 0)
		return;

	if (leavesLoaded) {
		for (int i = 0; i < renderLeafDat->faceMaths.size(); i++) {
			FaceMath faceMath = renderLeafDat->faceMaths[i];

			if (g_app->hiddenLeaves.count(faceMath.index))
				continue;

			vec3* srcVerts = &renderLeafDat->faceMathVerts[faceMath.vertIdx];

			if (isPolyInView(&faceMath, frustum, srcVerts)) {
				pickLeaves.insert(faceMath.index);
			}
		}
	}
}

bool BspRenderer::pickPoly(vec3 start, vec3 dir, int hullIdx, int& entIdx, int& faceIdx, int& leafIdx, float& bestDist) {
	bool foundBetterPick = false;
	entIdx = -1;
	faceIdx = -1;
	leafIdx = -1;

	vec3 pickOffset = vec3(mapOffset.x, mapOffset.y, mapOffset.z);
	start -= mapOffset;

	if (!map || map->ents.size() == 0) {
		return false;
	}

	if (!map->ents[0]->hidden && pickModelPoly(start, dir, vec3(), vec3(), 0, hullIdx, 0, faceIdx, bestDist)) {
		entIdx = 0;
		foundBetterPick = true;
	}
	if (g_app->pickMode == PICK_LEAF && pickLeaf(start, dir, leafIdx, bestDist)) {
		entIdx = 0;
		foundBetterPick = true;
	}

	bool renderSmallSprites = !(g_settings.render_flags & RENDER_RENDER_MODES) && !g_app->previewMode;

	for (int i = 0, sz = map->ents.size(); i < sz; i++) {
		Entity* ent = map->ents[i];
		if (ent->hidden)
			continue;

		int modelIdx = renderEnts[i].modelIdx;

		if (modelIdx >= 0 && modelIdx < map->modelCount && modelIdx < numRenderModels) {

			bool isSpecial = false;
			for (int k = 0; k < renderModels[modelIdx].groupCount; k++) {
				if (renderModels[modelIdx].renderGroups[k].transparent) {
					isSpecial = true;
					break;
				}
			}

			if (isSpecial && !(g_settings.render_flags & RENDER_SPECIAL_ENTS)) {
				continue;
			} else if (!isSpecial && !(g_settings.render_flags & RENDER_ENTS)) {
				continue;
			}

			vec3 angles = map->ents[i]->canRotate() ? renderEnts[i].angles : vec3();
			if (pickModelPoly(start, dir, renderEnts[i].offset, angles,
					modelIdx, hullIdx, i, faceIdx, bestDist)) {
				entIdx = i;
				foundBetterPick = true;
			}
		}
		else if (i > 0 && g_settings.render_flags & RENDER_POINT_ENTS) {
			vec3 mins = renderEnts[i].offset + renderEnts[i].pointEntCube->mins;
			vec3 maxs = renderEnts[i].offset + renderEnts[i].pointEntCube->maxs;

			if (pickAABB(start, dir, mins, maxs, bestDist)) {
				entIdx = i;
				foundBetterPick = true;
			}
			else if (ent->cachedMdl) {
				bool bigSprite = !ent->isIconSprite && !renderSmallSprites;
				bool shouldPickModel = bigSprite || ent->cachedMdl->isStudioModel();
				if (shouldPickModel && ent->cachedMdl->pick(start, dir, ent, bestDist)) {
					entIdx = i;
					foundBetterPick = true;
				}
			}
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickModelPoly(vec3 start, vec3 dir, vec3 offset, vec3 rot, int modelIdx, int hullIdx,
	int testEntidx, int& faceIdx, float& bestDist) {
	BSPMODEL& model = map->models[modelIdx];

	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_WIREFRAME))) {
		return false;
	}
	if (map->modelCount == 0)
		return false;

	bool foundBetterPick = false;
	bool skipSpecial = !(g_settings.render_flags & RENDER_SPECIAL) || g_app->previewMode;

	bool hasAngles = rot != vec3();
	mat4x4 angleTransform = map->ents[testEntidx]->getRotationMatrix(true);

	start -= offset;

	if (hasAngles) {
		angleTransform = angleTransform.invert();
		dir = angleTransform.multColMajor(vec4(dir, 0)).xyz();
		start = angleTransform.multColMajor(vec4(start, 1)).xyz();
	}

	for (int k = 0; k < model.nFaces && model.iFirstFace + k < map->faceCount; k++) {
		if (g_app->hiddenFaces.count(model.iFirstFace + k))
			continue;

		BSPFACE& face = map->faces[model.iFirstFace + k];

		if (skipSpecial && modelIdx == 0) {
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}
		}

		FaceMath* faceMath = &faceMaths[model.iFirstFace + k];

		vec2* localVerts = &faceMathLocalVerts[faceMath->vertIdx];

		float t = bestDist;
		if (pickFaceMath(start, dir, *faceMath, localVerts, t)) {
			foundBetterPick = true;
			bestDist = t;
			faceIdx = model.iFirstFace + k;
		}
	}

	bool selectWorldClips = modelIdx == 0 && (g_settings.render_flags & RENDER_WORLD_CLIPNODES) && hullIdx != -1;
	bool selectEntClips = modelIdx > 0 && (g_settings.render_flags & RENDER_ENT_CLIPNODES);

	if (hullIdx == -1 && renderModels[modelIdx].groupCount == 0) {
		// clipnodes are visible for this model because it has no faces
		hullIdx = getBestClipnodeHull(modelIdx);
	}

	if (clipnodesLoaded && (selectWorldClips || selectEntClips) && hullIdx != -1) {
		for (int i = 0; i < renderClipnodeDat[modelIdx].faceMaths[hullIdx].size(); i++) {
			RenderClipnodes& renderClip = renderClipnodeDat[modelIdx];
			FaceMath* faceMath = &renderClip.faceMaths[hullIdx][i];
			vec2* localVerts = &renderClip.faceMathLocalVerts[faceMath->vertIdx];

			float t = bestDist;
			if (pickFaceMath(start, dir, *faceMath, localVerts, t)) {
				foundBetterPick = true;
				bestDist = t;

				// Nav mesh WIP code
				if (g_app->debugNavMesh && modelIdx == 0 && hullIdx == 3) {
					static int lastPick = 0;
					
					g_app->debugPoly = debugFaces[i];
					g_app->debugNavPoly = i;

					//Polygon3D merged = debugFaces[lastPick].merge(debugFaces[i]);
					//vector<vector<vec3>> split = debugFaces[i].split(debugFaces[lastPick]);
					//logf("split %d by %d == %d\n", i, lastPick, split.size());

					NavNode& node = g_app->debugNavMesh->nodes[i];

					lastPick = i;
					logf("Picked hull %d, face %d, verts %d, area %.1f\nNav links %d\n", hullIdx, i, debugFaces[i].verts.size(), debugFaces[i].area, node.numLinks());
				}
			}
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickLeaf(vec3 start, vec3 dir, int& leafIdx, float& bestDist) {
	BSPMODEL& model = map->models[0];

	if (map->modelCount == 0)
		return false;

	bool foundBetterPick = false;

	if (leavesLoaded) {
		for (int i = 0; i < renderLeafDat->faceMaths.size(); i++) {
			FaceMath faceMath = renderLeafDat->faceMaths[i];

			if (g_app->hiddenLeaves.count(faceMath.index))
				continue;

			vec2* localVerts = &renderLeafDat->faceMathLocalVerts[faceMath.vertIdx];

			float t = bestDist;
			if (pickFaceMath(start, dir, faceMath, localVerts, t)) {
				foundBetterPick = true;
				bestDist = t;
				leafIdx = faceMath.index;
			}
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickFaceMath(vec3 start, vec3 dir, FaceMath& faceMath, vec2* localVerts, float& bestDist) {
	float dot = dotProduct(dir, faceMath.plane_z);
	if (dot >= 0) {
		return false; // don't select backfaces or parallel faces
	}

	float t = dotProduct((faceMath.plane_z * faceMath.fdist) - start, faceMath.plane_z) / dot;
	if (t < 0 || t >= bestDist) {
		return false; // intersection behind camera, or not a better pick
	}

	// transform intersection point to the plane's coordinate system
	vec3 intersection = start + dir * t;
	vec2 localRayPoint = (faceMath.worldToLocal * vec4(intersection, 1)).xy();

	// check if point is inside the polygon using the plane's 2D coordinate system
	if (!pointInsidePolygon(localVerts, faceMath.numVerts, localRayPoint)) {
		return false;
	}

	bestDist = t;
	g_app->debugVec0 = intersection;

	return true;
}

int BspRenderer::getBestClipnodeHull(int modelIdx) {
	if (!clipnodesLoaded) {
		return -1;
	}

	RenderClipnodes& clip = renderClipnodeDat[modelIdx];

	// prefer hull that most closely matches the object size from a player's perspective
	if (clip.clipnodeBuffer[0]) {
		return 0;
	}
	else if (clip.clipnodeBuffer[3]) {
		return 3;
	}
	else if (clip.clipnodeBuffer[1]) {
		return 1;
	}
	else if (clip.clipnodeBuffer[2]) {
		return 2;
	}
	
	return -1;
}

EntCube* BspRenderer::getEntCube(int idx) {
	return renderEnts[idx].pointEntCube;
}



int RenderGroup::calcMemoryUsage() {
	int bytes = sizeof(RenderGroup);
	bytes += buffer->calcMemoryUsage();
	return bytes;
}

int RenderModel::calcMemoryUsage() {
	int bytes = sizeof(RenderModel) + renderFaceCount*sizeof(RenderFace);

	for (int i = 0; i < groupCount; i++) {
		renderGroups[i].calcMemoryUsage();
	}

	return bytes;
}

int RenderEnt::calcMemoryUsage() {
	return sizeof(RenderEnt) + pointEntCube ? pointEntCube->calcMemoryUsage() : 0;
}

int FaceMath::calcMemoryUsage() {
	int bytes = sizeof(FaceMath);
	return bytes;
}

int PvsPoly::calcMemoryUsage() {
	int bytes = sizeof(FaceMath);
	return bytes;
}

int BspRenderer::calcMemoryUsage() {
	if (g_app->isLoading)
		return 0;

	int bytes = sizeof(BspRenderer);
	bytes += pointEntRenderer ? pointEntRenderer->calcMemoryUsage() : 0;
	bytes += leafNavMesh ? leafNavMesh->calcMemoryUsage() : 0;

	for (Wad* wad : wads) {
		bytes += wad->calcMemoryUsage();
	}

	bytes += numRenderLightmapInfos * sizeof(LightmapInfo);

	if (renderEnts) {
		for (int i = 0; i < map->ents.size(); i++) {
			bytes += renderEnts[i].calcMemoryUsage();
		}
	}
	if (renderModels) {
		for (int i = 0; i < numRenderModels; i++) {
			bytes += renderModels[i].calcMemoryUsage();
		}
	}
	
	for (MegaRenderGroup& mega : megaRenderGroups) {
		bytes += sizeof(MegaRenderGroup) + sizeof(EntModelGroupIdx) * mega.refs.size();
		bytes += mega.group.calcMemoryUsage();
	}
	for (int i = 0; i < MAX_MAP_HULLS + 1; i++) {
		bytes += megaRenderClipnodes.buffer[i] ? megaRenderClipnodes.buffer[i]->calcMemoryUsage() : 0;
	}
	bytes += megaRenderClipnodes.refs.size() * sizeof(int);

	if (renderClipnodeDat) {
		bytes += sizeof(RenderClipnodes)*numRenderClipnodes;

		for (int i = 0; i < numRenderClipnodes; i++) {
			RenderClipnodes& clip = renderClipnodeDat[i];

			for (int k = 0; k < MAX_MAP_HULLS; k++) {
				bytes += clip.clipnodeBuffer[k] ? clip.clipnodeBuffer[k]->calcMemoryUsage() : 0;
				bytes += clip.faceMaths->size() * sizeof(FaceMath);
				bytes += clip.faceMathVerts.size() * sizeof(vec3);
				bytes += clip.faceMathLocalVerts.size() * sizeof(vec2);
			}
		}
	}

	if (renderLeafDat) {
		bytes += sizeof(RenderLeaves);
		bytes += renderLeafDat->faceMaths.size() * sizeof(FaceMath);
		bytes += renderLeafDat->faceMathVerts.size() * sizeof(vec3);
		bytes += renderLeafDat->faceMathLocalVerts.size() * sizeof(vec2);
		bytes += renderLeafDat->originalColors.size() * sizeof(COLOR4);

		for (int i = 0; i < renderLeafDat->leafRanges.size(); i++) {
			bytes += renderLeafDat->leafRanges[i].size() * sizeof(int);
		}
	}

	for (int i = 0; i < numFaceMaths; i++) {
		bytes += faceMaths[i].calcMemoryUsage();
	}
	bytes += sizeof(vec3) * faceMathVerts.size();
	bytes += sizeof(vec2) * faceMathLocalVerts.size();

	bytes += pointEnts ? pointEnts->calcMemoryUsage() : 0;
	bytes += skyBoxBuffer ? skyBoxBuffer->calcMemoryUsage() : 0;
	
	for (PvsPoly& poly : facePolys) {
		bytes += poly.calcMemoryUsage();
	}

	if (glTextures) {
		for (int i = 0; i < numLoadedTextures; i++) {
			bytes += glTextures[i]->calcMemoryUsage();
		}
	}
	
	for (int i = 0; i < 6; i++) {
		bytes += skyboxTextures[i] ? skyboxTextures[i]->calcMemoryUsage() : 0;
	}

	bytes += glTextureArray ? sizeof(glTextureArray) : NULL;

	if (miptexToTexArray)
		bytes += map->textureCount * sizeof(TexArrayOffset);

	for (Polygon3D& poly : debugFaces) {
		bytes += poly.calcMemoryUsage();
	}

	bytes += debugNavMesh ? debugNavMesh->calcMemoryUsage() : 0;

	if (glLightmapTextures) {
		for (int i = 0; i < numLightmapAtlases; i++) {
			bytes += glLightmapTextures[i]->calcMemoryUsage();
		}
	}
	if (glTextureAtlases) {
		for (int i = 0; i < numTextureAtlases; i++) {
			bytes += glTextureAtlases[i]->calcMemoryUsage();
		}
	}
	
	bytes += textureAtlasInfos.size() * sizeof(SubTexture);
	bytes += whiteTex ? whiteTex->calcMemoryUsage() : 0;
	bytes += whiteTex3D ? whiteTex3D->calcMemoryUsage() : 0;
	bytes += redTex ? redTex->calcMemoryUsage() : 0;
	bytes += greyTex ? greyTex->calcMemoryUsage() : 0;
	bytes += blackTex ? blackTex->calcMemoryUsage() : 0;

	return bytes;
}

Texture* BspRenderer::getRgbTexture(int iMiptex) {
	if (!glTextures)
		return 0;

	Texture* tex = glTextures[iMiptex];
	int w = tex->width;
	int h = tex->height;

	COLOR3* dat = new COLOR3[w * h];
	uint8_t* tempDat = NULL;
	
	if (g_settings.pal_textures) {
		uint8_t* srcDat = (uint8_t*)tex->data;

		if (!tex->data) {
			tex->bind();
			tempDat = new uint8_t[w * h];
			glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, tempDat);
			srcDat = tempDat;
		}

		uint16_t px, py;
		palAtlasCoords(iMiptex, px, py);
		COLOR3* pal = (COLOR3*)&glPalette->data[(py*1024 + px) * sizeof(COLOR3)];

		for (int i = 0; i < w * h; i++) {
			dat[i] = pal[srcDat[i]];
		}
	}
	else {
		COLOR4* srcDat = (COLOR4*)tex->data;

		if (!tex->data) {
			tex->bind();
			tempDat = new uint8_t[w * h * 4];
			glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempDat);
			srcDat = (COLOR4*)tempDat;
		}

		for (int i = 0; i < w * h; i++) {
			dat[i] = srcDat[i].rgb();
		}
	}

	delete[] tempDat;

	Texture* newTex = new Texture(w, h, dat);
	newTex->upload(GL_RGB);
	if (!g_settings.texture_filtering) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	return newTex;
}