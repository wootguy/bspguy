#pragma once
#include "Keyvalue.h"
#include <map>

typedef std::map< std::string, std::string > hashmap;

class Entity
{
public:
	hashmap keyvalues;
	std::vector<std::string> keyOrder;

	int cachedModelIdx = -2; // -2 = not cached
	std::vector<std::string> cachedTargets;
	bool targetsCached = false;

	Entity(void) = default;
	Entity(const std::string& classname);
	~Entity(void) = default;

	void addKeyvalue(const std::string& key, const std::string& value);
	void removeKeyvalue(const std::string& key);
	bool renameKey(int idx, std::string newName);
	void clearAllKeyvalues();
	void clearEmptyKeyvalues();

	void setOrAddKeyvalue(const std::string& key, const std::string& value);

	// returns -1 for invalid idx
	int getBspModelIdx();
	int getBspModelIdxForce();

	bool isBspModel();

	vec3 getOrigin();

	bool hasKey(const std::string& key);

	std::vector<std::string> getTargets();

	bool hasTarget(std::string checkTarget);

	void renameTargetnameValues(std::string oldTargetname, std::string newTargetname);

	size_t getMemoryUsage(); // aproximate
};

