#include "Keyvalue.h"
#include "util.h"

Keyvalue::Keyvalue(string line)
{
	int begin = -1;
	int end = -1;

	key = "";
	value = "";
	int comment = 0;

	for (uint i = 0; i < line.length(); i++)
	{
		if (line[i] == '/')
		{
			if (++comment >= 2)
			{
				key = value = "";
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
					key = line.substr(begin,end-begin);
					begin = end = -1;
				}
				else
				{
					value = line.substr(begin,end-begin);
					break;
				}
			}
		}
	}
}

Keyvalue::Keyvalue(void)
{
	key = value = "";
}

Keyvalue::Keyvalue( std::string key, std::string value )
{
	this->key = key;
	this->value = value;
}


Keyvalue::~Keyvalue(void)
{
}

vec3 Keyvalue::getVector()
{
	vec3 v;
	vector<string> parts = splitString(value, " ");

	if (parts.size() != 3) {
		cout << "Not enough coordinates in vector '" << value << "'\n";
		return v;
	}

	v.x = atof(parts[0].c_str());
	v.y = atof(parts[1].c_str());
	v.z = atof(parts[2].c_str());

	return v;
}
