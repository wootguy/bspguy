#include "Entity.h"
#include <string>
#include "util.h"
#include <algorithm>

using namespace std;

Entity::Entity(void)
{
}

Entity::Entity(const string& classname)
{
	addKeyvalue("classname", classname);
}

Entity::~Entity(void)
{
}

void Entity::addKeyvalue( Keyvalue& k )
{
	int dup = 1;
	if (keyvalues.find(k.key) == keyvalues.end()) {
		keyvalues[k.key] = k.value;
		keyOrder.push_back(k.key);
	}
	else
	{
		while (true)
		{
			string newKey = k.key + '#' + to_string((long long)dup);
			if (keyvalues.find(newKey) == keyvalues.end())
			{
				//println("wrote dup key " + newKey);
				keyvalues[newKey] = k.value;
				keyOrder.push_back(newKey);
				break;
			}
			dup++;
		}
	}
}

void Entity::addKeyvalue(const std::string& key, const std::string& value)
{
	keyvalues[key] = value;

	keyOrder.push_back(key);
}

bool Entity::hasKey(const std::string& key)
{
	return keyvalues.find(key) != keyvalues.end();
}

int Entity::getBspModelIdx() {
	if (!hasKey("model"))
		return -1;

	string model = keyvalues["model"];
	if (model.size() <= 1 || model[0] != '*') {
		return -1;
	}

	string modelIdxStr = model.substr(1);
	if (!isNumeric(modelIdxStr)) {
		return -1;
	}

	return atoi(modelIdxStr.c_str());
}

bool Entity::isBspModel() {
	return getBspModelIdx() >= 0;
}
