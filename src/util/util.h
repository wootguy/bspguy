#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <cmath>
#include <mutex>
#include "ProgressMeter.h"
#include "bsptypes.h"
#include <string.h>
#include "mat4x4.h"
#include "colors.h"
#include <stdint.h>
#include "Polygon3D.h"
#include "globals.h"

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

struct Frustum {
	vec3 origin;
	vec3 planes[4]; // right, left, top, bottom
};

struct FaceMath;

void errorf(const char* format, ...);

void warnf(const char* format, ...);

void logf(const char* format, ...);

void debugf(const char* format, ...);

char* strcpy_safe(char* dest, const char* src, size_t size);

char* strcat_safe(char* dest, const char* src, size_t size);

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

string toUpperCase(string str);

string trimSpaces(string s);

int getBspTextureSize(BSPMIPTEX* bspTexture);

float clamp(float val, float min, float max);

vec3 parseVector(string s);

COLOR3 parseColor(string s);

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
vector<vec3> getTriangularVerts(vec3* verts, int numVerts);

vec3 getNormalFromVerts(vec3* verts, int numVerts);

// transforms verts onto a plane (which is defined by the verts themselves)
vector<vec2> localizeVerts(vec3* verts, int numVerts);

// Returns CCW sorted indexes into the verts, as viewed on the plane the verts define
vector<int> getSortedPlanarVertOrder(vector<vec3>& verts);

bool sortPlanarVerts(vec3* verts, int numVerts);

bool pointInsidePolygon(vec2* verts, int numVerts, vec2 p);

void replaceAll(std::string& str, const std::string& from, const std::string& to);

void sleepms(uint32_t ms);

void push_unique_vec2(vector<vec2>& verts, vec2 vert);

void push_unique_vec3(vector<vec3>& verts, vec3 vert, float epsilon=0.125f);

vector<string> getAssetPaths(string assetPath);

vector<string> getAssetPaths();

// search all asset paths for a file
// returns empty string if not found
string findAsset(string asset);

// converts windows/linux slashes
void normalizePath(string& path);

// distance between a point and a plane
float planeDistance(vec3 planeNormal, float planeDist, vec3 point);

// intersection point of a line and a plane (unbound)
vec3 planeLineIntersect(vec3 planeNormal, float planeDist, vec3 a, vec3 b);

// returns distance from starting point or -1 on no intersect
float rayTriangleIntersect(const vec3& rayOrigin, const vec3& rayDir, const vec3& v0, const vec3& v1, const vec3& v2);

Frustum getViewFrustum(vec3 camOrigin, vec3 camAngles, float aspect, float zNear, float zFar, float fov);

// true if box is in the view frustum. vp is view-projection matrix
bool isBoxInView(vec3 min, vec3 max, const Frustum& frustum, float zMax);

bool isPointInView(vec3 p, const Frustum& frustum, float zMax);

bool isPolyInView(FaceMath* poly, const Frustum& frustum, vec3* srcVerts);

bool isPolyInView(Polygon3D poly, const Frustum& frustum);

// modulos a float value between start and end
float normalizeRangef(const float value, const float start, const float end);

string getAbsolutePath(const string& relpath);
bool isAbsolutePath(const std::string& path);
string joinPaths(string path1, string path2);
string getFolderPath(string path);
vec3 VecToAngles(const vec3& forward);
vec3 rotateAroundAxis(const vec3& v, const vec3& axis, float angle);
float signedAngle(const vec3& u, const vec3& v, const vec3& n);

string base64encode(const uint8_t* data, size_t len);

vector<uint8_t> base64decode(const string& input);

// return a c string created with printf formatting (not thread safe + uses static buffer)
char* cstrf(const char* format, ...);

void AngleVectors(const vec3& angles, float* forward, float* right, float* up);