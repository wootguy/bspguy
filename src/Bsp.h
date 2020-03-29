#include "Wad.h"
#include "Entity.h"

#define BSP_MODEL_BYTES 64 // size of a BSP model in bytes

#define LUMP_ENTITIES      0
#define LUMP_PLANES        1
#define LUMP_TEXTURES      2
#define LUMP_VERTICES      3
#define LUMP_VISIBILITY    4
#define LUMP_NODES         5
#define LUMP_TEXINFO       6
#define LUMP_FACES         7
#define LUMP_LIGHTING      8
#define LUMP_CLIPNODES     9
#define LUMP_LEAVES       10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES        12
#define LUMP_SURFEDGES    13
#define LUMP_MODELS       14
#define HEADER_LUMPS      15

struct BSPLUMP
{
	int nOffset; // File offset to data
	int nLength; // Length of data
};

struct BSPHEADER
{
	int nVersion;           // Must be 30 for a valid HL BSP file
	BSPLUMP lump[HEADER_LUMPS]; // Stores the directory of lumps
};

struct BSPTEXDATA
{
	int numTex;
	int * offset;
	int * len;
	WADTEX ** tex;
};

struct membuf : std::streambuf
{
	membuf(char* begin, int len) {
		this->setg(begin, begin, begin + len);
	}
};

class Bsp
{
public:
	std::string path;
	BSPHEADER header;
	byte ** lumps;
	bool valid;
	
	vector<Entity*> ents;

	Bsp();
	Bsp(std::string fname);
	~Bsp();

private:

	bool load_lumps(string fname);

	void load_ents();
};