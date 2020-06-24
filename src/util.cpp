#include "util.h"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <string.h>
#include "Wad.h"
#include <stdarg.h>

ProgressMeter g_progress;
string g_game_path;
int g_render_flags;
vector<string> g_log_buffer;

void logf(const char* format, ...) {
	static char line[4096];

	va_list vl;
	va_start(vl, format);
	vsnprintf(line, 4096, format, vl);
	va_end(vl);

	printf(line);
	g_log_buffer.push_back(line);
}

bool fileExists(const string& fileName)
{
	if (FILE *file = fopen(fileName.c_str(), "r"))
	{
		fclose(file);
		return true;
	}
	return false; 
}

char * loadFile( const string& fileName, int& length)
{
	if (!fileExists(fileName))
		return NULL;
	ifstream fin(fileName.c_str(), ifstream::in|ios::binary);
	long long begin = fin.tellg();
	fin.seekg (0, ios::end);
	uint size = (uint)((int)fin.tellg() - begin);
	char * buffer = new char[size];
	fin.seekg(0);
	fin.read(buffer, size);
	fin.close();
	length = (int)size; // surely models will never exceed 2 GB
	return buffer;
}

vector<string> splitString(string str, const char* delimitters)
{
	vector<string> split;
	if (str.size() == 0)
		return split;

	// somehow plain assignment doesn't create a copy and even modifies the parameter that was passed by value (WTF!?!)
	//string copy = str; 
	string copy;
	for (int i = 0; i < str.length(); i++)
		copy += str[i];

	char* tok = strtok((char*)copy.c_str(), delimitters);

	while (tok != NULL)
	{
		split.push_back(tok);
		tok = strtok(NULL, delimitters);
	}
	return split;
}

string basename(string path) {

	int lastSlash = path.find_last_of("\\/");
	if (lastSlash != string::npos) {
		return path.substr(lastSlash + 1);
	}
	return path;
}

string stripExt(string path) {
	int lastDot = path.find_last_of(".");
	if (lastDot != string::npos) {
		return path.substr(0, lastDot);
	}
	return path;
}

bool isNumeric(const std::string& s)
{
	std::string::const_iterator it = s.begin();

	while (it != s.end() && isdigit(*it))
		++it;

	return !s.empty() && it == s.end();
}

string toLowerCase(string str)
{
	transform(str.begin(), str.end(), str.begin(), ::tolower);
	return str;
}

string trimSpaces(string s)
{
	// Remove white space indents
	int lineStart = s.find_first_not_of(" \t\n\r");
	if (lineStart == string::npos)
		return "";

	// Remove spaces after the last character
	int lineEnd = s.find_last_not_of(" \t\n\r");
	if (lineEnd != string::npos && lineEnd < s.length() - 1)
		s = s.substr(lineStart, (lineEnd + 1) - lineStart);
	else
		s = s.substr(lineStart);

	return s;
}

int getBspTextureSize(BSPMIPTEX* bspTexture) {
	int sz = sizeof(BSPMIPTEX);
	if (bspTexture->nOffsets[0] != 0) {
		sz += 256 * 3 + 4; // pallette + padding

		for (int i = 0; i < MIPLEVELS; i++) {
			sz += (bspTexture->nWidth >> i)* (bspTexture->nHeight >> i);
		}
	}
	return sz;
}

float clamp(float val, float min, float max) {
	if (val > max) {
		return max;
	}
	else if (val < min) {
		return min;
	}
	return val;
}

vec3 parseVector(string s) {
	vec3 v;
	vector<string> parts = splitString(s, " ");

	if (parts.size() != 3) {
		logf("Not enough coordinates in vector %s\n", s.c_str());
		return v;
	}

	v.x = atof(parts[0].c_str());
	v.y = atof(parts[1].c_str());
	v.z = atof(parts[2].c_str());

	return v;
}

COLOR3 operator*(COLOR3 c, float scale)
{
	c.r *= scale;
	c.g *= scale;
	c.b *= scale;
	return c;
}

bool pickAABB(vec3 start, vec3 rayDir, vec3 mins, vec3 maxs, float& bestDist) {
	bool foundBetterPick = false;

	/*
	Fast Ray-Box Intersection
	by Andrew Woo
	from "Graphics Gems", Academic Press, 1990
	https://web.archive.org/web/20090803054252/http://tog.acm.org/resources/GraphicsGems/gems/RayBox.c
	*/

	bool inside = true;
	char quadrant[3];
	register int i;
	int whichPlane;
	double maxT[3];
	double candidatePlane[3];

	float* origin = (float*)&start;
	float* dir = (float*)&rayDir;
	float* minB = (float*)&mins;
	float* maxB = (float*)&maxs;
	float coord[3];

	const char RIGHT = 0;
	const char LEFT = 1;
	const char MIDDLE = 2;

	/* Find candidate planes; this loop can be avoided if
	rays cast all from the eye(assume perpsective view) */
	for (i = 0; i < 3; i++) {
		if (origin[i] < minB[i]) {
			quadrant[i] = LEFT;
			candidatePlane[i] = minB[i];
			inside = false;
		}
		else if (origin[i] > maxB[i]) {
			quadrant[i] = RIGHT;
			candidatePlane[i] = maxB[i];
			inside = false;
		}
		else {
			quadrant[i] = MIDDLE;
		}
	}

	/* Ray origin inside bounding box */
	if (inside) {
		return false;
	}

	/* Calculate T distances to candidate planes */
	for (i = 0; i < 3; i++) {
		if (quadrant[i] != MIDDLE && dir[i] != 0.0f)
			maxT[i] = (candidatePlane[i] - origin[i]) / dir[i];
		else
			maxT[i] = -1.0f;
	}

	/* Get largest of the maxT's for final choice of intersection */
	whichPlane = 0;
	for (i = 1; i < 3; i++) {
		if (maxT[whichPlane] < maxT[i])
			whichPlane = i;
	}

	/* Check final candidate actually inside box */
	if (maxT[whichPlane] < 0.0f)
		return false;
	for (i = 0; i < 3; i++) {
		if (whichPlane != i) {
			coord[i] = origin[i] + maxT[whichPlane] * dir[i];
			if (coord[i] < minB[i] || coord[i] > maxB[i])
				return false;
		}
		else {
			coord[i] = candidatePlane[i];
		}
	}
	/* ray hits box */

	vec3 intersectPoint(coord[0], coord[1], coord[2]);
	float dist = (intersectPoint - start).length();

	if (dist < bestDist) {
		bestDist = dist;
		return true;
	}

	return false;
}

bool rayPlaneIntersect(vec3 start, vec3 dir, vec3 normal, float fdist, float& intersectDist) {
	float dot = dotProduct(dir, normal);

	// don't select backfaces or parallel faces
	if (dot == 0) {
		return false;
	}
	intersectDist = dotProduct((normal * fdist) - start, normal) / dot;

	if (intersectDist < 0) {
		return false; // intersection behind ray
	}

	return true;
}

float getDistAlongAxis(vec3 axis, vec3 p)
{
	return dotProduct(axis, p) / sqrt(dotProduct(axis, axis));
}

bool getPlaneFromVerts(vector<vec3>& verts, vec3& outNormal, float& outDist) {
	const float tolerance = 0.00001f; // normals more different than this = non-planar face

	int numVerts = verts.size();
	for (int i = 0; i < numVerts; i++) {
		vec3 v0 = verts[(i + 0) % numVerts];
		vec3 v1 = verts[(i + 1) % numVerts];
		vec3 v2 = verts[(i + 2) % numVerts];

		vec3 ba = v1 - v0;
		vec3 cb = v2 - v1;

		vec3 normal = crossProduct(ba, cb).normalize(1.0f);

		if (i == 0) {
			outNormal = normal;
		}
		else {
			float dot = dotProduct(outNormal, normal);
			if (fabs(dot) < 1.0f - tolerance) {
				//logf("DOT %f", dot);
				return false; // non-planar face
			}
		}
	}

	outDist = getDistAlongAxis(outNormal, verts[0]);
	return true;
}

vec2 getCenter(vector<vec2>& verts) {
	vec2 maxs = vec2(-9e99, -9e99);
	vec2 mins = vec2(9e99, 9e99);

	for (int i = 0; i < verts.size(); i++) {
		expandBoundingBox(verts[i], mins, maxs);
	}

	return mins + (maxs - mins) * 0.5f;
}

void getBoundingBox(vector<vec3>& verts, vec3& mins, vec3& maxs) {
	maxs = vec3(-9e99, -9e99, -9e99);
	mins = vec3(9e99, 9e99, 9e99);

	for (int i = 0; i < verts.size(); i++) {
		expandBoundingBox(verts[i], mins, maxs);
	}
}

void expandBoundingBox(vec3 v, vec3& mins, vec3& maxs) {
	if (v.x > maxs.x) maxs.x = v.x;
	if (v.y > maxs.y) maxs.y = v.y;
	if (v.z > maxs.z) maxs.z = v.z;

	if (v.x < mins.x) mins.x = v.x;
	if (v.y < mins.y) mins.y = v.y;
	if (v.z < mins.z) mins.z = v.z;
}

void expandBoundingBox(vec2 v, vec2& mins, vec2& maxs) {
	if (v.x > maxs.x) maxs.x = v.x;
	if (v.y > maxs.y) maxs.y = v.y;

	if (v.x < mins.x) mins.x = v.x;
	if (v.y < mins.y) mins.y = v.y;
}

#ifdef WIN32
#include <Windows.h>
void print_color(int colors)
{
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	colors = colors ? colors : (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
	SetConsoleTextAttribute(console, (WORD)colors);
}
#else
void print_color(int colors)
{
	if (!colors)
	{
		logf("\x1B[0m");
		return;
	}
	const char* mode = colors & PRINT_BRIGHT ? "1" : "0";
	const char* color = "37";
	switch (colors & ~PRINT_BRIGHT)
	{
	case PRINT_RED:								color = "31"; break;
	case PRINT_GREEN:							color = "32"; break;
	case PRINT_RED | PRINT_GREEN:				color = "33"; break;
	case PRINT_BLUE:							color = "34"; break;
	case PRINT_RED | PRINT_BLUE:				color = "35"; break;
	case PRINT_GREEN | PRINT_BLUE:				color = "36"; break;
	case PRINT_GREEN | PRINT_BLUE | PRINT_RED:	color = "36"; break;
	}
	logf("\x1B[%s;%sm", mode, color);
}
#endif
