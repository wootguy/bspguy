#include "util.h"
#include <iostream>
#include <fstream>
#include <string>

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
