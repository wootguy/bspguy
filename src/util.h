#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#define __WINDOWS__

bool fileExists(const string& fileName);

char * loadFile( const string& fileName, int& length);

