#include "Keyvalue.h"
#include "util.h"

Keyvalue::Keyvalue(std::string line)
{
	int begin = -1;
	int end = -1;

	key.clear();
	value.clear();
	int comment = 0;

	for (uint i = 0; i < line.length(); i++)
	{
		if (line[i] == '/')
		{
			if (++comment >= 2)
			{
				key.clear();
				value.clear();
				break;
			}
		}
		else
			comment = 0;
		if (line[i] == '"')
		{
			if (begin == -1)
				begin = i + 1;
			else
			{
				end = i;
				if (key.length() == 0)
				{
					key = line.substr(begin, end - begin);
					begin = end = -1;
				}
				else
				{
					value = line.substr(begin, end - begin);
					break;
				}
			}
		}
	}
}

Keyvalue::Keyvalue(void)
{
	key.clear();
	value.clear();
}

Keyvalue::Keyvalue(std::string key, std::string value)
{
	this->key = std::move(key);
	this->value = std::move(value);
}

vec3 Keyvalue::getVector()
{
	return parseVector(value);
}
