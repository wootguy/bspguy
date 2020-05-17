#include "util.h"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <string.h>
#include "Wad.h"

ProgressMeter g_progress;

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
		printf("\x1B[0m");
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
	printf("\x1B[%s;%sm", mode, color);
}
#endif