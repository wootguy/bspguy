#include "Bsp.h"
#include "util.h"
#include <algorithm>
#include <string.h>
#include <sstream>

Bsp::Bsp() {
	lumps = new byte * [HEADER_LUMPS];

	header.nVersion = 30;

	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nLength = 0;
		header.lump[i].nOffset = 0;
		lumps[i] = NULL;
	}

	name = "merged";
}

Bsp::Bsp(std::string fpath)
{
	this->path = fpath;
	this->name = stripExt(basename(fpath));

	bool exists = true;
	if (!fileExists(fpath)) {
		cout << "ERROR: " + fpath + " not found\n";
		return;
	}

	if (!load_lumps(fpath)) {
		cout << fpath + " is not a valid BSP file\n";
		return;
	}

	load_ents();
}

Bsp::~Bsp()
{	 
	for (int i = 0; i < HEADER_LUMPS; i++)
		if (lumps[i])
			delete [] lumps[i];
	delete [] lumps;

	for (int i = 0; i < ents.size(); i++)
		delete ents[i];
}

void Bsp::merge(Bsp& other) {
	cout << "Merging " << other.path << " into " << path << endl;

	for (int i = 0; i < HEADER_LUMPS; i++) {
		if (!lumps[i] && !other.lumps[i]) {
			//cout << "Skipping " << g_lump_names[i] << " lump (missing from both maps)\n";
		}
		else if (!lumps[i]) {
			//cout << "Replacing " << g_lump_names[i] << " lump\n";
			header.lump[i].nLength = other.header.lump[i].nLength;
			lumps[i] = new byte[other.header.lump[i].nLength];
			memcpy(lumps[i], other.lumps[i], other.header.lump[i].nLength);

			// process the lump here (TODO: faster to just copy wtv needs copying)
			switch (i) {
				case LUMP_ENTITIES:
					load_ents(); break;
			}
		}
		else if (!other.lumps[i]) {
			cout << "Keeping " << g_lump_names[i] << " lump\n";
		}
		else {
			cout << "Merging " << g_lump_names[i] << " lump\n";

			switch (i) {
				case LUMP_ENTITIES:
					merge_ents(other); break;
			}
		}
	}
}

void Bsp::merge_ents(Bsp& other)
{

	for (int i = 0; i < other.ents.size(); i++) {
		if (other.ents[i]->keyvalues["classname"] == "worldspawn") {
			Entity* otherWorldspawn = other.ents[i];

			vector<string> otherWads = splitString(otherWorldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < otherWads.size(); j++) {
				otherWads[i] = basename(otherWads[i]);
			}

			Entity* worldspawn = NULL;
			for (int k = 0; k < ents.size(); k++) {
				if (ents[k]->keyvalues["classname"] == "worldspawn") {
					worldspawn = ents[k];
					break;
				}
			}

			// merge wad list
			vector<string> thisWads = splitString(worldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < thisWads.size(); j++) {
				thisWads[i] = basename(thisWads[i]);
			}

			// add unique wads to this map
			for (int j = 0; j < otherWads.size(); j++) {
				if (std::find(thisWads.begin(), thisWads.end(), otherWads[j]) == thisWads.end()) {
					thisWads.push_back(otherWads[j]);
					cout << "Adding " << otherWads[j] << "\n";
				}
			}

			worldspawn->keyvalues["wad"] = "";
			for (int j = 0; j < thisWads.size(); j++) {
				worldspawn->keyvalues["wad"] += thisWads[j] + ";";
			}

			// include prefixed version of the other maps keyvalues
			for (auto it = otherWorldspawn->keyvalues.begin(); it != otherWorldspawn->keyvalues.end(); it++) {
				if (it->first == "classname" || it->first == "wad") {
					continue;
				}
				// TODO: unknown keyvalues crash the game? Try something else.
				//worldspawn->addKeyvalue(Keyvalue(other.name + "_" + it->first, it->second));
			}
		}
		else {
			Entity* copy = new Entity();
			copy->keyvalues = other.ents[i]->keyvalues;
			ents.push_back(copy);
		}
	}

	stringstream ent_data;

	for (int i = 0; i < ents.size(); i++) {
		ent_data << "{\n";

		for (auto it = ents[i]->keyvalues.begin(); it != ents[i]->keyvalues.end(); it++) {
			ent_data << "\"" << it->first << "\" \"" << it->second << "\"\n";
		}

		ent_data << "}\n";
	}

	string str_data = ent_data.str();

	delete lumps[LUMP_ENTITIES];
	header.lump[LUMP_ENTITIES].nLength = str_data.size();
	lumps[LUMP_ENTITIES] = new byte[str_data.size()];
	memcpy((char*)lumps[LUMP_ENTITIES], str_data.c_str(), str_data.size());
}

void Bsp::write(string path) {
	cout << "Writing " << path << endl;

	// calculate lump offsets
	int offset = sizeof(BSPHEADER);
	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nOffset = offset;
		offset += header.lump[i].nLength;
	}

	ofstream file(path, ios::out | ios::binary | ios::trunc);


	file.write((char*)&header, sizeof(BSPHEADER));

	// write the lumps
	for (int i = 0; i < HEADER_LUMPS; i++) {
		file.write((char*)lumps[i], header.lump[i].nLength);
	}
}

bool Bsp::load_lumps(string fpath)
{
	bool valid = true;

	// Read all BSP Data
	ifstream fin(fpath, ios::binary | ios::ate);
	int size = fin.tellg();
	fin.seekg(0, fin.beg);

	if (size < sizeof(BSPHEADER) + sizeof(BSPLUMP)*HEADER_LUMPS)
		return false;

	fin.read((char*)&header.nVersion, sizeof(int));
	
	for (int i = 0; i < HEADER_LUMPS; i++)
		fin.read((char*)&header.lump[i], sizeof(BSPLUMP));

	lumps = new byte*[HEADER_LUMPS];
	memset(lumps, 0, sizeof(byte*)*HEADER_LUMPS);
	
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (header.lump[i].nLength == 0) {
			lumps[i] = NULL;
			continue;
		}

		fin.seekg(header.lump[i].nOffset);
		if (fin.eof()) {
			cout << "FAILED TO READ BSP LUMP " + to_string(i) + "\n";
			valid = false;
		}
		else
		{
			lumps[i] = new byte[header.lump[i].nLength];
			fin.read((char*)lumps[i], header.lump[i].nLength);
		}
	}	
	
	fin.close();

	return valid;
}

void Bsp::load_ents()
{
	bool verbose = true;
	membuf sbuf((char*)lumps[LUMP_ENTITIES], header.lump[LUMP_ENTITIES].nLength);
	istream in(&sbuf);

	int lineNum = 0;
	int lastBracket = -1;
	Entity* ent = NULL;

	string line = "";
	while (getline(in, line))
	{
		lineNum++;
		if (line.length() < 1 || line[0] == '\n')
			continue;

		if (line[0] == '{')
		{
			if (lastBracket == 0)
			{
				cout << path + ".bsp ent data (line " + to_string(lineNum) + "): Unexpected '{'\n";
				continue;
			}
			lastBracket = 0;

			if (ent != NULL)
				delete ent;
			ent = new Entity();
		}
		else if (line[0] == '}')
		{
			if (lastBracket == 1)
				cout << path + ".bsp ent data (line " + to_string(lineNum) + "): Unexpected '}'\n";
			lastBracket = 1;

			if (ent == NULL)
				continue;

			ents.push_back(ent);
			ent = NULL;

			// you can end/start an ent on the same line, you know
			if (line.find("{") != string::npos)
			{
				ent = new Entity();
				lastBracket = 0;
			}
		}
		else if (lastBracket == 0 && ent != NULL) // currently defining an entity
		{
			Keyvalue k(line);
			if (k.key.length() && k.value.length())
				ent->addKeyvalue(k);
		}
	}	
	//cout << "got " << ents.size() <<  " entities\n";

	if (ent != NULL)
		delete ent;
}
