#pragma once
#include "Polygon3D.h"
#include <vector>
#include "LeafNavMesh.h"
#include <list>

struct LeafOctant {
    vec3 min;
    vec3 max;
    list<uint16_t> leaves;
    LeafOctant* children[8]; // Eight children octants

    LeafOctant(vec3 min, vec3 max);

    ~LeafOctant();
};

class LeafOctree {
public:
    LeafOctant* root;
    int maxDepth;

    LeafOctree(const vec3& min, const vec3& max, int depth);

    ~LeafOctree();

    void insertLeaf(LeafNode* leaf);

    void removeLeaf(LeafNode* leaf);

    bool isLeafInOctant(LeafNode* leaf, LeafOctant* node);

    void getLeavesInRegion(LeafNode* leaf, vector<bool>& regionLeaves);

    bool validate(int maxNodes);

    void shiftLeafIds(int shiftStart, int shiftAmount);

private:
    void buildOctree(LeafOctant* node, int currentDepth);

    void getLeavesInRegion(LeafOctant* node, LeafNode* leaf, int currentDepth, vector<bool>& regionLeaves);

    void insertLeaf(LeafOctant* node, LeafNode* leaf, int currentDepth);

    void removeLeaf(LeafOctant* node, LeafNode* leaf, int currentDepth);

    void shiftLeafIds(LeafOctant* node, int currentDepth, int shiftStart, int shiftAmount);

    bool validate(LeafOctant* node, int currentDepth, int maxNodes);
};