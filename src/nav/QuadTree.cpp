#include "QuadTree.h"
#include "util.h"
#include <string.h>
#include <algorithm>

Quadrant::Quadrant(vec2 min, vec2 max) {
    this->min = min;
    this->max = max;
    memset(children, NULL, sizeof(Quadrant*) * 4);
}

Quadrant::~Quadrant() {
    for (Quadrant* child : children) {
        delete child;
    }
}

QuadTree::~QuadTree() {
    delete root;
}

QuadTree::QuadTree(const vec2& min, const vec2& max, int depth) {
    root = new Quadrant(min, max);
    maxDepth = depth;
    buildQuadTree(root, 0);
}

void QuadTree::buildQuadTree(Quadrant* node, int currentDepth) {
    if (currentDepth >= maxDepth) {
        return;
    }
    const vec2& min = node->min;
    const vec2& max = node->max;
    vec2 mid((min.x + max.x) / 2, (min.y + max.y) / 2);

    // Define eight child octants using the min and max values
    node->children[0] = new Quadrant(min, mid);
    node->children[1] = new Quadrant(vec2(mid.x, min.y), vec2(max.x, mid.y));
    node->children[2] = new Quadrant(vec2(min.x, mid.y), vec2(mid.x, max.y));
    node->children[3] = new Quadrant(mid, max);

    for (Quadrant* child : node->children) {
        buildQuadTree(child, currentDepth + 1);
    }
}

void QuadTree::insertObject(vec2 min, vec2 max, int id) {
    insertObject(root, min, max, id, 0);
}

void QuadTree::insertObject(Quadrant* node, vec2 min, vec2 max, int id, int currentDepth) {
    if (currentDepth >= maxDepth) {
        node->objects.push_back(id);
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (isInQuadrant(min, max, node->children[i])) {
            insertObject(node->children[i], min, max, id, currentDepth + 1);
        }
    }
}

bool QuadTree::isInQuadrant(vec2 min, vec2 max, Quadrant* node) {
    return boxesIntersect(min, max, node->min, node->max);
}

unordered_set<int> QuadTree::getObjectsInRegion(vec2 min, vec2 max) {
    unordered_set<int> regionPolys;
    getObjectsInRegion(root, min, max, 0, regionPolys);
    return regionPolys;
}

void QuadTree::getObjectsInRegion(Quadrant* node, vec2 min, vec2 max, int currentDepth, unordered_set<int>& regionPolys) {
    if (currentDepth >= maxDepth) {
        for (int i : node->objects) {
            regionPolys.insert(i);
        }
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (isInQuadrant(min, max, node->children[i])) {
            getObjectsInRegion(node->children[i], min, max, currentDepth + 1, regionPolys);
        }
    }
}
