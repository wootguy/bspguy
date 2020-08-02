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
int g_render_flags;
vector<string> g_log_buffer;
mutex g_log_mutex;

void logf(const char* format, ...) {
	static char line[4096];

	g_log_mutex.lock();

	va_list vl;
	va_start(vl, format);
	vsnprintf(line, 4096, format, vl);
	va_end(vl);

	printf(line);
	g_log_buffer.push_back(line);

	g_log_mutex.unlock();
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

bool operator==(COLOR3 c1, COLOR3 c2) {
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b;
}

COLOR4 operator*(COLOR4 c, float scale)
{
	c.r *= scale;
	c.g *= scale;
	c.b *= scale;
	return c;
}

bool operator==(COLOR4 c1, COLOR4 c2) {
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b && c1.a == c2.a;
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

vec3 getCenter(vector<vec3>& verts) {
	vec3 maxs = vec3(-9e99, -9e99, -9e99);
	vec3 mins = vec3(9e99, 9e99, 9e99);

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

vector<vec3> getPlaneIntersectVerts(vector<BSPPLANE>& planes) {
	vector<vec3> intersectVerts;

	// https://math.stackexchange.com/questions/1883835/get-list-of-vertices-from-list-of-planes
	int numPlanes = planes.size();
	for (int i = 0; i < numPlanes - 2; i++) {
		for (int j = i + 1; j < numPlanes - 1; j++) {
			for (int k = j + 1; k < numPlanes; k++) {
				vec3& n0 = planes[i].vNormal;
				vec3& n1 = planes[j].vNormal;
				vec3& n2 = planes[k].vNormal;
				float d0 = planes[i].fDist;
				float d1 = planes[j].fDist;
				float d2 = planes[k].fDist;

				float t = n0.x * (n1.y * n2.z - n1.z * n2.y) +
					n0.y * (n1.z * n2.x - n1.x * n2.z) +
					n0.z * (n1.x * n2.y - n1.y * n2.x);

				if (fabs(t) < EPSILON) {
					continue;
				}

				// don't use crossProduct because it's less accurate
				//vec3 v = crossProduct(n1, n2)*d0 + crossProduct(n0, n2)*d1 + crossProduct(n0, n1)*d2;
				vec3 v(
					(d0 * (n1.z * n2.y - n1.y * n2.z) + d1 * (n0.y * n2.z - n0.z * n2.y) + d2 * (n0.z * n1.y - n0.y * n1.z)) / -t,
					(d0 * (n1.x * n2.z - n1.z * n2.x) + d1 * (n0.z * n2.x - n0.x * n2.z) + d2 * (n0.x * n1.z - n0.z * n1.x)) / -t,
					(d0 * (n1.y * n2.x - n1.x * n2.y) + d1 * (n0.x * n2.y - n0.y * n2.x) + d2 * (n0.y * n1.x - n0.x * n1.y)) / -t
				);

				bool validVertex = true;

				for (int m = 0; m < numPlanes; m++) {
					BSPPLANE& pm = planes[m];
					if (m != i && m != j && m != k && dotProduct(v, pm.vNormal) < pm.fDist + EPSILON) {
						validVertex = false;
						break;
					}
				}

				if (validVertex) {
					intersectVerts.push_back(v);
				}
			}
		}
	}

	return intersectVerts;
}

bool vertsAllOnOneSide(vector<vec3>& verts, BSPPLANE& plane) {
	// check that all verts are on one side of the plane.
	int planeSide = 0;
	for (int k = 0; k < verts.size(); k++) {
		float d = dotProduct(verts[k], plane.vNormal) - plane.fDist;
		if (d < -EPSILON) {
			if (planeSide == 1) {
				return false;
			}
			planeSide = -1;
		}
		if (d > EPSILON) {
			if (planeSide == -1) {
				return false;
			}
			planeSide = 1;
		}
	}

	return true;
}

vector<vec3> getTriangularVerts(vector<vec3>& verts) {
	int i0 = 0;
	int i1 = -1;
	int i2 = -1;

	int count = 1;
	for (int i = 1; i < verts.size() && count < 3; i++) {
		if (verts[i] != verts[i0]) {
			i1 = i;
			count++;
			break;
		}
	}

	if (i1 == -1) {
		//logf("Only 1 unique vert!\n");
		return vector<vec3>();
	}

	for (int i = 1; i < verts.size(); i++) {
		if (i == i1)
			continue;

		if (verts[i] != verts[i0] && verts[i] != verts[i1]) {
			vec3 ab = (verts[i1] - verts[i0]).normalize();
			vec3 ac = (verts[i] - verts[i0]).normalize();
			if (fabs(dotProduct(ab, ac)) == 1) {
				continue;
			}

			i2 = i;
			break;
		}
	}

	if (i2 == -1) {
		//logf("All verts are colinear!\n");
		return vector<vec3>();
	}

	return { verts[i0], verts[i1], verts[i2] };
}

vec3 getNormalFromVerts(vector<vec3>& verts) {
	vector<vec3> triangularVerts = getTriangularVerts(verts);

	if (triangularVerts.empty())
		return vec3();

	vec3 e1 = (triangularVerts[1] - triangularVerts[0]).normalize();
	vec3 e2 = (triangularVerts[2] - triangularVerts[0]).normalize();
	vec3 vertsNormal = crossProduct(e1, e2).normalize();

	return vertsNormal;
}

vector<vec2> localizeVerts(vector<vec3>& verts) {
	vec3 plane_z = getNormalFromVerts(verts);

	if (plane_z == vec3()) {
		return vector<vec2>();
	}

	vec3 plane_x = (verts[1] - verts[0]).normalize();
	vec3 plane_y = crossProduct(plane_z, plane_x).normalize();

	if (fabs(dotProduct(plane_z, plane_x)) > 0.99f) {
		logf("ZOMG CHANGE NORMAL\n");
	}

	mat4x4 worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

	vector<vec2> localVerts(verts.size());
	for (int e = 0; e < verts.size(); e++) {
		localVerts[e] = (worldToLocal * vec4(verts[e], 1)).xy();
	}

	return localVerts;
}

vector<int> getSortedPlanarVertOrder(vector<vec3>& verts) {
	vector<vec2> localVerts = localizeVerts(verts);
	if (localVerts.empty()) {
		return vector<int>();
	}

	vec2 center = getCenter(localVerts);
	vector<int> orderedVerts;
	vector<int> remainingVerts;

	for (int i = 0; i < localVerts.size(); i++) {
		remainingVerts.push_back(i);
	}

	orderedVerts.push_back(remainingVerts[0]);
	vec2 lastVert = localVerts[0];
	remainingVerts.erase(remainingVerts.begin() + 0);
	localVerts.erase(localVerts.begin() + 0);
	for (int k = 0, sz = remainingVerts.size(); k < sz; k++) {
		int bestIdx = 0;
		float bestAngle = 9e99;

		for (int i = 0; i < remainingVerts.size(); i++) {
			vec2 a = lastVert;
			vec2 b = localVerts[i];
			double a1 = atan2(a.x - center.x, a.y - center.y);
			double a2 = atan2(b.x - center.x, b.y - center.y);
			float angle = a2 - a1;
			if (angle < 0)
				angle += PI * 2;

			if (angle < bestAngle) {
				bestAngle = angle;
				bestIdx = i;
			}
		}

		lastVert = localVerts[bestIdx];
		orderedVerts.push_back(remainingVerts[bestIdx]);
		remainingVerts.erase(remainingVerts.begin() + bestIdx);
		localVerts.erase(localVerts.begin() + bestIdx);
	}

	return orderedVerts;
}

vector<vec3> getSortedPlanarVerts(vector<vec3>& verts) {
	vector<vec3> outVerts(verts.size());
	vector<int> vertOrder = getSortedPlanarVertOrder(verts);
	if (vertOrder.empty()) {
		return vector<vec3>();
	}
	for (int i = 0; i < vertOrder.size(); i++) {
		outVerts[i] = verts[vertOrder[i]];
	}
	return outVerts;
}

bool pointInsidePolygon(vector<vec2>& poly, vec2 p) {
	// https://stackoverflow.com/a/34689268
	bool inside = true;
	float lastd = 0;
	for (int i = 0; i < poly.size(); i++)
	{
		vec2& v1 = poly[i];
		vec2& v2 = poly[(i + 1) % poly.size()];

		if (v1.x == p.x && v1.y == p.y) {
			break; // on edge = inside
		}

		float d = (p.x - v1.x) * (v2.y - v1.y) - (p.y - v1.y) * (v2.x - v1.x);

		if ((d < 0 && lastd > 0) || (d > 0 && lastd < 0)) {
			// point is outside of this edge
			inside = false;
			break;
		}
		lastd = d;
	}
	return inside;
}

#ifdef WIN32
#include <Windows.h>
#include <Shlobj.h>

void print_color(int colors)
{
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	colors = colors ? colors : (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
	SetConsoleTextAttribute(console, (WORD)colors);
}

string getConfigDir()
{
	char path[MAX_PATH];
	SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, 0, path);
	return string(path) + "\\AppData\\Roaming\\bspguy\\";
}

bool dirExists(const string& dirName_in)
{
	DWORD ftyp = GetFileAttributesA(dirName_in.c_str());
	if (ftyp == INVALID_FILE_ATTRIBUTES)
		return false;  //something is wrong with your path!

	if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
		return true;   // this is a directory!

	return false;    // this is not a directory!
}

bool createDir(const string& dirName)
{
	int ret = CreateDirectory(dirName.c_str(), NULL);
	if (ret == ERROR_PATH_NOT_FOUND)
	{
		logf("Could not create directory: %s", dirName.c_str());
		return false;
	}
	return true;
}

#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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

string getConfigDir()
{
	return string("") + getenv("HOME") + "/.config/bspguy/";
}

bool dirExists(const string& dirName)
{
	struct stat sb;
	return stat(dirName.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

bool createDir(const string& dirName)
{
	if (dirExists(dirName))
		return true;

	int ret = mkdir(dirName.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (ret != 0)
	{
		logf("Could not create directory: %s", dirName.c_str());
		return false;
	}
	return true;
}
#endif
