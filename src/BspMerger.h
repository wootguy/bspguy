#include "util.h"
#include "Bsp.h"

// bounding box for a map, used for arranging maps for merging
struct MAPBLOCK
{
	vec3 mins, maxs, size, offset;
	Bsp* map;
	string merge_name;

	bool intersects(MAPBLOCK& other) {
		return (mins.x <= other.maxs.x && maxs.x >= other.mins.x) &&
			(mins.y <= other.maxs.y && maxs.y >= other.mins.y) &&
			(mins.z <= other.maxs.z && maxs.y >= other.mins.z);
	}
};

class BspMerger {
public:
	BspMerger();

	Bsp* merge(vector<Bsp*> maps, vec3 gap);

private:
	int merge_ops = 0;

	void merge(MAPBLOCK& dst, MAPBLOCK& src, string resultName);
	vector<vector<vector<MAPBLOCK>>> separate(vector<Bsp*>& maps, vec3 gap);
};