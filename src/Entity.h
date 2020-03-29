#pragma once
#include "Keyvalue.h"
#include <unordered_map>

typedef std::unordered_map< std::string, std::string > hashmap;

class Entity
{
public:
	hashmap keyvalues;

	Entity(void);
	Entity(const std::string& classname);
	~Entity(void);

	void addKeyvalue(Keyvalue& k);
	void addKeyvalue(const std::string& key, const std::string& value);

	bool hasKey(const std::string& key);
};

