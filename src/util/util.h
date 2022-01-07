#pragma once


#if defined(__cpp_lib_filesystem) || defined(USE_FILESYSTEM) || ((defined(__GNUC__) && (7 <= __GNUC_MAJOR__)))
#include <filesystem>
namespace fs = std::filesystem;
#define USE_FILESYSTEM
#elif _MSC_VER > 1920 || defined(USE_EXPERIMENTAL_FILESYSTEM)
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#define USE_FILESYSTEM
#endif


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

#ifndef WIN32
#define fopen_s(pFile,filename,mode) ((*(pFile))=fopen((filename),  (mode)))==NULL
#endif

#define PRINT_BLUE		1
#define PRINT_GREEN		2
#define PRINT_RED		4
#define PRINT_BRIGHT	8

#define PI 3.141592f

#define EPSILON	(0.03125f) // 1/32 (to keep floating point happy -Carmack)

//#define __WINDOWS__

extern bool g_verbose;
extern ProgressMeter g_progress;
extern std::vector<std::string> g_log_buffer;
extern char g_version_string[];
extern std::mutex g_log_mutex;

extern int g_render_flags;

void logf(const char* format, ...);

void debugf(const char* format, ...);

bool fileExists(const std::string& fileName);

char* loadFile(const std::string& fileName, int& length);

bool writeFile(const std::string& fileName, const char* data, int len);

bool removeFile(const std::string& fileName);

std::streampos fileSize(const std::string& filePath);

std::vector<std::string> splitString(const std::string & str, const char* delimitters);

std::string basename(std::string path);

std::string stripExt(std::string filename);

bool isNumeric(const std::string& s);

void print_color(int colors);

std::string getConfigDir();

bool dirExists(const std::string& dirName);

bool createDir(const std::string& dirName);

void removeDir(const std::string& dirName);

std::string toLowerCase(std::string str);

std::string trimSpaces(std::string s);

int getBspTextureSize(BSPMIPTEX* bspTexture);

float clamp(float val, float min, float max);

vec3 parseVector(std::string s);

bool IsEntNotSupportAngles(std::string& entname);

bool pickAABB(vec3 start, vec3 rayDir, vec3 mins, vec3 maxs, float& bestDist);

bool rayPlaneIntersect(vec3 start, vec3 dir, vec3 normal, float fdist, float& intersectDist);

float getDistAlongAxis(vec3 axis, vec3 p);

// returns false if verts are not planar
bool getPlaneFromVerts(std::vector<vec3>& verts, vec3& outNormal, float& outDist);

void getBoundingBox(std::vector<vec3>& verts, vec3& mins, vec3& maxs);

vec2 getCenter(std::vector<vec2>& verts);

vec3 getCenter(std::vector<vec3>& verts);

vec3 getCenter(vec3 maxs, vec3 mins);

void expandBoundingBox(vec3 v, vec3& mins, vec3& maxs);

void expandBoundingBox(vec2 v, vec2& mins, vec2& maxs);

std::vector<vec3> getPlaneIntersectVerts(std::vector<BSPPLANE>& planes);

bool vertsAllOnOneSide(std::vector<vec3>& verts, BSPPLANE& plane);

// get verts from the given set that form a triangle (no duplicates and not colinear)
std::vector<vec3> getTriangularVerts(std::vector<vec3>& verts);

vec3 getNormalFromVerts(std::vector<vec3>& verts);

// transforms verts onto a plane (which is defined by the verts themselves)
std::vector<vec2> localizeVerts(std::vector<vec3>& verts);

// Returns CCW sorted indexes into the verts, as viewed on the plane the verts define
std::vector<int> getSortedPlanarVertOrder(std::vector<vec3>& verts);

std::vector<vec3> getSortedPlanarVerts(std::vector<vec3>& verts);

bool pointInsidePolygon(std::vector<vec2>& poly, vec2 p);

enum class FIXUPPATH_SLASH
{
	FIXUPPATH_SLASH_CREATE,
	FIXUPPATH_SLASH_SKIP,
	FIXUPPATH_SLASH_REMOVE
};
void fixupPath(char* path, FIXUPPATH_SLASH startslash, FIXUPPATH_SLASH endslash);
void fixupPath(std::string& path, FIXUPPATH_SLASH startslash, FIXUPPATH_SLASH endslash);
void replaceAll(std::string& str, const std::string& from, const std::string& to);

void WriteBMP(std::string fileName, unsigned char* pixels, int width, int height, int bytesPerPixel);

std::string GetCurrentWorkingDir();