#pragma once
#include "types.h"
#include <string>
#include <vector>
#include "mat4x4.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include "ProgressMeter.h"

#define PRINT_BLUE		1
#define PRINT_GREEN		2
#define PRINT_RED		4
#define PRINT_BRIGHT	8

#define PI 3.141592f

#define EPSILON	(0.03125f) // 1/32 (to keep floating point happy -Carmack)

#define __WINDOWS__

extern bool g_verbose;
extern ProgressMeter g_progress;
extern string g_game_path;

extern int g_render_flags;

class BSPMIPTEX;

bool fileExists(const string& fileName);

char * loadFile( const string& fileName, int& length);

vector<string> splitString(string str, const char* delimitters);

string basename(string path);

string stripExt(string filename);

bool isNumeric(const std::string& s);

void print_color(int colors);

string toLowerCase(string str);

string trimSpaces(string s);

int getBspTextureSize(BSPMIPTEX* bspTexture);

float clamp(float val, float min, float max);

vec3 parseVector(string s);

bool pickAABB(vec3 start, vec3 rayDir, vec3 mins, vec3 maxs, float& bestDist);

bool rayPlaneIntersect(vec3 start, vec3 dir, vec3 normal, float fdist, float& intersectPoint);
