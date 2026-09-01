#include "Bsp.h"
#include "util.h"
#include "Editor.h"
#include "BspRenderer.h"
#include "PolyOctree.h"
#include "NavMeshGenerator.h"

#include <algorithm>
#include <queue>
#include <float.h>

void Bsp::delete_face(int faceId) {
	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (!model.nFaces)
			continue;

		if (faceId < model.iFirstFace) {
			model.iFirstFace--;
		}
		else if (model.iFirstFace + model.nFaces > faceId) {
			model.nFaces--;
		}

		if (model.nFaces == 0)
			model.iFirstFace = 0;
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE& node = nodes[i];

		if (!node.nFaces)
			continue;

		if (faceId < node.firstFace) {
			node.firstFace--;
		}
		else if (node.firstFace + node.nFaces > faceId) {
			node.nFaces--;
		}

		if (!node.nFaces)
			node.firstFace = 0;
	}

	for (int i = 0; i < marksurfCount; i++) {
		if (marksurfs[i] > faceId) {
			marksurfs[i] -= 1;
			continue;
		}
		if (marksurfs[i] != faceId) {
			continue;
		}

		for (int k = 0; k < leafCount; k++) {
			BSPLEAF& leaf = leaves[k];

			if (!leaf.nMarkSurfaces)
				continue;

			if (i < leaf.iFirstMarkSurface) {
				leaf.iFirstMarkSurface--;
			}
			else if (leaf.iFirstMarkSurface + leaf.nMarkSurfaces > i) {
				leaf.nMarkSurfaces--;
			}

			if (!leaf.nMarkSurfaces)
				leaf.iFirstMarkSurface = 0;
		}

		memmove(marksurfs + i, marksurfs + i + 1, (marksurfCount - (i + 1)) * sizeof(BSPMARKSURF));
		marksurfCount--;
		i--;
	}
}

void Bsp::delete_faces(vector<int>& faceIds) {
	int oldMarkSurfCount = marksurfCount;

	unordered_set<int> removeSet;

	sort(faceIds.begin(), faceIds.end(), [](const int& a, const int& b) {
		return a > b;
		});

	for (int f : faceIds) {
		removeSet.insert(f);
		delete_face(f);
	}

	if (marksurfCount != oldMarkSurfCount) {
		BSPMARKSURF* newMarks = new BSPMARKSURF[marksurfCount];
		memcpy(newMarks, marksurfs, marksurfCount * sizeof(BSPMARKSURF));
		replace_lump(LUMP_MARKSURFACES, newMarks, marksurfCount * sizeof(BSPMARKSURF));
	}

	int newFaceCount = faceCount - faceIds.size();
	BSPFACE* newFaces = new BSPFACE[newFaceCount];
	memcpy(newFaces, faces, newFaceCount * sizeof(BSPFACE));

	int idx = 0;
	for (int i = 0; i < faceCount; i++) {
		if (removeSet.count(i))
			continue;
		newFaces[idx++] = faces[i];
	}

	replace_lump(LUMP_FACES, newFaces, newFaceCount * sizeof(BSPFACE));
}

bool Bsp::subdivide_face(int faceIdx, bool dryRunForExtents) {
	BSPFACE& face = faces[faceIdx];
	BSPPLANE& plane = planes[face.iPlane];
	BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

	vector<vec3> faceVerts;
	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[0] : edge.iVertex[1];

		faceVerts.push_back(verts[vertIdx]);
	}

	Polygon3D poly(faceVerts);

	vec3 minVertU, maxVertU;
	vec3 minVertV, maxVertV;

	float minU = FLT_MAX;
	float maxU = -FLT_MAX;
	float minV = FLT_MAX;
	float maxV = -FLT_MAX;
	for (int i = 0; i < faceVerts.size(); i++) {
		vec3& pos = faceVerts[i];

		float u = dotProduct(info.vS, pos);
		float v = dotProduct(info.vT, pos);

		if (u < minU) {
			minU = u;
			minVertU = pos;
		}
		if (u > maxU) {
			maxU = u;
			maxVertU = pos;
		}
		if (v < minV) {
			minV = v;
			minVertV = pos;
		}
		if (v > maxV) {
			maxV = v;
			maxVertV = pos;
		}
	}
	vec2 axisU = poly.project(info.vS).normalize();
	vec2 axisV = poly.project(info.vT).normalize();

	vec2 midVertU = poly.project(minVertU + (maxVertU - minVertU) * 0.5f);
	vec2 midVertV = poly.project(minVertV + (maxVertV - minVertV) * 0.5f);

	Line2D ucut(midVertU + axisV * 1000.0f, midVertU + axisV * -1000.0f);
	Line2D vcut(midVertV + axisU * 1000.0f, midVertV + axisU * -1000.0f);

	int size[2];
	GetFaceLightmapSize(this, faceIdx, size);

	Line2D& cutLine = size[0] > size[1] ? ucut : vcut;

	vector<vector<vec3>> polys = poly.cut(cutLine);

	if (polys.empty()) {
		return false;
	}

	int addVerts = polys[0].size() + polys[1].size();

	int addMarks = 0;
	if (!dryRunForExtents) {
		for (int i = 0; i < marksurfCount; i++) {
			if (marksurfs[i] == faceIdx) {
				addMarks++;
			}
		}
	}
	int totalMarks = marksurfCount + addMarks;

	bool limitExceeded = false;
	if (totalMarks >= INT32_MAX) {
		logf("Exceeded max marksurfaces while subdividing face\n");
		limitExceeded = true;
	}
	if (faceCount + 1 >= INT32_MAX) {
		logf("Exceeded max faces while subdividing face\n");
		limitExceeded = true;
	}
	if (edgeCount + addVerts >= INT32_MAX) {
		logf("Exceeded max edges while subdividing face\n");
		limitExceeded = true;
	}
	if (surfedgeCount + addVerts >= INT32_MAX) {
		logf("Exceeded max edges while subdividing face\n");
		limitExceeded = true;
	}
	if (vertCount + addVerts >= INT32_MAX) {
		logf("Exceeded max vertexes while subdividing face\n");
		limitExceeded = true;
	}

	if (limitExceeded) {
		return false;
	}

	BSPFACE* newFaces = new BSPFACE[faceCount + 1];
	memcpy(newFaces, faces, faceIdx * sizeof(BSPFACE));
	memcpy(newFaces + faceIdx + 1, faces + faceIdx, (faceCount - faceIdx) * sizeof(BSPFACE));

	BSPMARKSURF* newMarkSurfs = NULL;
	if (!dryRunForExtents) {
		newMarkSurfs = new BSPMARKSURF[totalMarks];
		memcpy(newMarkSurfs, marksurfs, marksurfCount * sizeof(BSPMARKSURF));
	}

	BSPEDGE* newEdges = new BSPEDGE[addVerts];
	vec3* newVerts = new vec3[addVerts];
	int32_t* newSurfEdges = new int32_t[addVerts];

	int oldSurfBegin = face.iFirstEdge;
	int oldSurfEnd = face.iFirstEdge + face.nEdges;

	BSPEDGE* edgePtr = newEdges;
	vec3* vertPtr = newVerts;
	int32_t* surfedgePtr = newSurfEdges;

	for (int k = 0; k < 2; k++) {
		vector<vec3>& cutPoly = polys[k];

		newFaces[faceIdx + k] = faces[faceIdx];
		newFaces[faceIdx + k].iFirstEdge = (surfedgePtr - newSurfEdges) + surfedgeCount;
		newFaces[faceIdx + k].nEdges = cutPoly.size();

		int vertOffset = (vertPtr - newVerts) + vertCount;
		int edgeOffset = (edgePtr - newEdges) + edgeCount;

		for (int i = 0; i < cutPoly.size(); i++) {
			edgePtr->iVertex[0] = vertOffset + i;
			edgePtr->iVertex[1] = vertOffset + ((i + 1) % cutPoly.size());
			edgePtr++;

			*vertPtr++ = cutPoly[i];

			// IMPORTANT: Logically it shouldn't matter if you use the first or second index of
			// the edge, but using the first one crashes the software renderer. You will see the
			// face stretching out to infinity before the crash. Doesn't make sense because the
			// same verts are visited and in the same order, just with a different offset.
			*surfedgePtr++ = edgeOffset + i;
		}
	}

	if (!dryRunForExtents) {
		for (int i = 0; i < modelCount; i++) {
			BSPMODEL& model = models[i];

			if (model.iFirstFace > faceIdx) {
				model.iFirstFace += 1;
			}
			else if (model.iFirstFace <= faceIdx && model.iFirstFace + model.nFaces > faceIdx) {
				model.nFaces++;
			}
		}

		for (int i = 0; i < nodeCount; i++) {
			BSPNODE& node = nodes[i];

			if (node.firstFace > faceIdx) {
				node.firstFace += 1;
			}
			else if (node.firstFace <= faceIdx && node.firstFace + node.nFaces > faceIdx) {
				node.nFaces++;
			}
		}

		for (int i = 0; i < totalMarks; i++) {
			if (newMarkSurfs[i] == faceIdx) {
				memmove(newMarkSurfs + i + 1, newMarkSurfs + i, (totalMarks - (i + 1)) * sizeof(BSPMARKSURF));
				newMarkSurfs[i + 1] = faceIdx + 1;

				for (int k = 0; k < leafCount; k++) {
					BSPLEAF& leaf = leaves[k];

					if (!leaf.nMarkSurfaces)
						continue;
					else if (leaf.iFirstMarkSurface > i) {
						leaf.iFirstMarkSurface += 1;
					}
					else if (leaf.iFirstMarkSurface <= i && leaf.iFirstMarkSurface + leaf.nMarkSurfaces > i) {
						//logf("Added mark %d/%d to leaf %d (%d + %d)\n", i, marksurfCount, k, leaf.iFirstMarkSurface, leaf.nMarkSurfaces);
						leaf.nMarkSurfaces += 1;
					}
				}

				i++; // skip the other side of the subdivided face, or else it triggers the next block
			}
			else if (newMarkSurfs[i] > faceIdx) {
				newMarkSurfs[i]++;
			}
		}
	}

	if (!dryRunForExtents) {
		replace_lump(LUMP_MARKSURFACES, newMarkSurfs, totalMarks * sizeof(BSPMARKSURF));
	}

	replace_lump(LUMP_FACES, newFaces, (faceCount + 1) * sizeof(BSPFACE));
	append_lump(LUMP_EDGES, newEdges, addVerts * sizeof(BSPEDGE));
	append_lump(LUMP_SURFEDGES, newSurfEdges, addVerts * sizeof(int32_t));
	append_lump(LUMP_VERTICES, newVerts, addVerts * sizeof(vec3));

	delete[] newEdges;

	return true;
}

bool Bsp::has_bad_extents(int textureIdx, float scale) {
	BSPMIPTEX* tex = get_texture(textureIdx);
	if (!tex) {
		return false;
	}

	int newWidth, newHeight;
	get_scaled_texture_dimensions(textureIdx, scale, newWidth, newHeight);

	float actualScaleX = (float)newWidth / tex->nWidth;
	float actualScaleY = (float)newHeight / tex->nHeight;

	for (int i = 0; i < faceCount; i++) {
		BSPTEXTUREINFO& info = texinfos[faces[i].iTextureInfo];

		if ((info.nFlags & TEX_SPECIAL) || info.iMiptex != textureIdx) {
			continue;
		}

		BSPTEXTUREINFO oldInfo = info;

		adjust_resized_texture_coordinates(faces[i], info, newWidth, newHeight, tex->nWidth, tex->nHeight);

		int size[2];
		if (!GetFaceLightmapSize(this, i, size)) {
			info = oldInfo;
			return true;
		}

		info = oldInfo;
	}

	return false;
}

bool Bsp::has_bad_extents() {
	for (int i = 0; i < faceCount; i++) {
		BSPTEXTUREINFO& info = texinfos[faces[i].iTextureInfo];

		if ((info.nFlags & TEX_SPECIAL)) {
			continue;
		}

		int size[2];
		if (!GetFaceLightmapSize(this, i, size)) {
			return true;
		}
	}

	return false;
}

float Bsp::get_scale_to_fix_bad_extents(int textureIdx) {
	float bestScale = 1.0f;
	float lastWorkingScale = 1.0f;
	bool hadBadExtents = false;

	while (has_bad_extents(textureIdx, bestScale)) {
		bestScale -= bestScale > 0.1f ? 0.1f : 0.01f; // coarse adjust
		if (bestScale < 0) {
			bestScale = FLT_MIN;
			break;
		}
		hadBadExtents = true;
	}
	if (hadBadExtents) {
		while (!has_bad_extents(textureIdx, bestScale)) {
			lastWorkingScale = bestScale;
			bestScale += 0.01f; // fine tuning
		}
		bestScale = lastWorkingScale - 0.02f; // undo last bad step and add epsilon
	}

	return bestScale;
}

int Bsp::get_subdivisions_needed_to_fix_mip_extents(int mip) {
	bool anySubdivides = true;

	LumpState oldLumps = duplicate_lumps(
		(1 << LUMP_FACES) | (1 << LUMP_EDGES) | (1 << LUMP_SURFEDGES) | (1 << LUMP_VERTICES)
	);

	int numSub = 0;

	// dry run to see how many subdivisions are needed
	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.nFlags & TEX_SPECIAL || info.iMiptex != mip) {
			continue;
		}

		int size[2];
		if (GetFaceLightmapSize(this, faceIdx, size)) {
			continue;
		}

		if (subdivide_face(faceIdx, true)) {
			anySubdivides = true;
			numSub++;
			fa--;
		}
		else {
			numSub++;
		}
	}

	// undo all changes
	replace_lumps(oldLumps);

	return numSub;
}

void Bsp::fix_all_bad_surface_extents_with_subdivide(int subdivideLimitPerTexture) {
	unordered_set<int> bad_extent_mips;
	for (int fa = 0; fa < faceCount; fa++) {
		BSPFACE& face = faces[fa];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.nFlags & TEX_SPECIAL)
			continue;

		int size[2];
		if (GetFaceLightmapSize(this, fa, size)) {
			continue;
		}

		bad_extent_mips.insert(info.iMiptex);
	}

	// count subdivisions needed for each mip
	unordered_map<int, int> bad_extent_mips_count;
	for (int mip : bad_extent_mips) {
		bad_extent_mips_count[mip] = get_subdivisions_needed_to_fix_mip_extents(mip);
	}

	// exclude mips that needed more subdivisions than is allowed
	unordered_set<int> subdivide_mips;
	for (auto item : bad_extent_mips_count) {
		if (item.second < subdivideLimitPerTexture && item.second > 0) {
			BSPMIPTEX* tex = get_texture(item.first);
			if (!tex) {
				continue;
			}
			logf("%d subdivides needed for texture %s (%dx%d)\n", item.second, tex->szName, tex->nWidth, tex->nHeight);
			subdivide_mips.insert(item.first);
		}
	}

	unordered_set<int> repeatErrors;
	int numSub = 0;

	// subdivide again only for the target mips

	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.nFlags & TEX_SPECIAL || !subdivide_mips.count(info.iMiptex)) {
			continue;
		}

		int size[2];
		if (GetFaceLightmapSize(this, faceIdx, size)) {
			continue;
		}

		if (subdivide_face(faceIdx)) {
			numSub++;
			fa--;
		}
		else {
			BSPMIPTEX* tex = get_texture(info.iMiptex);
			if (!tex) {
				continue;
			}
			vec3 center = get_face_center(faceIdx);
			if (!repeatErrors.count(faceIdx)) {
				logf("Failed to subdivide face %d %s (%d %d %d)\n", faceIdx, tex->szName,
					(int)center.x, (int)center.y, (int)center.z);
				repeatErrors.insert(faceIdx);
			}
		}
	}

	logf("Subdivided %d faces across %d textures (%d skipped).\n", numSub, subdivide_mips.size(),
		bad_extent_mips.size() - subdivide_mips.size());
}

int Bsp::fix_bad_surface_extents_with_subdivide(int faceIdx) {
	vector<int> faces;
	faces.push_back(faceIdx);

	int totalFaces = 1;

	while (faces.size()) {
		int size[2];
		int i = faces[faces.size() - 1];
		if (GetFaceLightmapSize(this, i, size)) {
			faces.pop_back();
			continue;
		}

		// adjust face indexes if about to split a face with a lower index 
		for (int i = 0; i < faces.size(); i++) {
			if (faces[i] > i) {
				faces[i]++;
			}
		}

		if (subdivide_face(i)) {
			totalFaces++;
			faces.push_back(i + 1);
			faces.push_back(i);
		}
	}

	if (totalFaces != 1) {
		logf("Subdivided face %d into %d faces\n", faceIdx, totalFaces);
	}
	else {
		debugf("Face %d does not need to be subdivided\n", faceIdx);
	}

	return totalFaces - 1;
}

void Bsp::fix_bad_surface_extents_with_downscale(int minTextureDim) {
	int numShrink = 0;

	static vector<Wad*> emptyWads;
	vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;

	unordered_set<int> bad_extent_mips;

	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		int size[2];
		if (GetFaceLightmapSize(this, faceIdx, size)) {
			continue;
		}

		bad_extent_mips.insert(info.iMiptex);
	}

	unordered_set<int> embedded_mips;
	unordered_set<int> resized_mips;
	for (int mip : bad_extent_mips) {
		BSPMIPTEX* tex = get_texture(mip);
		if (!tex) {
			continue;
		}

		if (tex->nOffsets[0] != 0) {
			continue;
		}

		if (tex->nWidth > minTextureDim || tex->nHeight > minTextureDim) {
			embed_texture(mip, wads);
			embedded_mips.insert(mip);
		}
	}

	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.nFlags & TEX_SPECIAL) {
			continue;
		}

		int size[2];
		if (GetFaceLightmapSize(this, faceIdx, size)) {
			continue;
		}

		int mip = info.iMiptex;
		if (downscale_texture(mip, minTextureDim, false)) {
			// retry after downscaling
			resized_mips.insert(mip);
			numShrink++;
			fa--;
			continue;
		}
	}

	for (int mip : embedded_mips) {
		if (resized_mips.find(mip) != resized_mips.end()) {
			continue;
		}

		unembed_texture(mip, wads);
	}

	logf("Downscaled %d textures\n", numShrink);
}

int Bsp::count_faces_for_mip(int miptex) {
	int count = 0;

	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.iMiptex == miptex) {
			count++;
		}
	}

	return count;
}

bool Bsp::fix_bad_surface_extents_with_scale(int faceIdx) {
	BSPFACE& face = faces[faceIdx];
	BSPTEXTUREINFO* info = &texinfos[face.iTextureInfo];

	if (info->nFlags & TEX_SPECIAL) {
		return false;
	}

	int size[2];
	if (GetFaceLightmapSize(this, faceIdx, size)) {
		return false;
	}

	info = get_unique_texinfo(faceIdx);

	vec2 oldScale(1.0f / info->vS.length(), 1.0f / info->vT.length());
	BSPTEXTUREINFO oldInfo = *info;

	bool scaledOk = false;
	for (int i = 0; i < 128; i++) {
		info->vS *= 0.5f;
		info->vT *= 0.5f;

		if (GetFaceLightmapSize(this, faceIdx, size)) {
			scaledOk = true;
			break;
		}
	}

	BSPMIPTEX* tex = get_texture(info->iMiptex);
	if (!tex) {
		return false;
	}

	if (!scaledOk) {
		*info = oldInfo;
		logf("Failed to fix face %s with scales %f %f\n", tex->szName, oldScale.x, oldScale.y);
	}
	else {
		vec2 newScale(1.0f / info->vS.length(), 1.0f / info->vT.length());

		vec3 center = get_face_center(faceIdx);
		logf("Scaled up %s from %.2fx%.2f -> %.2fx%.2f (%d %d %d)\n",
			tex->szName, oldScale.x, oldScale.y, newScale.x, newScale.y,
			(int)center.x, (int)center.y, (int)center.z);
	}

	return true;
}

void Bsp::fix_bad_surface_extents_with_scale() {
	int numScale = 0;

	// create unique texinfos in case any are shared with both good and bad faces
	for (int fa = 0; fa < faceCount; fa++) {
		int faceIdx = fa;
		BSPFACE& face = faces[faceIdx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		if (info.nFlags & TEX_SPECIAL) {
			continue;
		}

		int size[2];
		if (GetFaceLightmapSize(this, faceIdx, size)) {
			continue;
		}

		get_unique_texinfo(faceIdx);
	}


	for (int fa = 0; fa < faceCount; fa++) {
		if (fix_bad_surface_extents_with_scale(fa)) {
			numScale++;
		}
	}

	logf("Scaled up %d face textures\n", numScale);
}

vec3 Bsp::get_face_center(int faceIdx) {
	BSPFACE& face = faces[faceIdx];

	vec3 centroid;

	for (int k = 0; k < face.nEdges; k++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + k];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		centroid += verts[vertIdx];
	}

	return centroid / (float)face.nEdges;
}

vec3 Bsp::get_face_ut_reference(int faceIdx) {
	BSPFACE& face = faces[faceIdx];

	vec3 a, b;
	for (int k = 0; k < face.nEdges && k < 2; k++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + k];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

		if (k == 0) {
			a = verts[vertIdx];
		}
		else {
			b = verts[vertIdx];
		}
	}

	return (a - b).normalize();
}

void Bsp::get_face_verts(int faceIdx, vector<vec3>& outVerts) {
	if (faceIdx < 0 || faceIdx >= faceCount) {
		outVerts.clear();
		return;
	}

	BSPFACE& face = faces[faceIdx];

	outVerts.resize(face.nEdges);

	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		outVerts[e] = verts[vertIdx];
	}
}

void Bsp::get_face_bounding_box(int faceIdx, vec3& mins, vec3& maxs) {
	if (faceIdx < 0 || faceIdx >= faceCount) {
		return;
	}

	BSPFACE& face = faces[faceIdx];

	maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);

	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		expandBoundingBox(verts[vertIdx], mins, maxs);
	}
}

void Bsp::get_face_plane(int faceIdx, vec3& v0, vec3& normal) {
	if (faceIdx < 0 || faceIdx >= faceCount) {
		return;
	}

	BSPFACE& face = faces[faceIdx];
	int32_t edgeIdx = surfedges[face.iFirstEdge];
	BSPEDGE& edge = edges[abs(edgeIdx)];
	int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

	v0 = verts[vertIdx];
	normal = face.nPlaneSide ? planes[face.iPlane].vNormal * -1 : planes[face.iPlane].vNormal;
}

void Bsp::adjust_resized_texture_coordinates(BSPFACE& face, BSPTEXTUREINFO& info, int newWidth, int newHeight, int oldWidth, int oldHeight) {
	// scale up face texture coordinates
	float scaleX = newWidth / (float)oldWidth;
	float scaleY = newHeight / (float)oldHeight;

	// get any vert on the face to use a reference point. Why?
	// When textures are scaled, the texture relative to the face will depend on how far away its
	// vertices are from the world origin. This means faces far away from the world origin shift many
	// pixels per scale unit, and faces aligned with the world origin don't shift at all when scaled.
	int32_t edgeIdx = surfedges[face.iFirstEdge];
	BSPEDGE& edge = edges[abs(edgeIdx)];
	int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
	vec3 vert = verts[vertIdx];

	vec3 oldvs = info.vS;
	vec3 oldvt = info.vT;
	info.vS *= scaleX;
	info.vT *= scaleY;

	// get before/after uv coordinates
	float oldu = (dotProduct(oldvs, vert) + info.shiftS) * (1.0f / (float)oldWidth);
	float oldv = (dotProduct(oldvt, vert) + info.shiftT) * (1.0f / (float)oldHeight);
	float u = dotProduct(info.vS, vert) + info.shiftS;
	float v = dotProduct(info.vT, vert) + info.shiftT;

	// undo the shift in uv coordinates for this face
	info.shiftS += (oldu * newWidth) - u;
	info.shiftT += (oldv * newHeight) - v;
}

void Bsp::adjust_resized_texture_coordinates(int textureId, int oldWidth, int oldHeight) {
	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return;
	}

	int newWidth = tex->nWidth;
	int newHeight = tex->nHeight;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (texinfos[face.iTextureInfo].iMiptex != textureId)
			continue;

		// each affected face should have a unique texinfo because
		// the shift amount may be different for every face after scaling
		BSPTEXTUREINFO* info = get_unique_texinfo(i);

		adjust_resized_texture_coordinates(face, *info, newWidth, newHeight, oldWidth, oldHeight);
	}
}

unordered_set<int> Bsp::select_connected_faces(vector<int>& srcFaces, unordered_set<int>& ignoreFaces, bool planarOnly, bool textureOnly) {
	struct TestPoly {
		Polygon3D* poly;
		int miptex;
		vec3 normal;
	};

	unordered_set<int> selected;
	queue<TestPoly> testPolys;
	vector<Polygon3D*> polys;
	vector<int> polyModels; // maps a polygon index to a model index

	for (int fa = 0; fa < faceCount; fa++) {
		polyModels.push_back(get_model_from_face(fa));

		BSPFACE& face = faces[fa];

		vector<vec3> faceVerts;

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
			faceVerts.push_back(verts[vertIdx]);
		}

		Polygon3D* poly = new Polygon3D(faceVerts, polys.size(), true);

		polys.push_back(poly);
	}

	for (int idx : srcFaces) {
		BSPFACE& face = faces[idx];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		BSPPLANE& plane = planes[face.iPlane];

		vector<vec3> selectedVerts;
		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
			selectedVerts.push_back(verts[vertIdx]);
		}

		TestPoly tpoly;
		tpoly.poly = polys[idx];
		tpoly.miptex = info.iMiptex;
		tpoly.normal = plane.vNormal;

		testPolys.push(tpoly);
	}

	PolygonOctree* octree = NavMeshGenerator::createPolyOctree(this, polys, 6);

	while (testPolys.size()) {
		TestPoly tpoly = testPolys.front();
		testPolys.pop();

		int srcModel = polyModels[tpoly.poly->idx];
		unordered_set<int> regionPolys = octree->getPolysInRegion(tpoly.poly);

		for (int ridx : regionPolys) {
			int idx = polys[ridx]->idx;
			BSPFACE& faceA = faces[idx];

			if (polyModels[idx] != srcModel)
				continue;

			if (selected.count(idx) || ignoreFaces.count(idx))
				continue;

			if (textureOnly && tpoly.miptex != texinfos[faceA.iTextureInfo].iMiptex)
				continue;

			if (planarOnly) {
				BSPPLANE& plane = planes[faceA.iPlane];
				if (plane.vNormal != tpoly.normal)
					continue;
			}

			bool isConnected = false;

			for (int e = 0; e < faceA.nEdges && !isConnected; e++) {
				int32_t edgeIdx = surfedges[faceA.iFirstEdge + e];
				BSPEDGE& edge = edges[abs(edgeIdx)];
				int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
				const float epsilon = 1.0f;

				vec3& v2 = verts[vertIdx];
				for (const vec3& v1 : tpoly.poly->verts) {
					if ((v1 - v2).length() < epsilon) {
						isConnected = true;
						break;
					}
				}
			}

			// shares an edge. Select this face
			if (isConnected) {
				for (int e = 0; e < faceA.nEdges; e++) {
					int32_t edgeIdx = surfedges[faceA.iFirstEdge + e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
				}

				selected.insert(idx);

				TestPoly newTpoly;
				newTpoly.poly = polys[ridx];
				newTpoly.miptex = tpoly.miptex;
				newTpoly.normal = tpoly.normal;
				testPolys.push(newTpoly);
			}
		}
	}

	for (Polygon3D* poly : polys) {
		delete poly;
	}
	delete octree;

	return selected;
}

BSPTEXTUREINFO* Bsp::get_unique_texinfo(int faceIdx) {
	BSPFACE& targetFace = faces[faceIdx];
	int targetInfo = targetFace.iTextureInfo;

	for (int i = 0; i < faceCount; i++) {
		if (i != faceIdx && faces[i].iTextureInfo == targetFace.iTextureInfo) {
			int newInfo = create_texinfo();
			texinfos[newInfo] = texinfos[targetInfo];
			targetInfo = newInfo;
			targetFace.iTextureInfo = newInfo;
			debugf("Create new texinfo\n");
			break;
		}
	}

	return &texinfos[targetInfo];
}

bool Bsp::isInteriorFace(const Polygon3D& poly, int hull) {
	int headnode = models[0].iHeadnodes[hull];
	vec3 testPos = poly.center + poly.plane_z * 0.5f;
	return pointContents(headnode, testPos, hull) == CONTENTS_EMPTY;
}

int Bsp::addTextureInfo(BSPTEXTUREINFO& copy) {
	BSPTEXTUREINFO* newInfos = new BSPTEXTUREINFO[texinfoCount + 1];
	memcpy(newInfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	int newIdx = texinfoCount;
	newInfos[newIdx] = copy;

	replace_lump(LUMP_TEXINFO, newInfos, (texinfoCount + 1) * sizeof(BSPTEXTUREINFO));

	return newIdx;
}

vector<ScalableTexinfo> Bsp::getScalableTexinfos(int modelIdx) {
	BSPMODEL& model = models[modelIdx];
	vector<ScalableTexinfo> scalable;
	set<int> visitedTexinfos;

	for (int k = 0; k < model.nFaces; k++) {
		BSPFACE& face = faces[model.iFirstFace + k];
		int texinfoIdx = face.iTextureInfo;

		if (visitedTexinfos.find(texinfoIdx) != visitedTexinfos.end()) {
			continue;
			//texinfoIdx = face.iTextureInfo = addTextureInfo(texinfos[texinfoIdx]);
		}
		visitedTexinfos.insert(texinfoIdx);

		ScalableTexinfo st;
		st.oldS = texinfos[texinfoIdx].vS;
		st.oldT = texinfos[texinfoIdx].vT;
		st.oldShiftS = texinfos[texinfoIdx].shiftS;
		st.oldShiftT = texinfos[texinfoIdx].shiftT;
		st.texinfoIdx = texinfoIdx;
		st.planeIdx = face.iPlane;
		st.faceIdx = model.iFirstFace + k;
		scalable.push_back(st);
	}

	return scalable;
}

void Bsp::move_texinfo(int idx, vec3 offset) {
	BSPTEXTUREINFO& info = texinfos[idx];

	BSPMIPTEX* tex = get_texture(info.iMiptex);
	if (!tex) {
		return;
	}

	vec3 offsetDir = offset.normalize();
	float offsetLen = offset.length();

	float scaleS = info.vS.length();
	float scaleT = info.vT.length();
	vec3 nS = info.vS.normalize();
	vec3 nT = info.vT.normalize();

	vec3 newOriS = offset + (nS * info.shiftS);
	vec3 newOriT = offset + (nT * info.shiftT);

	float shiftScaleS = dotProduct(offsetDir, nS);
	float shiftScaleT = dotProduct(offsetDir, nT);

	int olds = info.shiftS;
	int oldt = info.shiftT;

	float shiftAmountS = shiftScaleS * offsetLen * scaleS;
	float shiftAmountT = shiftScaleT * offsetLen * scaleT;

	info.shiftS -= shiftAmountS;
	info.shiftT -= shiftAmountT;

	// minimize shift values (just to be safe. floats can be p wacky and zany)
	while (fabs(info.shiftS) > tex->nWidth) {
		info.shiftS += (info.shiftS < 0) ? (int)tex->nWidth : -(int)(tex->nWidth);
	}
	while (fabs(info.shiftT) > tex->nHeight) {
		info.shiftT += (info.shiftT < 0) ? (int)tex->nHeight : -(int)(tex->nHeight);
	}
}
