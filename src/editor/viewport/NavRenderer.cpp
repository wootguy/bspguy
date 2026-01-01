#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "NavRenderer.h"
#include "LeafNavMeshGenerator.h"
#include "NavMesh.h"
#include "util.h"
#include "ShaderProgram.h"
#include "render_utils.h"
#include "Bsp.h"
#include "Editor.h"
#include "VertexBuffer.h"
#include "Gui.h"
#include "Entity.h"
#include "BspRenderer.h"

NavRenderer::NavRenderer() {

}

NavRenderer::~NavRenderer() {
	if (debugLeafNavMesh) {
		delete debugLeafNavMesh;
		debugLeafNavMesh = NULL;
	}
}

void NavRenderer::controls() {
	Bsp* map = g_app->mapRenderer->map;

	if (g_app->pressed[GLFW_KEY_F] && !g_app->oldPressed[GLFW_KEY_F] && !g_app->anyCtrlPressed)
	{
		if (debugLeafNavMesh && g_app->pickMode == PICK_OBJECT && !g_app->isLoading) {
			debugLeafNavMesh->refreshNodes(map);
			g_app->debugInt++;
		}
	}

	if (debugLeafNavMesh) {
		if (g_app->pressed[GLFW_KEY_G] && !g_app->oldPressed[GLFW_KEY_G]) {
			g_app->debugInt -= 2;
			
			debugLeafNavMesh->refreshNodes(map);
			g_app->debugInt++;
		}

		if (g_app->pressed[GLFW_KEY_H] && !g_app->oldPressed[GLFW_KEY_H]) {
			g_app->debugInt = 269;
			debugLeafNavMesh->refreshNodes(map);
			g_app->debugInt++;
		}
	}
}

void NavRenderer::renderNavMesh(Bsp* map, vec3 cameraOrigin) {
	const bool navmeshwipcode = false;
	if (!navmeshwipcode)
		return;

	g_shaders.color->bind();
	g_shaders.color->modelMat->loadIdentity();
	g_shaders.color->updateMatrixes();
	glDisable(GL_CULL_FACE);

	glLineWidth(128.0f);
	drawLine(g_app->debugLine0, g_app->debugLine1, { 255, 0, 0, 255 });

	drawLine(g_app->debugTraceStart, g_app->debugTrace.vecEndPos, COLOR4(255, 0, 0, 255));

	if (g_app->debugNavMesh && g_app->debugNavPoly != -1) {
		glLineWidth(1);
		NavNode& node = g_app->debugNavMesh->nodes[g_app->debugNavPoly];
		Polygon3D& poly = g_app->debugNavMesh->polys[g_app->debugNavPoly];

		for (int i = 0; i < MAX_NAV_LINKS; i++) {
			NavLink& link = node.links[i];
			if (link.node == -1) {
				break;
			}
			Polygon3D& linkPoly = g_app->debugNavMesh->polys[link.node];

			vec3 srcMid, dstMid;
			g_app->debugNavMesh->getLinkMidPoints(g_app->debugNavPoly, i, srcMid, dstMid);

			glDisable(GL_DEPTH_TEST);
			drawLine(poly.center, srcMid, COLOR4(0, 255, 255, 255));
			drawLine(srcMid, dstMid, COLOR4(0, 255, 255, 255));
			drawLine(dstMid, linkPoly.center, COLOR4(0, 255, 255, 255));

			if (fabs(link.zDist) > NAV_STEP_HEIGHT) {
				int i = link.srcEdge;
				int k = link.dstEdge;
				int inext = (i + 1) % poly.verts.size();
				int knext = (k + 1) % linkPoly.verts.size();

				Line2D thisEdge(poly.topdownVerts[i], poly.topdownVerts[inext]);
				Line2D otherEdge(linkPoly.topdownVerts[k], linkPoly.topdownVerts[knext]);

				float t0, t1, t2, t3;
				float overlapDist = thisEdge.getOverlapRanges(otherEdge, t0, t1, t2, t3);

				vec3 delta1 = poly.verts[inext] - poly.verts[i];
				vec3 delta2 = linkPoly.verts[knext] - linkPoly.verts[k];
				vec3 e1 = poly.verts[i] + delta1 * t0;
				vec3 e2 = poly.verts[i] + delta1 * t1;
				vec3 e3 = linkPoly.verts[k] + delta2 * t2;
				vec3 e4 = linkPoly.verts[k] + delta2 * t3;

				bool isBelow = link.zDist > 0;
				delta1 = e2 - e1;
				delta2 = e4 - e3;
				vec3 mid1 = e1 + delta1 * 0.5f;
				vec3 mid2 = e3 + delta2 * 0.5f;
				vec3 inwardDir = crossProduct(poly.plane_z, delta1.normalize());
				vec3 testOffset = (isBelow ? inwardDir : inwardDir * -1) + vec3(0, 0, 1.0f);

				float flatLen = (e2.xy() - e1.xy()).length();
				float stepUnits = 1.0f;
				float step = stepUnits / flatLen;
				TraceResult tr;
				bool isBlocked = true;
				for (float f = 0; f < 0.5f; f += step) {
					vec3 test1 = mid1 + (delta1 * f) + testOffset;
					vec3 test2 = mid2 + (delta2 * f) + testOffset;
					vec3 test3 = mid1 + (delta1 * -f) + testOffset;
					vec3 test4 = mid2 + (delta2 * -f) + testOffset;

					map->traceHull(test1, test2, 3, &tr);
					if (!tr.fAllSolid && !tr.fStartSolid && tr.flFraction > 0.99f) {
						drawLine(test1, test2, COLOR4(255, 255, 0, 255));
					}
					else {
						drawLine(test1, test2, COLOR4(255, 0, 0, 255));
					}

					map->traceHull(test3, test4, 3, &tr);
					if (!tr.fAllSolid && !tr.fStartSolid && tr.flFraction > 0.99f) {
						drawLine(test3, test4, COLOR4(255, 255, 0, 255));
					}
					else {
						drawLine(test3, test4, COLOR4(255, 0, 0, 255));
					}
				}

				//if (isBlocked) {
				//	continue;
				//}
			}

			glEnable(GL_DEPTH_TEST);
			drawBox(linkPoly.center, 4, COLOR4(0, 255, 255, 255));
		}
	}

	if (!debugLeafNavMesh && !g_app->isLoading) {
		//LeafNavMesh* navMesh = LeafNavMeshGenerator().generate(map, false, CONTENTS_EMPTY, 3);
		LeafNavMesh* navMesh = LeafNavMeshGenerator().generate(map, true, CONTENTS_NOT_SOLID, 0);
		debugLeafNavMesh = navMesh;
	}

	if (debugLeafNavMesh && !g_app->isLoading) {
		glLineWidth(1);

		// split leaves dynamically by solid entities
		//debugLeafNavMesh->refreshNodes(map);

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);

		int leafNavIdx = debugLeafNavMesh->getNodeIdx(map, cameraOrigin);

		// draw split leaves
		for (int i = 0; i < debugLeafNavMesh->nodes.size(); i++) {
			LeafNode& node = debugLeafNavMesh->nodes[i];

			if (node.childIdx != NAV_INVALID_IDX) {
				continue;
			}

			if (!node.face_buffer) {
				g_app->mapRenderer->generateSingleLeafNavMeshBuffer(&node);

				if (!node.face_buffer) {
					continue;
				}
			}

			node.face_buffer->draw(GL_TRIANGLES);
		}

		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);

		if (leafNavIdx >= 0 && leafNavIdx < debugLeafNavMesh->nodes.size()) {

			if (g_app->pickInfo.getEnt() && g_app->pickInfo.getEntIndex() != 0) {
				glDisable(GL_DEPTH_TEST);

				int endNode = debugLeafNavMesh->getNodeIdx(map, g_app->pickInfo.getEnt());
				//vector<int> route = debugLeafNavMesh->AStarRoute(leafNavIdx, endNode);
				vector<int> route = debugLeafNavMesh->dijkstraRoute(leafNavIdx, endNode);

				if (route.size()) {
					LeafNode* lastNode = &debugLeafNavMesh->nodes[route[0]];

					vec3 lastPos = lastNode->origin;
					drawBox(lastNode->origin, 2, COLOR4(0, 255, 255, 255));

					for (int i = 1; i < route.size(); i++) {
						LeafNode& node = debugLeafNavMesh->nodes[route[i]];

						vec3 nodeCenter = node.origin;

						for (int k = 0; k < lastNode->links.size(); k++) {
							LeafLink& link = lastNode->links[k];

							if (link.node == route[i]) {
								vec3 linkPoint = link.pos;

								if (link.baseCost > 16000) {
									drawLine(lastPos, linkPoint, COLOR4(255, 0, 0, 255));
									drawLine(linkPoint, node.origin, COLOR4(255, 0, 0, 255));
								}
								else if (link.baseCost > 0) {
									drawLine(lastPos, linkPoint, COLOR4(255, 64, 0, 255));
									drawLine(linkPoint, node.origin, COLOR4(255, 64, 0, 255));
								}
								else if (link.costMultiplier > 99.0f) {
									drawLine(lastPos, linkPoint, COLOR4(255, 255, 0, 255));
									drawLine(linkPoint, node.origin, COLOR4(255, 255, 0, 255));
								}
								else if (link.costMultiplier > 9.0f) {
									drawLine(lastPos, linkPoint, COLOR4(255, 0, 255, 255));
									drawLine(linkPoint, node.origin, COLOR4(255, 0, 255, 255));
								}
								else if (link.costMultiplier > 1.9f) {
									drawLine(lastPos, linkPoint, COLOR4(64, 255, 0, 255));
									drawLine(linkPoint, node.origin, COLOR4(64, 255, 0, 255));
								}
								else {
									drawLine(lastPos, linkPoint, COLOR4(0, 255, 255, 255));
									drawLine(linkPoint, node.origin, COLOR4(0, 255, 255, 255));
								}
								drawBox(nodeCenter, 2, COLOR4(0, 255, 255, 255));
								lastPos = nodeCenter;
								break;
							}
						}

						lastNode = &node;
					}

					drawLine(lastPos, g_app->pickInfo.getEnt()->getHullOrigin(map), COLOR4(0, 255, 255, 255));
				}
			}
			else {
				LeafNode& node = debugLeafNavMesh->nodes[leafNavIdx];

				drawBox(node.origin, 2, COLOR4(0, 255, 0, 255));

				std::string linkStr;

				for (int i = 0; i < node.links.size(); i++) {
					LeafLink& link = node.links[i];
					if (link.node == -1) {
						break;
					}
					LeafNode& linkLeaf = debugLeafNavMesh->nodes[link.node];
					if (linkLeaf.childIdx != NAV_INVALID_IDX) {
						continue;
					}

					Polygon3D& linkArea = link.linkArea;

					if (link.baseCost > 16000) {
						drawLine(node.origin, link.pos, COLOR4(255, 0, 0, 255));
						drawLine(link.pos, linkLeaf.origin, COLOR4(255, 0, 0, 255));
					}
					else if (link.baseCost > 0) {
						drawLine(node.origin, link.pos, COLOR4(255, 128, 0, 255));
						drawLine(link.pos, linkLeaf.origin, COLOR4(255, 128, 0, 255));
					}
					else if (link.costMultiplier > 99.0f) {
						drawLine(node.origin, link.pos, COLOR4(255, 255, 0, 255));
						drawLine(link.pos, linkLeaf.origin, COLOR4(255, 255, 0, 255));
					}
					else if (link.costMultiplier > 9.0f) {
						drawLine(node.origin, link.pos, COLOR4(255, 0, 255, 255));
						drawLine(link.pos, linkLeaf.origin, COLOR4(255, 0, 255, 255));
					}
					else if (link.costMultiplier > 1.9f) {
						drawLine(node.origin, link.pos, COLOR4(64, 255, 0, 255));
						drawLine(link.pos, linkLeaf.origin, COLOR4(64, 255, 0, 255));
					}
					else {
						drawLine(node.origin, link.pos, COLOR4(0, 255, 255, 255));
						drawLine(link.pos, linkLeaf.origin, COLOR4(0, 255, 255, 255));
					}

					if (node.leafIdx == 208 && linkLeaf.leafIdx == 76) {
						for (int k = 0; k < linkArea.verts.size(); k++) {
							drawBox(linkArea.verts[k], 1, COLOR4(255, 255, 0, 255));
						}
					}
					drawBox(link.pos, 1, COLOR4(0, 255, 0, 255));
					drawBox(linkLeaf.origin, 2, COLOR4(0, 255, 255, 255));
					linkStr += to_string(link.node) + " (" + to_string(linkArea.verts.size()) + "v), ";

					/*
					for (int k = 0; k < node.links.size(); k++) {
						if (i == k)
							continue;
						drawLine(link.pos, node.links[k].pos, COLOR4(64, 0, 255, 255));
					}
					*/
				}

				//logf("Leaf node idx: %d, links: %s\n", leafNavIdx, linkStr.c_str());
			}

		}
		if (false) {
			// special case: touching on a single edge point
			//Polygon3D poly1({ vec3(213.996979, 202.000000, 362.000000), vec3(213.996979, 202.000000, 198.000824), vec3(213.996979, 105.996414, 198.000824), vec3(213.996979, 105.996414, 362.000000), });
			//Polygon3D poly2({ vec3(80.000969, -496.000000, 266.002014), vec3(310.000000, -496.000000, 266.002014), vec3(310.000000, 106.003876, 266.002014), vec3(80.000999, 106.003876, 266.002014), });

			Polygon3D poly1({ vec3(310.000000, 330.000000, 294.000000), vec3(213.996979, 330.000000, 294.000000), vec3(213.996979, 330.000000, 362.001282), vec3(310.000000, 330.000000, 362.001282), });
			Polygon3D poly2({ vec3(496.000000, -496.000000, 294.000000), vec3(496.000000, 431.998474, 294.000000), vec3(80.002045, 431.998474, 294.000000), vec3(80.002045, -496.000000, 294.000000), });

			vec3 start, end;
			poly1.planeIntersectionLine(poly2, start, end);

			vec3 ipos;
			COLOR4 c1 = poly1.intersect2D(start, end, ipos) ? COLOR4(255, 0, 0, 100) : COLOR4(0, 255, 255, 100);
			COLOR4 c2 = poly2.intersect2D(start, end, ipos) ? COLOR4(255, 0, 0, 100) : COLOR4(0, 255, 255, 100);
			COLOR4 c3 = poly1.intersects(poly2) ? COLOR4(255, 0, 0, 100) : COLOR4(0, 255, 255, 100);

			//drawPolygon3D(Polygon3D(poly1), c3);
			//drawPolygon3D(Polygon3D(poly2), c3);
			//drawLine(start, end, COLOR4(100, 0, 255, 255));

			//drawPolygon3D(g_app->debugPoly, COLOR4(255, 255, 255, 150));
		}

		if (g_app->debugPoly.isValid && g_app->debugPoly2.isValid) {
			g_shaders.color->bind();
			g_shaders.color->pushMatrix(MAT_PROJECTION);
			g_shaders.color->pushMatrix(MAT_VIEW);
			g_shaders.color->projMat->ortho(0, g_app->windowWidth, g_app->windowHeight, 0, -1.0f, 1.0f);
			g_shaders.color->viewMat->loadIdentity();
			g_shaders.color->updateMatrixes();

			vec2 maxSz = vec2(500, 500);
			vec2 sz = g_app->debugPoly.localMaxs - g_app->debugPoly.localMins;
			float scale = min(maxSz.y / sz.y, maxSz.x / sz.x);
			vec2 offset = g_app->debugPoly.localMins * -scale;
			vec2 pos = offset + vec2(700, 100);

			vector<vec2> projectedVerts;
			for (vec3& v : g_app->debugPoly2.verts) {
				projectedVerts.push_back(g_app->debugPoly.project(v));
			}

			drawPolygon2D(g_app->debugPoly.localVerts, pos, scale, COLOR4(255, 0, 0, 255));
			drawPolygon2D(projectedVerts, pos, scale, COLOR4(255, 0, 0, 255));

			g_app->debugPoly.coplanerIntersectArea(g_app->debugPoly2);

			for (int i = 0; i < g_app->debugVerts2d.size(); i++) {
				vec2 v = g_app->debugVerts2d[i];
				vec2 vpos = pos + v * scale;
				g_app->gui->addText(Text2D(vpos, cstrf("Vert %d: %d %d", i, (int)v.x, (int)v.y)));
				drawBox2D(vpos, 8, COLOR4(255, 255, 0, 255));
			}

			// draw camera origin in the same coordinate space
			vec2 cam = g_app->debugPoly.project(cameraOrigin);
			drawBox2D(pos + cam * scale, 16, g_app->debugPoly.isInside(cam) ? COLOR4(0, 255, 0, 255) : COLOR4(255, 32, 0, 255));

			g_shaders.color->popMatrix(MAT_PROJECTION);
			g_shaders.color->popMatrix(MAT_VIEW);
		}
	}

	if (g_app->pickInfo.getFace()) {
		BSPFACE& face = *g_app->pickInfo.getFace();
		BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];

		vector<vec3> faceVerts;
		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[0] : edge.iVertex[1];

			faceVerts.push_back(map->verts[vertIdx]);
		}

		Polygon3D poly(faceVerts);
		//vec3 center = poly.center + pickInfo.ent->getOrigin();
		vec3 center = poly.center - poly.plane_z;

		drawLine(center, center + info.vS * -10, { 128, 0, 255, 255 });
		drawLine(center, center + info.vT * -10, { 0, 255, 0, 255 });
		drawLine(center, center + poly.plane_z * -10, { 255, 255, 255, 255 });
	}

	glLineWidth(1);

	glCheckError("Rendering nav mesh");
}

void NavRenderer::renderLeafGraph(LeafNavMesh* mesh, vec3 cameraOrigin, Bsp* map) {
	if (g_app->isLoading || !mesh)
		return;

	int leafNavIdx = mesh->getNodeIdx(map, cameraOrigin);

	if (leafNavIdx == NAV_INVALID_IDX)
		return;

	LeafNode& node = mesh->nodes[leafNavIdx];

	g_shaders.color->bind();
	g_shaders.color->modelMat->loadIdentity();
	g_shaders.color->updateMatrixes();
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	drawBox(node.origin, 2, COLOR4(0, 255, 0, 255));

	{
		vec3 screenOri = g_app->worldToScreen(node.origin - vec3(0, 0, 1));
		if (screenOri.z > 0) {
			const char* label = cstrf("Leaf %d", (int)node.leafIdx);
			g_app->gui->addText(Text2D(screenOri.x, screenOri.y, label, TEXT2D_ALIGN_CENTER));
		}
	}

	for (int i = 0; i < node.links.size(); i++) {
		LeafLink& link = node.links[i];
		if (link.node == -1) {
			break;
		}
		LeafNode& linkLeaf = mesh->nodes[link.node];
		if (linkLeaf.childIdx != NAV_INVALID_IDX) {
			continue;
		}

		Polygon3D& linkArea = link.linkArea;

		drawLine(node.origin, link.pos, COLOR4(255, 128, 0, 255));
		drawLine(link.pos, linkLeaf.origin, COLOR4(255, 128, 0, 255));

		for (int i = 0; i < linkArea.verts.size(); i++) {
			vec3& v1 = linkArea.verts[i];
			vec3& v2 = linkArea.verts[(i + 1) % linkArea.verts.size()];

			drawLine(v1, v2, COLOR4(255, 255, 0, 255));
		}

		drawBox(link.pos, 1, COLOR4(255, 255, 0, 255));
		drawLine(link.pos, link.pos + linkArea.plane_z * 16, COLOR4(255, 255, 0, 255));
		drawBox(linkLeaf.origin, 2, COLOR4(0, 255, 0, 255));

		vec3 screenOri = g_app->worldToScreen(linkLeaf.origin - vec3(0, 0, 1));
		if (screenOri.z > 0) {
			const char* label = cstrf("Leaf %d", (int)linkLeaf.leafIdx);
			g_app->gui->addText(Text2D(screenOri.x, screenOri.y, label, TEXT2D_ALIGN_CENTER));
		}
	}
}
