#pragma once
#include "util.h"

class Keyvalue
{
public:
	std::string key;
	std::string value;

	Keyvalue(std::string line);
	Keyvalue(std::string key, std::string value);
	Keyvalue(void);
	~Keyvalue(void) = default;

	vec3 getVector();
};

