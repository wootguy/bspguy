#include "LeafOctree.h"
#include "util.h"
#include <string.h>
#include <algorithm>

LeafOctant::LeafOctant(vec3 min, vec3 max) {
    this->min = min;
    this->max = max;
    memset(children, NULL, sizeof(LeafOctant*) * 8);
}

LeafOctant::~LeafOctant() {
    for (LeafOctant* child : children) {
        delete child;
    }
}

void LeafOctant::removeLeaf(LeafMesh* leaf) {
    leaves.erase(std::remove(leaves.begin(), leaves.end(), leaf), leaves.end());
    for (int i = 0; i < 8; i++) {
        if (children[i])
            children[i]->removeLeaf(leaf);
    }
}

LeafOctree::~LeafOctree() {
    delete root;
}

LeafOctree::LeafOctree(const vec3& min, const vec3& max, int depth) {
    root = new LeafOctant(min, max);
    maxDepth = depth;
    buildOctree(root, 0);
}

void LeafOctree::buildOctree(LeafOctant* node, int currentDepth) {
    if (currentDepth >= maxDepth) {
        return;
    }
    const vec3& min = node->min;
    const vec3& max = node->max;
    vec3 mid((min.x + max.x) / 2, (min.y + max.y) / 2, (min.z + max.z) / 2);

    // Define eight child octants using the min and max values
    node->children[0] = new LeafOctant(min, mid);
    node->children[1] = new LeafOctant(vec3(mid.x, min.y, min.z), vec3(max.x, mid.y, mid.z));
    node->children[2] = new LeafOctant(vec3(min.x, mid.y, min.z), vec3(mid.x, max.y, mid.z));
    node->children[3] = new LeafOctant(vec3(mid.x, mid.y, min.z), vec3(max.x, max.y, mid.z));
    node->children[4] = new LeafOctant(vec3(min.x, min.y, mid.z), vec3(mid.x, mid.y, max.z));
    node->children[5] = new LeafOctant(vec3(mid.x, min.y, mid.z), vec3(max.x, mid.y, max.z));
    node->children[6] = new LeafOctant(vec3(min.x, mid.y, mid.z), vec3(mid.x, max.y, max.z));
    node->children[7] = new LeafOctant(mid, max);

    for (LeafOctant* child : node->children) {
        buildOctree(child, currentDepth + 1);
    }
}

void LeafOctree::insertLeaf(LeafMesh* leaf) {
    insertLeaf(root, leaf, 0);
}

void LeafOctree::insertLeaf(LeafOctant* node, LeafMesh* leaf, int currentDepth) {
    if (currentDepth >= maxDepth) {
        node->leaves.push_back(leaf);
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (isLeafInOctant(leaf, node->children[i])) {
            insertLeaf(node->children[i], leaf, currentDepth + 1);
        }
    }
}

void LeafOctree::removeLeaf(LeafMesh* leaf) {
    root->removeLeaf(leaf);
}

bool LeafOctree::isLeafInOctant(LeafMesh* leaf, LeafOctant* node) {
    return boxesIntersect(leaf->mins, leaf->maxs, node->min, node->max);
}

void LeafOctree::getLeavesInRegion(LeafMesh* leaf, vector<bool>& regionLeaves) {
    fill(regionLeaves.begin(), regionLeaves.end(), false);
    getLeavesInRegion(root, leaf, 0, regionLeaves);
}

void LeafOctree::getLeavesInRegion(LeafOctant* node, LeafMesh* leaf, int currentDepth, vector<bool>& regionLeaves) {
    if (currentDepth >= maxDepth) {
        for (auto p : node->leaves) {
            if (p->idx != -1)
                regionLeaves[p->idx] = true;
        }
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (isLeafInOctant(leaf, node->children[i])) {
            getLeavesInRegion(node->children[i], leaf, currentDepth + 1, regionLeaves);
        }
    }
}
