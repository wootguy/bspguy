#include "Fgd.h"

Fgd::Fgd(string path) {
	this->path = path;
	this->name = stripExt(basename(path));
}

void Fgd::parse() {

	if (!fileExists(path)) {
		printf("Missing FGD: %s\n", path.c_str());
		return;
	}

	printf("Parsing %s\n", path.c_str());

	ifstream in(path);

	lineNum = 0;
	int lastBracket = -1;

	FgdClass* fgdClass = new FgdClass();
	int bracketNestLevel = 0;

	line = "";
	while (getline(in, line)) {
		lineNum++;

		// strip comments
		size_t cpos = line.find("//");
		if (cpos != string::npos)
			line = line.substr(0, cpos);

		line = trimSpaces(line);

		if (line.empty())
			continue;
		
		if (line[0] == '@') {
			if (bracketNestLevel) {
				printf("ERROR: New FGD class definition starts before previous one ends (line %d)\n", lineNum);
			}

			parseClassHeader(*fgdClass);
		}

		if (line.find('[') != string::npos) {
			bracketNestLevel++;
		}
		if (line.find(']') != string::npos) {
			bracketNestLevel--;
			if (bracketNestLevel == 0) {
				classes.push_back(fgdClass);
				fgdClass = new FgdClass();
			}
		}

		if (line == "[" || line == "]") {
			continue;
		}

		if (bracketNestLevel == 1) {
			parseKeyvalue(*fgdClass);
		}

		if (bracketNestLevel == 2) {
			if (fgdClass->keyvalues.size() == 0) {
				printf("ERROR: Choice values begin before any keyvalue are defined (line %d)\n", lineNum);
				continue;
			}
			KeyvalueDef& lastKey = fgdClass->keyvalues[fgdClass->keyvalues.size()-1];
			parseChoicesOrFlags(lastKey);
		}
	}

	processClassInheritance();
}

void Fgd::parseClassHeader(FgdClass& fgdClass) {
	vector<string> headerParts = splitString(line, "=");

	// group parts enclosed in parens or quotes
	vector<string> typeParts = groupParts(splitString(trimSpaces(headerParts[0]), " "));

	string classType = toLowerCase(typeParts[0]);

	if (classType == "@baseclass") {
		fgdClass.classType = FGD_CLASS_BASE;
	}
	else if (classType == "@solidclass") {
		fgdClass.classType = FGD_CLASS_SOLID;
	}
	else if (classType == "@pointclass") {
		fgdClass.classType = FGD_CLASS_POINT;
	}
	else {
		printf("ERROR: Unrecognized FGD class type '%s'\n", typeParts[0].c_str());
	}
	
	// parse constructors/properties
	for (int i = 1; i < typeParts.size(); i++) {
		string lpart = toLowerCase(typeParts[i]);

		if (lpart.find("base(") == 0) {
			vector<string> baseClassList = splitString(getValueInParens(typeParts[i]), ",");
			for (int k = 0; k < baseClassList.size(); k++) {
				string baseClass = trimSpaces(baseClassList[k]);
				fgdClass.baseClasses.push_back(baseClass);
			}
		}
		else if (lpart.find("size(") == 0) {
			vector<string> sizeList = splitString(getValueInParens(typeParts[i]), ",");

			if (sizeList.size() == 1) {
				vec3 size = parseVector(sizeList[0]);
				fgdClass.mins = size * -0.5f;
				fgdClass.maxs = size * 0.5f;
			}
			else if (sizeList.size() == 2) {
				fgdClass.mins = parseVector(sizeList[0]);
				fgdClass.maxs = parseVector(sizeList[1]);
			}
			else {
				printf("ERROR: Expected 2 vectors in size() property (line %d)\n", lineNum);
			}

			fgdClass.sizeSet = true;
		}
		else if (lpart.find("color(") == 0) {
			vector<string> nums = splitString(getValueInParens(typeParts[i]), " ");

			if (nums.size() == 3) {
				fgdClass.color = { (byte)atoi(nums[0].c_str()), (byte)atoi(nums[1].c_str()), (byte)atoi(nums[2].c_str()) };
			}
			else {
				printf("ERROR: Expected 3 components in color() property (line %d)\n", lineNum);
			}

			fgdClass.colorSet = true;
		}
		else if (lpart.find("studio(") == 0) {
			fgdClass.model = getValueInParens(typeParts[i]);
			fgdClass.isModel = true;
		}
		else if (lpart.find("iconsprite(") == 0) {
			fgdClass.iconSprite = getValueInParens(typeParts[i]);
		}
		else if (lpart.find("sprite(") == 0) {
			fgdClass.sprite = getValueInParens(typeParts[i]);
			fgdClass.isSprite = true;
		}
		else if (lpart.find("decal(") == 0) {
			fgdClass.isDecal = true;
		}
		else if (typeParts[i].find("(") != string::npos) {
			string typeName = typeParts[i].substr(0, typeParts[i].find("("));
			printf("WARNING: Unrecognized type %s (line %d)\n", typeName.c_str(), lineNum);
		}
	}

	if (headerParts.size() == 0) {
		printf("ERROR: Unexpected end of class header (line %d)\n", lineNum);
		return;
	}

	vector<string> nameParts = splitString(headerParts[1], ":");

	if (nameParts.size() >= 2) {
		fgdClass.description = getValueInQuotes(nameParts[1]);
	}
	if (nameParts.size() >= 1) {
		fgdClass.name = trimSpaces(nameParts[0]);
		// strips brackets if they're there
		fgdClass.name = fgdClass.name.substr(0, fgdClass.name.find(" "));
	}
}

void Fgd::parseKeyvalue(FgdClass& outClass) {
	vector<string> keyParts = splitString(line, ":");

	KeyvalueDef def;

	def.name = keyParts[0].substr(0, keyParts[0].find("("));
	def.valueType = toLowerCase(getValueInParens(keyParts[0]));

	if (keyParts.size() > 1)
		def.description = getValueInQuotes(keyParts[1]);

	if (keyParts.size() > 2) {
		if (keyParts[2].find("=") != string::npos) { // choice
			def.defaultValue = trimSpaces(keyParts[2].substr(0, keyParts[2].find("=")));
		}
		else if (keyParts[2].find("\"") != string::npos) { // string
			def.defaultValue = getValueInQuotes(keyParts[2]);
		}
		else { // integer
			def.defaultValue = trimSpaces(keyParts[2]);
		}
	}

	outClass.keyvalues.push_back(def);

	//cout << "ADD KEY " << def.name << "(" << def.valueType << ") : " << def.description << " : " << def.defaultValue << endl;
}

void Fgd::parseChoicesOrFlags(KeyvalueDef& outKey) {
	vector<string> keyParts = splitString(line, ":");

	KeyvalueChoice def;

	if (keyParts[0].find("\"") != string::npos) {
		def.svalue = getValueInQuotes(keyParts[0]);
		def.ivalue = 0;
		def.isInteger = false;
	}
	else {
		def.svalue = trimSpaces(keyParts[0]);
		def.ivalue = atoi(keyParts[0].c_str());
		def.isInteger = true;
	}
	
	if (keyParts.size() > 1)
		def.name = getValueInQuotes(keyParts[1]);

	outKey.choices.push_back(def);

	//cout << "ADD CHOICE LINE " << lineNum << " = " << def.svalue << " : " << def.name << endl;
}

vector<string> Fgd::groupParts(vector<string>& ungrouped) {
	vector<string> grouped;

	for (int i = 0; i < ungrouped.size(); i++) {

		if (stringGroupStarts(ungrouped[i])) {
			string groupedPart = ungrouped[i];

			for (i = i + 1; i < ungrouped.size(); i++) {
				groupedPart += " " + ungrouped[i];
				if (stringGroupEnds(ungrouped[i])) {
					break;
				}
			}
			grouped.push_back(groupedPart);
		}
		else {
			grouped.push_back(ungrouped[i]);
		}
	}

	return grouped;
}

bool Fgd::stringGroupStarts(string s) {
	if (s.find("(") != string::npos) {
		return s.find(")") == string::npos;
	}
	
	size_t startStringPos = s.find("\"");
	if (startStringPos != string::npos) {
		size_t endStringPos = s.rfind("\"");
		return endStringPos == startStringPos || endStringPos == string::npos;
	}
	
	return false;
}

bool Fgd::stringGroupEnds(string s) {
	return s.find(")") != string::npos || s.find("\"") != string::npos;
}

string Fgd::getValueInParens(string s) {
	s = s.substr(s.find("(") + 1);
	s = s.substr(0, s.rfind(")"));
	return trimSpaces(s);
}

string Fgd::getValueInQuotes(string s) {
	s = s.substr(s.find("\"") + 1);
	s = s.substr(0, s.rfind("\""));
	return s;
}

void Fgd::processClassInheritance() {
	for (int i = 0; i < classes.size(); i++) {
		classMap[classes[i]->name] = classes[i];
		//printf("Got class %s\n", classes[i]->name.c_str());
	}

	for (int i = 0; i < classes.size(); i++) {
		vector<FgdClass*> allBaseClasses;
		classes[i]->getBaseClasses(this, allBaseClasses);

		if (allBaseClasses.size() != 0)
		{
			//cout << classes[i]->name << " INHERITS FROM: ";
			for (int k = allBaseClasses.size()-1; k >= 0; k--) {
				if (!classes[i]->colorSet && allBaseClasses[k]->colorSet) {
					classes[i]->color = allBaseClasses[k]->color;
				}
				if (!classes[i]->sizeSet && allBaseClasses[k]->sizeSet) {
					classes[i]->mins = allBaseClasses[k]->mins;
					classes[i]->maxs = allBaseClasses[k]->maxs;
				}
				//cout << allBaseClasses[k]->name << " ";
			}
			//cout << endl;
		}
		
	}
}

void FgdClass::getBaseClasses(Fgd* fgd, vector<FgdClass*>& inheritanceList) {
	for (int i = baseClasses.size()-1; i >= 0; i--) {
		if (fgd->classMap.find(baseClasses[i]) == fgd->classMap.end()) {
			printf("ERROR: Invalid base class %s\n", baseClasses[i].c_str());
			continue;
		}
		inheritanceList.push_back(fgd->classMap[baseClasses[i]]);
	}
	for (int i = baseClasses.size() - 1; i >= 0; i--) {
		if (fgd->classMap.find(baseClasses[i]) == fgd->classMap.end()) {
			continue;
		}
		fgd->classMap[baseClasses[i]]->getBaseClasses(fgd, inheritanceList);
	}
}