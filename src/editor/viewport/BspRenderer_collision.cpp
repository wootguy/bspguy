#include "BspRenderer.h"
#include "Bsp.h"
#include "Editor.h"
#include "Entity.h"
#include "VertexBuffer.h"
#include "ShaderProgram.h"
#include "Clipper.h"
#include "util.h"
#include <float.h>

bool BspRenderer::collisionReady(int hull) const {
	return hull > 0 && hull < MAX_MAP_HULLS && clipnodesLoaded && renderClipnodeDat
		&& numRenderClipnodes > 0 && !map->ents.empty() && !map->ents[0]->hidden
		&& (g_settings.render_flags & RENDER_WORLD_CLIPNODES);
}

const vector<NodeVolumeCuts>& BspRenderer::getCollisionVolumes(int hull) const {
	static const vector<NodeVolumeCuts> empty;
	return collisionReady(hull) ? worldCollisionVolumes[hull] : empty;
}

int BspRenderer::pickCollision(vec3 start, vec3 dir, int hull) {
	if (!collisionReady(hull))
		return -1;
	start -= mapOffset;
	float bestDist = FLT_MAX;
	int picked = -1;
	RenderClipnodes& clip = renderClipnodeDat[0];
	for (FaceMath& face : clip.faceMaths[hull]) {
		if (pickFaceMath(start, dir, face, &clip.faceMathLocalVerts[face.vertIdx], bestDist))
			picked = face.index;
	}
	return picked;
}

int BspRenderer::collisionAtPoint(vec3 point, int hull) const {
	if (!collisionReady(hull))
		return -1;
	point -= mapOffset;
	vector<int> branch;
	int idx = map->models[0].iHeadnodes[hull];
	while (idx >= 0) {
		if (idx >= map->clipnodeCount || branch.size() > (size_t)map->clipnodeCount)
			return -1;
		const BSPCLIPNODE& node = map->clipnodes[idx];
		if (node.iPlane < 0 || node.iPlane >= map->planeCount)
			return -1;
		const BSPPLANE& plane = map->planes[node.iPlane];
		int side = dotProduct(plane.vNormal, point) < plane.fDist ? 1 : 0;
		branch.push_back(side);
		idx = node.iChildren[side];
	}
	if (idx != CONTENTS_SOLID)
		return -1;
	const vector<NodeVolumeCuts>& volumes = worldCollisionVolumes[hull];
	for (int i = 0; i < (int)volumes.size(); i++) {
		if (volumes[i].branch == branch)
			return i;
	}
	return -1;
}

void BspRenderer::pickCollisionFrustum(Frustum frustum, int hull, unordered_set<int>& volumes) {
	if (!collisionReady(hull))
		return;
	frustum.origin -= mapOffset;
	RenderClipnodes& clip = renderClipnodeDat[0];
	// Like face selection, drag-selection includes volumes through the scene.
	for (FaceMath& face : clip.faceMaths[hull]) {
		if (isPolyInView(&face, frustum, &clip.faceMathVerts[face.vertIdx]))
			volumes.insert(face.index);
	}
}

void BspRenderer::updateCollisionSelection() {
	delete collisionSelectionBuffer;
	collisionSelectionBuffer = NULL;
	int hull = g_app->clipnodeRenderHull;
	if (!collisionReady(hull) || g_app->pickMode != PICK_CLIPNODE)
		return;

	vector<clipnodeVert> verts;
	vector<vec3> mathVerts;
	vector<vec2> localVerts;
	vector<FaceMath> maths;
	Clipper clipper;
	for (int idx : g_app->pickInfo.clipnodes) {
		if (idx < 0 || idx >= (int)worldCollisionVolumes[hull].size())
			continue;
		CMesh mesh = clipper.clip(worldCollisionVolumes[hull][idx].cuts);
		generateNodeMesh(&mesh, COLOR4(255, 128, 32, 255), verts, mathVerts, localVerts, maths, idx);
	}
	if (!verts.empty()) {
		clipnodeVert* data = new clipnodeVert[verts.size()];
		memcpy(data, verts.data(), verts.size() * sizeof(clipnodeVert));
		collisionSelectionBuffer = new VertexBuffer(g_shaders.clipnode, data, verts.size(), true);
		collisionSelectionBuffer->upload();
	}
}

void BspRenderer::renderCollisionSelection() {
	if (g_app->pickMode != PICK_CLIPNODE || g_app->previewMode
		|| !collisionReady(g_app->clipnodeRenderHull) || !collisionSelectionBuffer)
		return;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// Show the complete selected volume, including portions behind the
	// world's other collision cells. This also makes box selections reviewable.
	GLboolean hadDepthTest = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);
	g_shaders.clipnode->bind();
	g_shaders.clipnode->modelMat->loadIdentity();
	g_shaders.clipnode->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_shaders.clipnode->updateMatrixes();
	g_shaders.clipnode->setUniform("colorMult", vec4(1, 1, 1, 1));
	g_shaders.clipnode->setUniform("opacity", 0.65f);
	collisionSelectionBuffer->draw(g_shaders.clipnode, GL_TRIANGLES);
	g_shaders.clipnode->setUniform("opacity", 0.5f);
	if (hadDepthTest)
		glEnable(GL_DEPTH_TEST);
}
