#pragma once
#include "Keyvalue.h"
#include <map>

typedef std::map< std::string, std::string > hashmap;

class Entity
{
public:
	hashmap keyvalues;
	vector<string> keyOrder;

	int cachedModelIdx = -2; // -2 = not cached
	vector<string> cachedTargets;
	bool targetsCached = false;

	Entity(void);
	Entity(const std::string& classname);
	~Entity(void);

	void addKeyvalue(Keyvalue& k);
	void addKeyvalue(const std::string& key, const std::string& value);
	void removeKeyvalue(const std::string& key);
	bool renameKey(int idx, string newName);
	void clearAllKeyvalues();
	void clearEmptyKeyvalues();

	void setOrAddKeyvalue(const std::string& key, const std::string& value);

	// returns -1 for invalid idx
	int getBspModelIdx();

	bool isBspModel();

	vec3 getOrigin();

	bool hasKey(const std::string& key);

	vector<string> getTargets();

	bool hasTarget(string tname);

	void renameTargetnameValues(string oldTargetname, string newTargetname);

	int getMemoryUsage(); // aproximate
};

