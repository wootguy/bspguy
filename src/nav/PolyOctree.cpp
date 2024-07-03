#include "PolyOctree.h"
#include "util.h"
#include <string.h>
#include <algorithm>

PolyOctant::PolyOctant(vec3 min, vec3 max) {
    this->min = min;
    this->max = max;
    memset(children, NULL, sizeof(PolyOctant*) * 8);
}

PolyOctant::~PolyOctant() {
    for (PolyOctant* child : children) {
        delete child;
    }
}

void PolyOctant::removePolygon(Polygon3D* polygon) {
    polygons.erase(std::remove(polygons.begin(), polygons.end(), polygon), polygons.end());
    for (int i = 0; i < 8; i++) {
        if (children[i])
            children[i]->removePolygon(polygon);
    }
}

PolygonOctree::~PolygonOctree() {
    delete root;
}

PolygonOctree::PolygonOctree(const vec3& min, const vec3& max, int depth) {
    root = new PolyOctant(min, max);
    maxDepth = depth;
    buildOctree(root, 0);
}

void PolygonOctree::buildOctree(PolyOctant* node, int currentDepth) {
    if (currentDepth >= maxDepth) {
        return;
    }
    const vec3& min = node->min;
    const vec3& max = node->max;
    vec3 mid((min.x + max.x) / 2, (min.y + max.y) / 2, (min.z + max.z) / 2);

    // Define eight child octants using the min and max values
    node->children[0] = new PolyOctant(min, mid);
    node->children[1] = new PolyOctant(vec3(mid.x, min.y, min.z), vec3(max.x, mid.y, mid.z));
    node->children[2] = new PolyOctant(vec3(min.x, mid.y, min.z), vec3(mid.x, max.y, mid.z));
    node->children[3] = new PolyOctant(vec3(mid.x, mid.y, min.z), vec3(max.x, max.y, mid.z));
    node->children[4] = new PolyOctant(vec3(min.x, min.y, mid.z), vec3(mid.x, mid.y, max.z));
    node->children[5] = new PolyOctant(vec3(mid.x, min.y, mid.z), vec3(max.x, mid.y, max.z));
    node->children[6] = new PolyOctant(vec3(min.x, mid.y, mid.z), vec3(mid.x, max.y, max.z));
    node->children[7] = new PolyOctant(mid, max);

    for (PolyOctant* child : node->children) {
        buildOctree(child, currentDepth + 1);
    }
}

void PolygonOctree::insertPolygon(Polygon3D* polygon) {
    insertPolygon(root, polygon, 0);
}

void PolygonOctree::insertPolygon(PolyOctant* node, Polygon3D* polygon, int currentDepth) {
    if (currentDepth >= maxDepth) {
        node->polygons.push_back(polygon);
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (isPolygonInOctant(polygon, node->children[i])) {
            insertPolygon(node->children[i], polygon, currentDepth + 1);
        }
    }
}

void PolygonOctree::removePolygon(Polygon3D* polygon) {
    root->removePolygon(polygon);
}

bool PolygonOctree::isPolygonInOctant(Polygon3D* polygon, PolyOctant* node) {
    return boxesIntersect(polygon->worldMins, polygon->worldMaxs, node->min, node->max);
}

void PolygonOctree::getPolysInRegion(Polygon3D* poly, vector<bool>& regionPolys) {
    fill(regionPolys.begin(), regionPolys.end(), false);
    getPolysInRegion(root, poly, 0, regionPolys);
}

void PolygonOctree::getPolysInRegion(PolyOctant* node, Polygon3D* poly, int currentDepth, vector<bool>& regionPolys) {
    if (currentDepth >= maxDepth) {
        for (auto p : node->polygons) {
            if (p->idx != -1)
                regionPolys[p->idx] = true;
        }
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (isPolygonInOctant(poly, node->children[i])) {
            getPolysInRegion(node->children[i], poly, currentDepth + 1, regionPolys);
        }
    }
}
