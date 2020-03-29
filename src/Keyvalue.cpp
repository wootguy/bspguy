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
	int coordidx = 0;
	int begin = -1;
	for (int i = 0, len = value.length(); i < len; i++)
	{
		char c = value[i];
		if (begin != -1)
		{
			if (i == len-1)
				i = len;
			if (c == ' ' || i == len)
			{
				float coord = atof(value.substr(begin,i-begin).c_str());
				if (coordidx == 0)
					v.x = coord;
				else if (coordidx == 1)
					v.y = coord;
				else if (coordidx == 2)
					v.z = coord;
				else
					cout << "too many coordinates in vertex '" + value + "'\n";
				coordidx++;
				begin = -1;
			}
		}
		else if (isdigit(c) || c == '.' || c == '-')
			begin = i;
	}
	if (coordidx < 3) cout << "Not enough coordinates in vector '" + value + "'\n";
	return v;
}
