#include "Packet.h"
#include <string.h>
#include "util.h"

using namespace std;

uint32_t g_id;

Packet::Packet(const Packet& other) {
	this->addr = other.addr;
	this->sz = other.sz;
	this->data = new char[sz];
	memcpy(this->data, other.data, sz);
	id = g_id++;
}

Packet& Packet::operator=(const Packet& other) {
	this->addr = other.addr;
	this->sz = other.sz;
	this->data = new char[sz];
	memcpy(this->data, other.data, sz);
	return *this;
}

Packet::Packet(const string& message)
{
	init(message);
	id = g_id++;
}

Packet::Packet(const string& message, IPV4 addr) : addr(addr)
{
	init(message);
	id = g_id++;
}

Packet::Packet(IPV4 addr, void * data, int sz) : addr(addr), sz(sz)
{
	this->data = new char[sz];
	memcpy(this->data, data, sz);
	id = g_id++;
}

Packet::Packet(const void * data, int sz) : sz(sz)
{
	this->data = new char[sz];
	memcpy(this->data, data, sz);
	id = g_id++;
}

Packet::Packet()
{
	data = NULL;
	sz = -1;
	id = g_id++;
}

Packet::~Packet()
{
	if (data != NULL)
		delete [] data;
	data = NULL;
}

string Packet::getString() {
	char* outDat = new char[sz + 1];
	memcpy(outDat, data, sz);
	outDat[sz] = 0;

	string outStr = outDat;
	delete[] outDat;

	return outStr;
}

void Packet::init(const string& message)
{
	sz = message.size();
	data = new char[sz+1];
	memcpy(data, (char*)&message[0], sz);
	data[sz] = 0;
	sz++;
}