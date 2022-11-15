#include "IPV4.h"
#include <stdlib.h>
#include "util.h"

using namespace std;

IPV4::IPV4() : b1(0), b2(0), b3(0), b4(0), port(0) {}

IPV4::IPV4(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint16_t port)
	: b1(b1), b2(b2), b3(b3), b4(b4), port(port) {}


IPV4::IPV4(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4 )
	: b1(b1), b2(b2), b3(b3), b4(b4), port(0) {}


int parseOctet(string& s)
{
	int dot = s.find_first_of(".");
	if (dot == -1)
		return -1;
	int octet = atoi(s.substr(0,dot).c_str());
	s = s.substr(dot+1);
	return octet;
}

IPV4::IPV4( const char * addr )
{
	b1 = b2 = b3 = b4 = 0;
	port = 0;
	string s = addr;

	if (s.size() == 0)
	{
		b1 = b2 = b3 = b4 = port = 0;
		return;
	}

	int o1 = parseOctet(s);
	int o2 = parseOctet(s);
	int o3 = parseOctet(s);

	if (o1 == -1 || o2 == -1 || o3 == -1)
	{
		logf("error parsing IPV4 string: %s", addr);
		b1 = b2 = b3 = b4 = port = 0;
		return;
	}

	b1 = o1;
	b2 = o2;
	b3 = o3;
	
	int colon = s.find_first_of(":");
	if (colon != -1)
	{
		b4 = atoi(s.substr(0, colon).c_str());
		s = s.substr(colon+1);
		port = atoi(s.c_str());
	}
	else
		b4 = atoi(s.c_str());
}

string IPV4::getString() const
{
	return getHostString() + ":" + to_string(port);
}

string IPV4::getHostString() const
{
	return to_string(b1) + "." + to_string(b2) + "." + to_string(b3) + "." + to_string(b4);
}

bool IPV4::isEmpty() const
{
	return !b1 && !b2 && !b3 && !b4 && !port;
}

bool operator==( IPV4 a, IPV4 b )
{
	return a.b1 == b.b1 && 
		   a.b2 == b.b2 && 
		   a.b3 == b.b3 && 
		   a.b4 == b.b4 && 
		   a.port == b.port;
}

bool operator!=( IPV4 a, IPV4 b )
{
	return a.b1 != b.b1 || 
		   a.b2 != b.b2 || 
		   a.b3 != b.b3 || 
		   a.b4 != b.b4 || 
		   a.port != b.port;
}
