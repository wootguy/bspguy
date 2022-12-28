#pragma once
#include "Socket.h"
#include <thread>
#include "mstream.h"
#include <vector>
#include <mutex>
#include <queue>

#define MAX_EDICTS 8192

#define SVC_DELTAPACKETENTITIES 41 
#define SVC_WELCOME 255 // new client connect ack

// client packet types
#define CLC_CONNECT 1 
#define CLC_DELTA_ACK 2
#define CLC_DELTA_RESET 3 // client delta desynced, request delta from null state

// flags for indicating which edict fields were updated
#define FL_DELTA_ORIGIN_X		(1 << 0)
#define FL_DELTA_ORIGIN_Y		(1 << 1)
#define FL_DELTA_ORIGIN_Z		(1 << 2)
#define FL_DELTA_ANGLES_X		(1 << 3)
#define FL_DELTA_ANGLES_Y		(1 << 4)
#define FL_DELTA_ANGLES_Z		(1 << 5)
#define FL_DELTA_MODELINDEX		(1 << 6)
#define FL_DELTA_SKIN			(1 << 7)
#define FL_DELTA_BODY			(1 << 8)
#define FL_DELTA_EFFECTS		(1 << 9)
#define FL_DELTA_SEQUENCE		(1 << 10)
#define FL_DELTA_GAITSEQUENCE	(1 << 11)
#define FL_DELTA_FRAME			(1 << 12)
#define FL_DELTA_ANIMTIME		(1 << 13)
#define FL_DELTA_FRAMERATE		(1 << 14)
#define FL_DELTA_CONTROLLER_0	(1 << 15)
#define FL_DELTA_CONTROLLER_1	(1 << 16)
#define FL_DELTA_CONTROLLER_2	(1 << 17)
#define FL_DELTA_CONTROLLER_3	(1 << 18)
#define FL_DELTA_BLENDING_0		(1 << 19)
#define FL_DELTA_BLENDING_1		(1 << 20)
#define FL_DELTA_SCALE			(1 << 21)
#define FL_DELTA_RENDERMODE		(1 << 22)
#define FL_DELTA_RENDERAMT		(1 << 23)
#define FL_DELTA_RENDERCOLOR_0	(1 << 24)
#define FL_DELTA_RENDERCOLOR_1	(1 << 25)
#define FL_DELTA_RENDERCOLOR_2	(1 << 26)
#define FL_DELTA_RENDERFX		(1 << 27)
#define FL_DELTA_AIMENT			(1 << 28)

#define FLOAT_TO_FIXED(v, fractional_bits) (v * (1 << fractional_bits) + 0.5f)
#define FIXED_TO_FLOAT(v, fractional_bits) ((float)v / (float)(1 << fractional_bits))

// TODO: player doesn't need: animtime, controller, blending, gaitsequence, aiment
struct netedict {
	bool		isValid;		// true if edict is rendered and sent to clients
	float		origin[3];
	uint16_t	angles[3];		// Model angles (0-360 scaled to 0-65535)
	uint16_t	modelindex;

	uint8_t		skin;
	uint8_t		body;			// sub-model selection for studiomodels
	uint8_t 	effects;

	uint8_t		sequence;		// animation sequence
	uint8_t		gaitsequence;	// movement animation sequence for player (0 for none)
	uint8_t		frame;			// % playback position in animation sequences (0..255)
	uint8_t		animtime;		// world time when frame was set
	uint8_t		framerate;		// animation playback rate (-8x to 8x)
	uint8_t		controller[4];	// bone controller setting (0..255)
	uint8_t		blending[2];	// blending amount between sub-sequences (0..255)

	uint8_t		scale;			// sprite rendering scale (0..255)

	uint8_t		rendermode;
	uint8_t		renderamt;
	uint8_t		rendercolor[3];
	uint8_t		renderfx;

	int16_t		aiment;		// entity pointer when MOVETYPE_FOLLOW, 0 if movetype is not MOVETYPE_FOLLOW

	float		velocity[3];	// calculated locally
};

struct DeltaPacket {
	uint16_t fragmentId;

	Packet packet;
};

struct DeltaUpdate {
	uint16_t updateId;
	bool acked; // did we tell the server which packets we received for this update?

	// 8192 edict deltas can't always fit in a single packet
	std::vector<DeltaPacket> packets;
};

class SvenTV {
public:
	netedict* baselines = NULL; // "edicts" states are computed from this + the latest delta packet
	netedict* lastedicts = NULL; // edicts from the previous delta update
	netedict* interpedicts = NULL; // edicts interpolated between the previous and current states
	netedict* edicts = NULL; // edicts in the latest delta update packet
	float updateRate = 10;

	std::mutex edicts_mutex; // lock before reading/writing edicts
	std::mutex command_mutex; // lock before using commands
	std::queue<std::string> commands; // thread-safe commands from sventv to the renderer

	SvenTV(IPV4 serverAddr);
	~SvenTV();

	// call each time a frame is rendered
	void interpolateEdicts();

private:
	IPV4 serverAddr;
	volatile bool shouldKillThread = false;
	uint16_t lastBaselineId = 0;
	uint16_t lastAckId = 0;
	Packet lastAck;
	double lastDeltaTime = 0; // time the last delta packet was received (for interpolation)

	std::vector<DeltaUpdate> receivedDeltas;

	void think();
	void connect();
	void handleDeltaPacket(mstream& reader, const Packet& packet);

	// applies all deltas from the packet to the baseline edicts to create the latest set of edicts
	bool applyDelta(const Packet& packet, bool isBaseline);

	Socket* socket = NULL;
	std::thread* netThread = NULL;
};