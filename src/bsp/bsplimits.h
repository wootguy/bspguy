#define MAX_MAP_HULLS	4
#define MAX_MAP_COORD 131072 // stuff breaks past this point

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
#define MAX_MAP_VISDATA (64 * ( 1024 * 1024 )) // 64 MB
#define MAX_MAP_ENTS 8192
#define MAX_MAP_SURFEDGES 512000
#define MAX_MAP_EDGES 256000
#define MAX_MAP_TEXTURES 4096
#define MAX_MAP_LIGHTDATA (64 * ( 1024 * 1024 )) // 64 MB
#define MAX_TEXTURE_DIMENSION 1024
#define MAXTEXTURENAME 16
#define MIPLEVELS 4
#define MAX_TEXTURE_SIZE ((MAX_TEXTURE_DIMENSION * MAX_TEXTURE_DIMENSION * sizeof(short) * 3) / 2)

#define MAX_KEYS_PER_ENT 64 // just guessing
#define MAX_KEY_LEN 256 // not sure if this includes the null char
#define MAX_VAL_LEN 4096 // not sure if this includes the null char

#define MAXLIGHTMAPS    4