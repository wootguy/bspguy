#include "Widget.h"

struct ModelInfo {
	string classname;
	string targetname;
	string model;
	string val;
	string usage;
	int entIdx;
};

struct AllocInfo {
	string texname;
	string faceCount;
	string val;
	string usage;
	float sort;
	int faceIdx;
};

struct ExtentInfo {
	string texname;
	string dimensions;
	string faceCount;
	string subsNeeded;
	int mip;
	int sort;
};

struct StatInfo {
	string name;
	string val;
	string max;
	string fullness;
	float progress;
	ImVec4 color;
};

class LimitsWidget : public Widget {
public:
	using Widget::Widget;
	void draw() override;

	void drawLimitsSummary(Bsp* map, bool modalMode);

private:
	vector<ModelInfo> limitModels[SORT_MODES];
	vector<AllocInfo> limitAllocs;
	vector<ExtentInfo> limitExtents;
	vector<StatInfo> stats;

	void drawLimitTab(Bsp* map, int sortMode);
	void drawAllocBlockLimitTab(Bsp* map);
	void drawFaceExtentsLimitTab();
	StatInfo calcStat(string name, uint val, uint max, bool isMem);
	ModelInfo calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, uint val, uint max, bool isMem);
};