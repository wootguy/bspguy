#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <cmath>
#include <mutex>
#include "ProgressMeter.h"
#include "bsptypes.h"
#include <string.h>

#define PRINT_BLUE		1
#define PRINT_GREEN		2
#define PRINT_RED		4
#define PRINT_BRIGHT	8

#define PI 3.141592f

#define EPSILON	(0.03125f) // 1/32 (to keep floating point happy -Carmack)

#define __WINDOWS__

#ifdef WIN32
#define strcasecmp _stricmp
#endif

void logf(const char* format, ...);

void debugf(const char* format, ...);

bool fileExists(const string& fileName);

char* loadFile(const string& fileName, int& length);

bool writeFile(const string& fileName, const char * data, int len);

bool removeFile(const string& fileName);

std::streampos fileSize(const string& filePath);

vector<string> splitString(string str, const char* delimitters);

string basename(string path);

string stripExt(string filename);

bool isNumeric(const std::string& s);

void print_color(int colors);

string getConfigDir();

bool dirExists(const string& dirName_in);

bool createDir(const string& dirName);

void removeDir(const string& dirName);

string toLowerCase(string str);

string trimSpaces(string s);

int getBspTextureSize(BSPMIPTEX* bspTexture);

float clamp(float val, float min, float max);

vec3 parseVector(string s);

bool pickAABB(vec3 start, vec3 rayDir, vec3 mins, vec3 maxs, float& bestDist);

bool rayPlaneIntersect(vec3 start, vec3 dir, vec3 normal, float fdist, float& intersectPoint);

float getDistAlongAxis(vec3 axis, vec3 p);

// returns false if verts are not planar
bool getPlaneFromVerts(vector<vec3>& verts, vec3& outNormal, float& outDist);

void getBoundingBox(vector<vec3>& verts, vec3& mins, vec3& maxs);

vec2 getCenter(vector<vec2>& verts);

vec3 getCenter(vector<vec3>& verts);

void expandBoundingBox(vec3 v, vec3& mins, vec3& maxs);

void expandBoundingBox(vec2 v, vec2& mins, vec2& maxs);

vector<vec3> getPlaneIntersectVerts(vector<BSPPLANE>& planes);

bool vertsAllOnOneSide(vector<vec3>& verts, BSPPLANE& plane);

bool boxesIntersect(const vec3& mins1, const vec3& maxs1, const vec3& mins2, const vec3& maxs2);

bool pointInBox(const vec3& p, const vec3& mins, const vec3& maxs);

bool isBoxContained(const vec3& innerMins, const vec3& innerMaxs, const vec3& outerMins, const vec3& outerMaxs);

// get verts from the given set that form a triangle (no duplicates and not colinear)
vector<vec3> getTriangularVerts(vector<vec3>& verts);

vec3 getNormalFromVerts(vector<vec3>& verts);

// transforms verts onto a plane (which is defined by the verts themselves)
vector<vec2> localizeVerts(vector<vec3>& verts);

// Returns CCW sorted indexes into the verts, as viewed on the plane the verts define
vector<int> getSortedPlanarVertOrder(vector<vec3>& verts);

void sortPlanarVerts(vector<vec3>& verts);

bool pointInsidePolygon(vector<vec2>& poly, vec2 p);

void replaceAll(std::string& str, const std::string& from, const std::string& to);

void sleepms(uint32_t ms);

void push_unique_vec2(vector<vec2>& verts, vec2 vert);

void push_unique_vec3(vector<vec3>& verts, vec3 vert);

vector<string> getAssetPaths();