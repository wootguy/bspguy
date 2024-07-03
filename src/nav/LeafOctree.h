#pragma once
#include "Polygon3D.h"
#include <vector>
#include "LeafNavMesh.h"

struct LeafOctant {
    vec3 min;
    vec3 max;
    vector<LeafMesh*> leaves;
    LeafOctant* children[8]; // Eight children octants

    LeafOctant(vec3 min, vec3 max);

    ~LeafOctant();

    void removeLeaf(LeafMesh* polygon);
};

class LeafOctree {
public:
    LeafOctant* root;
    int maxDepth;

    LeafOctree(const vec3& min, const vec3& max, int depth);

    ~LeafOctree();

    void insertLeaf(LeafMesh* leaf);

    void removeLeaf(LeafMesh* leaf);

    bool isLeafInOctant(LeafMesh* leaf, LeafOctant* node);

    void getLeavesInRegion(LeafMesh* leaf, vector<bool>& regionLeaves);

private:
    void buildOctree(LeafOctant* node, int currentDepth);

    void getLeavesInRegion(LeafOctant* node, LeafMesh* leaf, int currentDepth, vector<bool>& regionLeaves);

    void insertLeaf(LeafOctant* node, LeafMesh* leaf, int currentDepth);
};