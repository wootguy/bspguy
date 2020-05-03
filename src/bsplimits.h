#define MAX_MAP_HULLS	4
#define MAX_MAP_COORD 32767 // stuff breaks past this point

#define MAX_MAP_MODELS 4096
#define MAX_MAP_PLANES 65535
#define MAX_MAP_VERTS 65535
#define MAX_MAP_NODES 32768
#define MAX_MAP_TEXINFOS 32767
#define MAX_MAP_FACES 65535 // This ought to be 32768, otherwise faces(in world) can become invisible. --vluzacn
#define MAX_MAP_CLIPNODES 32767
#define MAX_MAP_LEAVES 65536
#define MAX_MAP_MARKSURFS 65536
#define MAX_MAP_TEXDATA 0
#define MAX_MAP_VISDATA 0x800000 //0x400000 //vluzacn
#define MAX_MAP_ENTS 8192
#define MAX_MAP_SURFEDGES 512000
#define MAX_MAP_EDGES 256000
#define MAX_MAP_TEXTURES 4096
#define MAX_MAP_LIGHTDATA 0x3000000 //0x600000 //vluzacn

#define MAXLIGHTMAPS    4