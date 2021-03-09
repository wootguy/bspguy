#pragma once
#include "types.h"
#include <string>
#include <vector>
#include "mat4x4.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <thread>
#include <future>
#include "ProgressMeter.h"
#include "bsptypes.h"

#define PRINT_BLUE		1
#define PRINT_GREEN		2
#define PRINT_RED		4
#define PRINT_BRIGHT	8

#define PI 3.141592f

#define EPSILON	(0.03125f) // 1/32 (to keep floating point happy -Carmack)

#define __WINDOWS__



extern bool g_verbose;
extern ProgressMeter g_progress;
extern vector<string> g_log_buffer;
extern const char* g_version_string;
extern mutex g_log_mutex;

extern int g_render_flags;

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

// get verts from the given set that form a triangle (no duplicates and not colinear)
vector<vec3> getTriangularVerts(vector<vec3>& verts);

vec3 getNormalFromVerts(vector<vec3>& verts);

// transforms verts onto a plane (which is defined by the verts themselves)
vector<vec2> localizeVerts(vector<vec3>& verts);

// Returns CCW sorted indexes into the verts, as viewed on the plane the verts define
vector<int> getSortedPlanarVertOrder(vector<vec3>& verts);

vector<vec3> getSortedPlanarVerts(vector<vec3>& verts);

bool pointInsidePolygon(vector<vec2>& poly, vec2 p);

enum class FIXUPPATH_SLASH
{
	FIXUPPATH_SLASH_CREATE,
	FIXUPPATH_SLASH_SKIP,
	FIXUPPATH_SLASH_REMOVE
};

void fixupPath(std::string& path, FIXUPPATH_SLASH needstartslash, FIXUPPATH_SLASH needendslash);
void replaceAll(std::string& str, const std::string& from, const std::string& to);

/// <summary> Writes an uncompressed 24 or 32 bit .tga image to the indicated file! </summary>
/// <param name='filename'>I'd recommended you add a '.tga' to the end of this filename.</param>
/// <param name='dataBGRA'>A chunk of color data, one channel per byte, ordered as BGRA. Size should be width*height*dataChanels.</param>
/// <param name='dataChannels'>The number of channels in the color data. Use 1 for grayscale, 3 for BGR, and 4 for BGRA.</param>
/// <param name='fileChannels'>The number of color channels to write to file. Must be 3 for BGR, or 4 for BGRA. Does NOT need to match dataChannels.</param>
void tga_write(const char* filename, uint32_t width, uint32_t height, uint8_t* data, uint8_t dataChannels = 4, uint8_t fileChannels = 3);