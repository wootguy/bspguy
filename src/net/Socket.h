#pragma once
#include "Packet.h"

class IPV4;

bool initNet();
void netStop();
IPV4 getLocalIP();

// IP headers are AT LEAST 20/40 bytes, maybe more
// UDP headers are always 8 bytes
#define IPV4_UDP_HEADER_SIZE (20+8)
#define IPV6_UDP_HEADER_SIZE (48+8)
#define IPV4_TCP_HEADER_SIZE (36+8)
#define IPV6_TCP_HEADER_SIZE (64+8)

// Socket flags
#define SOCKET_UDP 0
#define SOCKET_TCP 1
#define SOCKET_NONBLOCKING 0
#define SOCKET_BLOCKING 2
#define SOCKET_SERVER 4 // Set/cleared automatically when socket is created

// platform-specific socket data
struct SocketData;

class Socket
{
public:
	// keeps track of throughput on this socket
	uint32_t sendBytes;
	uint32_t recvBytes;

	// creates an invalid socket
	Socket();

	// creates and binds a server socket
	Socket(int socketType, uint16_t port);

	// creates a client socket
	Socket(int socketType, IPV4 addr);

	// closes the connection
	~Socket(void);

	// returns false if no packets were received
	bool recv(Packet& p);

	// Servers should specify an address for the packet
	// and clients should leave it blank
	bool send(const Packet& p);

	// Only clients should use this information since
	// no address is specified
	bool send(const void * dat, int sz);

	inline bool isServer() { return socketType & SOCKET_SERVER; }
	inline bool isTCP() { return socketType & SOCKET_TCP; }
	inline bool isBlocking() { return socketType & SOCKET_BLOCKING; }

	// create the TCP connection to the server
	// timeout is given in milliseconds.
	bool connect(uint32_t timeout);

	// accepts a TCP connection on this socket
	// Returns NULL if there are no pending connections
	Socket * accept();

private:
	void * skt; // platform-specific data
	int socketType;

	// Lets the server start receiving packets
	bool bind();

	// creates a platform-specific socket
	SocketData * createSocket(const char * addr, const char * port);
};