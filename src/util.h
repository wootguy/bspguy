#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#define __WINDOWS__

bool fileExists(const string& fileName);

char * loadFile( const string& fileName, int& length);

vector<string> splitString(string str, const char* delimitters);

string basename(string path);

string stripExt(string filename);

bool isNumeric(const std::string& s);
