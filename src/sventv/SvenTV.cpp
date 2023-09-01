#include "SvenTV.h"
#include "util.h"
#include "mstream.h"
#include <GLFW/glfw3.h>

// SvenTV TODO
//
// - packet ID overflows and loses sync after a while
//
// Player models:
// - uzi/mp5/m16/saw shoot anim too slow/smooth at updaterate 10
// - crossbow/sniper zoom tranistion animation needs smoothing
// - looking up/down past X degrees needs to rotate entire model, not just change blend
// - disable gait on player death

using namespace std;

SvenTV::SvenTV(IPV4 serverAddr) {
	this->serverAddr = serverAddr;
	baselines = new netedict[MAX_EDICTS];
	edicts = new netedict[MAX_EDICTS];
	lastedicts = new netedict[MAX_EDICTS];
	interpedicts = new netedict[MAX_EDICTS];
	memset(baselines, 0, MAX_EDICTS*sizeof(netedict));
	memset(edicts, 0, MAX_EDICTS*sizeof(netedict));
	memset(lastedicts, 0, MAX_EDICTS*sizeof(netedict));
	memset(interpedicts, 0, MAX_EDICTS*sizeof(netedict));
	netThread = new thread(&SvenTV::think, this);
}

SvenTV::~SvenTV() {
	shouldKillThread = true;

	if (netThread) {
		netThread->join();
		delete netThread;
	}

	delete[] baselines;
	delete[] lastedicts;
	delete[] interpedicts;
	delete[] edicts;	
	
}

void SvenTV::connect() {
	uint8_t connectDat = CLC_CONNECT;
	
	int counter = 0;
	Packet packet;
	while (!shouldKillThread) {

		if (counter++ % 10 == 0)
			socket->send(&connectDat, 1);

		if (socket->recv(packet)) {
			uint8_t packetType = (uint8_t)packet.data[0];

			if (packetType == SVC_WELCOME) {
				if (packet.sz < 2 || packet.sz > 64) {
					logf("Invalid welcome packet size %d\n", packet.sz);
					continue;
				}
				char mapNameBuffer[64];
				memcpy(mapNameBuffer, packet.data + 1, packet.sz - 1);
				mapNameBuffer[packet.sz - 1] = '\0';

				logf("Server welcomed us to map '%s'\n", mapNameBuffer);

				command_mutex.lock();
				commands.push("map " + string(mapNameBuffer));
				command_mutex.unlock();

				return;
			}
		}

		this_thread::sleep_for(chrono::milliseconds(50));
	}
}

bool compareByFragmentId(const DeltaPacket& a, const DeltaPacket& b)
{
	return a.fragmentId < b.fragmentId;
}

inline float lerp(float start, float end, float t) {
	return start + (end - start) * t;
}

inline int anglelerp16(int start, int end, float t) {
	// 65536 = 360 deg
	int shortest_angle = ((((end - start) % 65536) + 98304) % 65536) - 32768;

	return start + shortest_angle * t;
}

inline int anglelerp8(int start, int end, float t) {
	// 65536 = 360 deg
	int shortest_angle = ((((end - start) % 256) + 384) % 256) - 128;

	return start + shortest_angle * t;
}


void SvenTV::interpolateEdicts() {
	double delay = 1.0f / updateRate; // delay in seconds between packets
	float t = (glfwGetTime() - lastDeltaTime) / delay;

	if (t >= 1.0f) {
		for (int i = 0; i < MAX_EDICTS; i++) {
			if (!edicts[i].isValid) {
				interpedicts[i].isValid = false;
				continue;
			}
			interpedicts[i] = edicts[i];
		}

		return;
	}

	for (int i = 0; i < MAX_EDICTS; i++) {
		if (!edicts[i].isValid || !lastedicts[i].isValid) {
			interpedicts[i].isValid = false;
			continue;
		}

		netedict& out = interpedicts[i];
		netedict& start = lastedicts[i];
		netedict& end = edicts[i];

		out = start;

		out.origin[0] = lerp(start.origin[0], end.origin[0], t);
		out.origin[1] = lerp(start.origin[1], end.origin[1], t);
		out.origin[2] = lerp(start.origin[2], end.origin[2], t);

		out.velocity[0] = lerp(start.velocity[0], end.velocity[0], t);
		out.velocity[1] = lerp(start.velocity[1], end.velocity[1], t);
		out.velocity[2] = lerp(start.velocity[2], end.velocity[2], t);

		out.angles[0] = anglelerp16(start.angles[0], end.angles[0], t);
		out.angles[1] = anglelerp16(start.angles[1], end.angles[1], t);
		out.angles[2] = anglelerp16(start.angles[2], end.angles[2], t);

		if (start.sequence == end.sequence)
			out.frame = anglelerp8(start.frame, end.frame, t);
		else {
			out.frame = anglelerp8(0, end.frame, t);
			out.sequence = end.sequence;
		}
	}
}

bool SvenTV::applyDelta(const Packet& packet, bool isBaseline) {
	mstream reader(packet.data, packet.sz);

	uint8_t packetType;
	uint8_t offset;
	uint16_t fullIndex = 0;
	uint32_t deltaBits;
	uint16_t updateId; // ID of the global update which this packet belongs
	uint16_t baselineId; // ID of the update we should be delta-ing from
	uint16_t fragmentId; // ID of this packet within the update, unique only to this update

	reader.read(&packetType, 1);
	reader.read(&updateId, 2);
	reader.read(&baselineId, 2);
	reader.read(&fragmentId, 2);

	//logf("Apply delta %d frag %d%s\n", updateId, fragmentId, isBaseline ? " (BASELINE)" : "");

	int loop = -1;
	while (1) {
		loop++;
		reader.read(&offset, 1);

		if (reader.eom()) {
			break;
		}

		if (offset == 0) {
			reader.read(&fullIndex, 2);
		}
		else {
			fullIndex += offset;
		}

		if (fullIndex >= MAX_EDICTS) {
			logf("ERROR: Invalid delta packet wants to update edict %d at %d\n", (int)fullIndex, loop);
			memcpy(edicts, lastedicts, MAX_EDICTS*sizeof(netedict)); // rollback changes
			return false;
		}

		uint64_t startPos = reader.tell();

		netedict* ed = &baselines[fullIndex];

		if (!isBaseline) {
			// calculating current state from baseline and this delta packet
			ed = &edicts[fullIndex];
			lastedicts[fullIndex] = *ed;
			*ed = baselines[fullIndex]; // undo previous deltas, restart from baseline
		}

		reader.read(&deltaBits, 4);

		if (deltaBits == 0) {
			ed->isValid = false;
			//println("Skip free %d", fullIndex);
			continue;
		}
		ed->isValid = true;

		if (deltaBits & FL_DELTA_ORIGIN_X) {
			reader.read((void*)&ed->origin[0], 4);
		}
		if (deltaBits & FL_DELTA_ORIGIN_Y) {
			reader.read((void*)&ed->origin[1], 4);
		}
		if (deltaBits & FL_DELTA_ORIGIN_Z) {
			reader.read((void*)&ed->origin[2], 4);
		}
		if (deltaBits & FL_DELTA_ANGLES_X) {
			reader.read((void*)&ed->angles[0], 2);
		}
		if (deltaBits & FL_DELTA_ANGLES_Y) {
			reader.read((void*)&ed->angles[1], 2);
		}
		if (deltaBits & FL_DELTA_ANGLES_Z) {
			reader.read((void*)&ed->angles[2], 2);
		}
		if (deltaBits & FL_DELTA_MODELINDEX) {
			reader.read((void*)&ed->modelindex, 2);
		}
		if (deltaBits & FL_DELTA_SKIN) {
			reader.read((void*)&ed->skin, 1);
		}
		if (deltaBits & FL_DELTA_BODY) {
			reader.read((void*)&ed->body, 1);
		}
		if (deltaBits & FL_DELTA_EFFECTS) {
			reader.read((void*)&ed->effects, 1);
		}
		if (deltaBits & FL_DELTA_SEQUENCE) {
			reader.read((void*)&ed->sequence, 1);
		}
		if (deltaBits & FL_DELTA_GAITSEQUENCE) {
			reader.read((void*)&ed->gaitsequence, 1);
		}
		if (deltaBits & FL_DELTA_FRAME) {
			reader.read((void*)&ed->frame, 1);
		}
		if (deltaBits & FL_DELTA_ANIMTIME) {
			reader.read((void*)&ed->animtime, 1);
		}
		if (deltaBits & FL_DELTA_FRAMERATE) {
			reader.read((void*)&ed->framerate, 1);
		}
		if (deltaBits & FL_DELTA_CONTROLLER_0) {
			reader.read((void*)&ed->controller[0], 1);
		}
		if (deltaBits & FL_DELTA_CONTROLLER_1) {
			reader.read((void*)&ed->controller[1], 1);
		}
		if (deltaBits & FL_DELTA_CONTROLLER_2) {
			reader.read((void*)&ed->controller[2], 1);
		}
		if (deltaBits & FL_DELTA_CONTROLLER_3) {
			reader.read((void*)&ed->controller[3], 1);
		}
		if (deltaBits & FL_DELTA_BLENDING_0) {
			reader.read((void*)&ed->blending[0], 1);
		}
		if (deltaBits & FL_DELTA_BLENDING_1) {
			reader.read((void*)&ed->blending[1], 1);
		}
		if (deltaBits & FL_DELTA_SCALE) {
			reader.read((void*)&ed->scale, 1);
		}
		if (deltaBits & FL_DELTA_RENDERMODE) {
			reader.read((void*)&ed->rendermode, 1);
		}
		if (deltaBits & FL_DELTA_RENDERAMT) {
			reader.read((void*)&ed->renderamt, 1);
		}
		if (deltaBits & FL_DELTA_RENDERCOLOR_0) {
			reader.read((void*)&ed->rendercolor[0], 1);
		}
		if (deltaBits & FL_DELTA_RENDERCOLOR_1) {
			reader.read((void*)&ed->rendercolor[1], 1);
		}
		if (deltaBits & FL_DELTA_RENDERCOLOR_2) {
			reader.read((void*)&ed->rendercolor[2], 1);
		}
		if (deltaBits & FL_DELTA_RENDERFX) {
			reader.read((void*)&ed->renderfx, 1);
		}
		if (deltaBits & FL_DELTA_AIMENT) {
			reader.read((void*)&ed->aiment, 1);
		}

		// calculate instantaneous velocity. Will later be used to interpolate between previous/current states.
		netedict* lasted = &lastedicts[fullIndex];
		ed->velocity[0] = ed->origin[0] - lasted->origin[0];
		ed->velocity[1] = ed->origin[1] - lasted->origin[1];
		ed->velocity[2] = ed->origin[2] - lasted->origin[2];

		//println("Read index %d (%d bytes)", (int)fullIndex, (int)(reader.tell() - startPos));

		if (reader.eom()) {
			logf("ERROR: Invalid delta hit unexpected eom at %d\n", loop);
			memcpy(edicts, lastedicts, MAX_EDICTS * sizeof(netedict)); // rollback changes
			edicts_mutex.unlock();
			return false;
		}
	}

	return true;
}

void SvenTV::handleDeltaPacket(mstream& reader, const Packet& packet) {
	uint8_t offset;
	uint16_t fullIndex;
	uint32_t deltaBits;
	uint16_t updateId; // ID of the global update which this packet belongs
	uint16_t baselineId; // ID of the update we should be delta-ing from
	uint16_t fragmentId; // ID of this packet within the update, unique only to this update

	reader.read(&updateId, 2);
	reader.read(&baselineId, 2);
	reader.read(&fragmentId, 2);
	
	// TODO: check frame time, not baseline id, because this breaks on integer overflow
	if (baselineId > lastBaselineId) {
		bool isOldBaseline = baselineId < lastBaselineId;

		if (baselineId == 0) {
			logf("Set baseline to null state\n");
			memset(baselines, 0, MAX_EDICTS * sizeof(netedict)); // reset to null state
		}
		else {
			bool foundPreviousUpdate = false;

			for (int i = 0; i < receivedDeltas.size(); i++) {
				if (receivedDeltas[i].updateId != baselineId) {
					continue;
				}

				for (int k = 0; k < receivedDeltas[i].packets.size(); k++) {
					applyDelta(receivedDeltas[i].packets[k].packet, true);
				}

				//logf("Applied %d packets to create baseline %d\n", receivedDeltas[i].packets.size(), baselineId);
				lastBaselineId = baselineId;
				foundPreviousUpdate = true;
				break;
			}

			// anything with an updateId less than what the server is sending is no longer needed
			// the server will only ever use higher numbers as a new baseline
			// TODO: not when baselineId overflows
			int numDropped = 0;
			for (int i = 0; i < receivedDeltas.size(); i++) {
				if (receivedDeltas[i].updateId <= baselineId) {
					receivedDeltas.erase(receivedDeltas.begin() + i);
					i--;
					numDropped++;
				}
			}
			//logf("Deleted %d old delta updates\n", numDropped);

			if (!foundPreviousUpdate) {
				logf("Failed to create new baseline. Update %d not found. Asking for null baseline.\n", (int)updateId);
				uint8_t data = CLC_DELTA_RESET;
				memset(baselines, 0, MAX_EDICTS * sizeof(netedict));
				socket->send(Packet(&data, 1));
			}
		}
	} else if (baselineId < lastBaselineId) {
		// does the server not know we acked that baseline already?
		logf("Ignoring delta %d for old baseline %d and resending ack.\n", updateId, baselineId);
		socket->send(lastAck);
	}

	edicts_mutex.lock();
	bool applied = applyDelta(packet, false);
	edicts_mutex.unlock();

	if (!applied) {
		// Something went wrong. Don't let the server know we got this packet
		return;
	}

	lastDeltaTime = glfwGetTime();
	
	DeltaPacket deltaPacket;
	deltaPacket.fragmentId = fragmentId;
	deltaPacket.packet = Packet(packet); // TODO: not this. this does 2 extra copys

	int totalPackets = 0;
	bool didAppend = false;
	for (int i = 0; i < receivedDeltas.size(); i++) {
		if (receivedDeltas[i].updateId == updateId) {
			if (!receivedDeltas[i].acked) {
				// only append packets until an ack is sent
				// the server will assume we never got anything after the ack
				receivedDeltas[i].packets.push_back(deltaPacket);
			}
			totalPackets = receivedDeltas[i].packets.size();
			didAppend = true;
			//logf("Append DeltaUpdate to %d (now has %d packets)\n", (int)updateId, (int)receivedDeltas[i].packets.size());
			break;
		}
	}

	if (!didAppend) {
		totalPackets = 1;

		// new update started!
		// 
		// first ack the previous update
		if (receivedDeltas.size()) {
			uint16_t bestAck = 0;
			DeltaUpdate* bestUpdate = &receivedDeltas[0];

			for (int i = 0; i < receivedDeltas.size(); i++) {
				if (receivedDeltas[i].updateId > bestAck) {
					bestAck = receivedDeltas[i].updateId;
					bestUpdate = &receivedDeltas[i];
				}
			}

			// sort by fragment ID because UDP does not gauruntee order
			std::sort(bestUpdate->packets.begin(), bestUpdate->packets.end(), compareByFragmentId);

			// 2 byte updateID + bitfield of received packets
			int totalFrags = bestUpdate->packets[bestUpdate->packets.size() - 1].fragmentId + 1;
			int ackSize = 3 + ((totalFrags + 7) / 8);
			char* ackData = new char[ackSize];
			memset(ackData, 0, ackSize);
			mstream writer(ackData, ackSize);

			uint8_t packetType = CLC_DELTA_ACK;
			writer.write(&packetType, 1);
			writer.write(&bestUpdate->updateId, 2);

			int lastFragIdx = 0;
			int wroteBits = 0;
			for (int i = 0; i < bestUpdate->packets.size(); i++) {
				int fragId = bestUpdate->packets[i].fragmentId;

				// write 0s for any gaps (lost packets)
				for (int k = lastFragIdx+1; k < fragId; k++) {
					writer.writeBit(0);
					wroteBits++;
				}

				writer.writeBit(1);
				wroteBits++;
				lastFragIdx = fragId;
			}
			
			if (writer.eom()) {
				logf("WTFTFFFF %d\n", wroteBits);
			}

			//logf("Ack %d packets from update %d\n", bestUpdate->packets.size(), bestUpdate->updateId);
			bestUpdate->acked = true;
			lastAckId = bestUpdate->updateId;
			lastAck = Packet(ackData, ackSize);
			socket->send(lastAck);
		}

		//logf("Started Delta %d on baseline %d\n", updateId, baselineId);
		// push the new update
		DeltaUpdate update;
		update.updateId = updateId;
		update.acked = false;
		update.packets.push_back(deltaPacket);

		receivedDeltas.push_back(update);
		if (receivedDeltas.size() > 64) {
			receivedDeltas.erase(receivedDeltas.begin());
		}
	}

	//logf("Delta %5d -> %5d (%d packets), Acked %5d, History %d\n",
	//	(int)baselineId, (int)updateId, totalPackets, (int)lastAckId, (int)receivedDeltas.size());
}

void SvenTV::think() {
	initNet();
	socket = new Socket(SOCKET_UDP | SOCKET_NONBLOCKING, serverAddr);

	connect();

	Packet packet;

	while (!shouldKillThread) {
		if (socket->recv(packet)) {
			mstream reader(packet.data, packet.sz);
			uint8_t packetType;
			reader.read(&packetType, 1);

			if (packetType == SVC_DELTAPACKETENTITIES) {
				handleDeltaPacket(reader, packet);
			}
			else {
				logf("Unknown SvenTV packet type: %d\n", (int)packetType);
			}
		}
		else {
			this_thread::sleep_for(chrono::milliseconds(1));
		}
	}

	delete socket;
}