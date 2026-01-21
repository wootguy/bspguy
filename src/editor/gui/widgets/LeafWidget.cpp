#include "Widget.h"

const float MINSEP = 12;
const float LEVEL_HEIGHT = 16;

typedef struct {
    int addr;  // index into nodes array
    int off;
    int lev;
} extreme;

// https://gitlab.com/-/snippets/2529055
void setup_tree(vector<GraphNode>& nodes, int t, int level, extreme* rmost, extreme* lmost) {
    if (t < 0) {
        lmost->lev = -1;
        rmost->lev = -1;
        return;
    }

    nodes[t].y = level * LEVEL_HEIGHT;

    const uint64_t badidx = (uint64_t)-1;
    uint64_t l = nodes[t].links[0];
    uint64_t r = nodes[t].links[1];

    extreme lr, ll, rr, rl;
    setup_tree(nodes, l, level + 1, &lr, &ll);
    setup_tree(nodes, r, level + 1, &rr, &rl);

    if (l == badidx && r == badidx) { // leaf
        rmost->addr = t;
        lmost->addr = t;
        rmost->lev = level;
        lmost->lev = level;
        rmost->off = 0;
        lmost->off = 0;
        nodes[t].offset = 0;
    }
    else {
        int cursep = MINSEP;
        int rootsep = MINSEP;
        int loffsum = 0, roffsum = 0;

        uint64_t lptr = l, rptr = r;

        while (lptr != badidx && rptr != badidx) {
            if (cursep < MINSEP) {
                rootsep += (MINSEP - cursep);
                cursep = MINSEP;
            }

            if (nodes[lptr].links[1] != badidx) {
                loffsum += nodes[lptr].offset;
                cursep -= nodes[lptr].offset;
                lptr = nodes[lptr].links[1];
            }
            else {
                loffsum -= nodes[lptr].offset;
                cursep += nodes[lptr].offset;
                lptr = nodes[lptr].links[0];
            }

            if (nodes[rptr].links[0] != badidx) {
                roffsum -= nodes[rptr].offset;
                cursep -= nodes[rptr].offset;
                rptr = nodes[rptr].links[0];
            }
            else {
                roffsum += nodes[rptr].offset;
                cursep += nodes[rptr].offset;
                rptr = nodes[rptr].links[1];
            }
        }

        nodes[t].offset = (rootsep + 1) / 2;
        loffsum -= nodes[t].offset;
        roffsum += nodes[t].offset;

        if ((rl.lev > ll.lev) || (nodes[t].links[0] == badidx)) {
            *lmost = rl;
            lmost->off += nodes[t].offset;
        }
        else {
            *lmost = ll;
            lmost->off -= nodes[t].offset;
        }

        if ((lr.lev > rr.lev) || (nodes[t].links[1] == badidx)) {
            *rmost = lr;
            rmost->off -= nodes[t].offset;
        }
        else {
            *rmost = rr;
            rmost->off += nodes[t].offset;
        }

        // Threading for uneven subtrees
        if (lptr != badidx && lptr != nodes[t].links[0]) {
            nodes[rr.addr].thread = true;
            nodes[rr.addr].offset = abs((rr.off + nodes[t].offset) - loffsum);
            if (loffsum - nodes[t].offset <= rr.off)
                nodes[rr.addr].links[0] = lptr;
            else
                nodes[rr.addr].links[1] = lptr;
        }
        else if (rptr != badidx && rptr != nodes[t].links[1]) {
            nodes[ll.addr].thread = true;
            nodes[ll.addr].offset = abs((ll.off - nodes[t].offset) - roffsum);
            if (roffsum + nodes[t].offset >= ll.off)
                nodes[ll.addr].links[1] = rptr;
            else
                nodes[ll.addr].links[0] = rptr;
        }
    }
}

void petrify(vector<GraphNode>& nodes, uint64_t t, int xpos) {
    if (t == (uint64_t)-1) return;

    nodes[t].x = xpos;

    if (nodes[t].thread) {
        nodes[t].thread = false;
        nodes[t].links[0] = -1;
        nodes[t].links[1] = -1;
        nodes[t].offset = 0;
    }
    else {
        petrify(nodes, nodes[t].links[0], xpos - nodes[t].offset);
        petrify(nodes, nodes[t].links[1], xpos + nodes[t].offset);
    }
}

void LeafWidget::refresh() {
    gnodes.clear();
    nodeIdxToGnode.clear();
    hotPath.clear();
    mergedLeaves.clear();

	int headNode = map->models[0].iHeadnodes[0];
	int maxDepth = map->get_leaf_graph(headNode, gnodes, 0, true);

    unordered_map<int, int> leafCounts;
    map->get_leaf_counts(headNode, leafCounts);
    leafCounts.erase(0); // don't care about the solid leaf

    for (auto item : leafCounts) {
        if (item.second > 1) {
            mergedLeaves.insert(item.first);
        }
    }

	int rootIdx = 0;
	for (int i = 0; i < gnodes.size(); i++) {
		GraphNode& node = gnodes[i];
		nodeIdxToGnode[node.nodeIdx] = i;

		if (node.nodeIdx == headNode)
			rootIdx = i;
	}
	for (int i = 0; i < gnodes.size(); i++) {
		GraphNode& node = gnodes[i];
		gnodes[i].x = 0;
		gnodes[i].y = 0;
		gnodes[i].offset = 0;
		gnodes[i].thread = false;

		for (int k = 0; k < 2; k++) {
			uint64_t linkIdx = node.links[k];
			if (linkIdx != (uint64_t)-1) {
				auto item = nodeIdxToGnode.find(linkIdx);
				if (item != nodeIdxToGnode.end())
					node.links[k] = nodeIdxToGnode[linkIdx];
				else
					node.links[k] = (uint64_t)-1;
			}
		}
	}

    extreme lm, rm;
    setup_tree(gnodes, rootIdx, 0, &lm, &rm);
    petrify(gnodes, rootIdx, 0);

	float minX = FLT_MAX;
    float maxX = -FLT_MAX;
	for (int i = 0; i < gnodes.size(); i++) {
		if (gnodes[i].x < minX) {
			minX = gnodes[i].x;
		}
        if (gnodes[i].x > maxX) {
            maxX = gnodes[i].x;
        }
        gnodes[i].srcX = gnodes[i].x;
        gnodes[i].srcY = gnodes[i].y;
	}
    graphWidth = maxX - minX;
    graphHeight = (maxDepth+2) * LEVEL_HEIGHT;

    for (int i = 0; i < gnodes.size(); i++) {
        gnodes[i].srcX += -minX;
    }
}

bool PointInBounds(ImVec2 p, ImVec2 min, ImVec2 max) {
    return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
}

bool LineIntersectsAABB(const ImVec2& p0, const ImVec2& p1,
    const ImVec2& boxMin, const ImVec2& boxMax)
{
    float tmin = 0.0f;
    float tmax = 1.0f;

    ImVec2 d = ImVec2(p1.x - p0.x, p1.y - p0.y); // line direction

    auto clip = [&](float p, float q) -> bool {
        if (p == 0.0f) return (q >= 0); // parallel line
        float t = q / p;
        if (p < 0) {
            if (t > tmax) return false;
            if (t > tmin) tmin = t;
        }
        else {
            if (t < tmin) return false;
            if (t < tmax) tmax = t;
        }
        return true;
        };

    if (clip(-d.x, p0.x - boxMin.x))
        if (clip(d.x, boxMax.x - p0.x))
            if (clip(-d.y, p0.y - boxMin.y))
                if (clip(d.y, boxMax.y - p0.y))
                    return tmin <= tmax;

    return false;
}

void LeafWidget::selectLeaves(vector<int>& leaves) {
    hotPath.clear();
    
    for (int leafIdx : leaves) {
        vector<int> branch;
        uint32_t parent = map->get_node_branch(map->models[0].iHeadnodes[0], branch, leafIdx);
        uint64_t gnodeId = ((uint64_t)(leafIdx + 1) << 32) | ((uint64_t)parent << 1);
        hotPath.insert(nodeIdxToGnode[gnodeId]);
        hotPath.insert(nodeIdxToGnode[gnodeId | 1]);
        for (int k = 1; k < branch.size(); k++) {
            int gidx = nodeIdxToGnode[branch[k]];
            hotPath.insert(gidx);
        }
    }
}

void LeafWidget::draw() {
    if (needsRefresh) {
        refresh();
        needsRefresh = false;
    }

    ImGui::BeginChild("GraphChild", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    ImVec2 clipMin = ImGui::GetWindowPos();
    ImVec2 clipMax = ImVec2(clipMin.x + ImGui::GetWindowWidth(), clipMin.y + ImGui::GetWindowHeight());

    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 origin = ImVec2(
        windowPos.x + contentMin.x,
        windowPos.y + contentMin.y + 15
    );

    for (int i = 0; i < gnodes.size(); i++) {
        gnodes[i].x = gnodes[i].srcX + origin.x;
        gnodes[i].y = gnodes[i].srcY + origin.y;
    }

    for (int i = 0; i < gnodes.size(); i++) {
        GraphNode& node = gnodes[i];
        ImVec2 start(node.x, node.y);

        for (int k = 0; k < 2; k++) {
            if (node.links[k] == -1)
                continue;

            GraphNode& child = gnodes[node.links[k]];
            ImVec2 dst = ImVec2(child.x, child.y);

            if (LineIntersectsAABB(start, dst, clipMin, clipMax)) {
                bool isHot = hotPath.count(node.links[k]);
                ImU32 color = isHot ? IM_COL32(255, 255, 0, 255) : IM_COL32(128, 64, 255, 255);
                dl->AddLine(start, dst, color, 1.0f);
            }
        }
    }

    for (int i = 0; i < gnodes.size(); i++) {
        GraphNode& gnode = gnodes[i];
        ImVec2 start(gnode.x, gnode.y);
        ImU32 color;

        float sz = max(1, 5 - gnode.depth) * 3;
        ImVec2 rectMin(start.x - sz, start.y - sz);
        ImVec2 rectMax(start.x + sz, start.y + sz);
        bool isHovering = ImGui::IsMouseHoveringRect(rectMin, rectMax);
        bool isHot = hotPath.count(i);
        bool highlight = isHot || isHovering;

        switch (gnode.nodeType) {
        default:
            color = !isHot ? IM_COL32(160, 160, 160, 255) : IM_COL32(255, 255, 255, 255);
            break;
        case -1:
            color = !isHot ? IM_COL32(160, 0, 0, 255) : IM_COL32(255, 0, 0, 255);
            break;
        case 1:
            color = !isHot ? IM_COL32(0, 160, 0, 255) : IM_COL32(0, 255, 0, 255);
            break;
        }


        if (gnode.nodeType != 0) {
            int leafIdx = (gnode.nodeIdx >> 32) - 1;
            if (mergedLeaves.count(leafIdx)) {
                color = !isHot ? IM_COL32(64, 160, 255, 255) : IM_COL32(96, 255, 255, 255);
            }
        }

        if (PointInBounds(start, clipMin, clipMax)) {
            dl->AddCircleFilled(start, sz, color);
        }

        if (isHovering) {
            ImGui::BeginTooltip();
            if (gnode.nodeType == 0) {
                BSPNODE& node = map->nodes[gnode.nodeIdx];
                vec3 sz = node.nMaxs - node.nMins;
                ImGui::TextUnformatted(cstrf("Node %d\nDepth: %d\nFaces: %d (offset %d)\nSize: %d %d %d",
                    gnode.nodeIdx, gnode.depth, node.nFaces, node.firstFace, (int)sz.x, (int)sz.y, (int)sz.z));
            }
            else {
                int leafIdx = (gnode.nodeIdx >> 32) - 1;
                BSPLEAF& leaf = map->leaves[leafIdx];
                ImGui::TextUnformatted(cstrf("Leaf %d\nDepth: %d\nFaces: %d", leafIdx, gnode.depth, leaf.nMarkSurfaces));
            }
            ImGui::EndTooltip();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                vector<int> leaves;

                if (gnode.nodeType == 0) {
                    BSPNODE& node = map->nodes[gnode.nodeIdx];
                    map->get_child_leaves(gnode.nodeIdx, leaves);
                }
                else {
                    int leafIdx = (gnode.nodeIdx >> 32) - 1;
                    leaves.push_back(leafIdx);
                }

                selectLeaves(leaves);

                app->mapRenderer->highlightPickedFaces(false);
                app->mapRenderer->highlightPickedLeaves(false);
                app->pickInfo.deselect();

                for (int idx : leaves) {
                    app->pickInfo.selectLeaf(idx);
                }

                app->pickInfo.selectLeafFaces();
                app->mapRenderer->highlightPickedFaces(true);
                app->mapRenderer->highlightPickedLeaves(true);
                app->updateTextureAxes();
            }
        }
    }
    
    ImGui::Dummy(ImVec2(graphWidth, graphHeight));
    ImGui::EndChild();
}