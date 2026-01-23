#pragma once
#include "Polygon3D.h"
#include <vector>
#include <unordered_set>

struct Quadrant {
    vec2 min;
    vec2 max;
    vector<int> objects;
    Quadrant* children[4];

    Quadrant(vec2 min, vec2 max);

    ~Quadrant();
};

class QuadTree {
public:
    Quadrant* root;
    int maxDepth;

    QuadTree(const vec2& min, const vec2& max, int depth);

    ~QuadTree();

    void insertObject(vec2 min, vec2 max, int id);

    bool isInQuadrant(vec2 min, vec2 max, Quadrant* node);

    unordered_set<int> getObjectsInRegion(vec2 min, vec2 max);

private:
    void buildQuadTree(Quadrant* node, int currentDepth);

    void getObjectsInRegion(Quadrant* node, vec2 min, vec2 max, int currentDepth, unordered_set<int>& regionPolys);

    void insertObject(Quadrant* node, vec2 min, vec2 max, int id, int currentDepth);
};