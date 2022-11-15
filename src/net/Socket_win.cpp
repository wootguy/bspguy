#include "util.h"
#include "Socket.h"
#include <ws2tcpip.h>
#include <WinSock2.h>

addrinfo tcphints = 
{
	AI_PASSIVE, // AI_PASSIVE
	PF_INET,
	SOCK_STREAM,  // datagram socket type
	IPPROTO_TCP, // TCP
	0, 0, 0, 0
};

addrinfo udphints = 
{
	AI_PASSIVE, // AI_PASSIVE
	PF_INET,
	SOCK_DGRAM,  // datagram socket type
	IPPROTO_UDP, // UDP
	0, 0, 0, 0
};

struct SocketData
{
	SOCKET sock;
	addrinfo * addr; // local socket address
	sockaddr_in dest; // destination address
};

SocketData * Socket::createSocket(const char * addr, const char * port)
{
	SocketData * skt = new SocketData;
	// Resolve the local address and port to be used by the server
	addrinfo * hints = (socketType & SOCKET_TCP) ? &tcphints : &udphints;
	int ret = getaddrinfo(addr, port, hints, &skt->addr);

	if (ret != 0)
	{
		logf("Socket creation failed at getaddrinfo(): %d\n", WSAGetLastError());
		delete skt;
		return NULL;
	}

	// create the socket
	skt->sock = socket(skt->addr->ai_family, skt->addr->ai_socktype, skt->addr->ai_protocol);

	if (skt->sock == INVALID_SOCKET)
	{
		logf("Socket creation failed at socket(): %d\n", WSAGetLastError());
		freeaddrinfo(skt->addr);
		delete skt;
		return NULL;
	}

	if (socketType & SOCKET_UDP) {
		BOOL bNewBehavior = FALSE;
		DWORD dwBytesReturned = 0;
		WSAIoctl(skt->sock, WSAECONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);
	}

	// set socket to non-blocking
	if (!(socketType & SOCKET_BLOCKING))
	{
		u_long iMode = 1;
		ret = ioctlsocket(skt->sock, FIONBIO, &iMode);
		if (ret == SOCKET_ERROR)
		{
			logf("Failed to set socket to non-blocking: %d\n", WSAGetLastError());
			freeaddrinfo(skt->addr);
			delete skt;
			return NULL;
		}
	}
	return skt;
}

Socket::Socket()
{
	socketType = 0;
	sendBytes = recvBytes = 0;
	SocketData * sock = new SocketData;
	memset(sock, 0, sizeof(SocketData));
	skt = sock;
}

Socket::Socket(int socketType, uint16_t port)
{
	this->socketType = socketType | SOCKET_SERVER;
	sendBytes = recvBytes = 0;
	skt = createSocket(NULL, to_string(port).c_str());
	if (skt != NULL)
	{
		if (bind())
			logf("Server socket created on port %d\n", port);
	}
}

Socket::Socket(int socketType, IPV4 addr)
{
	this->socketType = socketType & ~SOCKET_SERVER;	
	sendBytes = recvBytes = 0;
	SocketData * sock = createSocket(addr.getHostString().c_str(), NULL);
	skt = sock;
	if (sock != NULL)
	{
		memset(&sock->dest, 0, sizeof(sockaddr_in));
		sock->dest.sin_family = AF_INET;
		sock->dest.sin_addr.s_addr = inet_addr( addr.getHostString().c_str() );
		sock->dest.sin_port = htons(addr.port);
		logf("Client socket created on %s:%d\n", inet_ntoa(sock->dest.sin_addr), (int)ntohs(sock->dest.sin_port) );
	}
}

Socket::~Socket(void)
{
	SocketData * sock = (SocketData*)skt;
	if (skt != NULL)
	{
		closesocket(sock->sock);
		if (sock->addr != NULL)
			freeaddrinfo(sock->addr);
		delete sock;
	}
}

bool Socket::connect(uint32_t timeout)
{
	if (!(socketType & SOCKET_TCP))
	{
		logf("Can't connect() with a UDP socket.\n");
		return false;
	}
	if (skt == NULL)
	{
		logf("Can't connect(). Invalid socket!\n");
		return false;
	}

	SocketData * sock = (SocketData*)skt;
	int ret = ::connect(sock->sock, (const sockaddr*)&sock->dest, sizeof(sock->dest));
	if (ret)
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
		{
			fd_set set;
			set.fd_array[0] = sock->sock;
			set.fd_count = 1;
			timeval tv;
			tv.tv_sec = (timeout / 1000);
			tv.tv_usec = (timeout % 1000)*1000;
			ret = ::select(NULL, &set, &set, NULL, &tv);

			if (ret == 0)
			{
				logf("Socket connection timed out\n");
				return false;
			}
			else if (ret == SOCKET_ERROR)
			{
				logf("Failed to connect socket: %d\n", err);
				return false;
			}
		}
		else if (err == WSAEISCONN) {
			return true; // already connected
		}
		else
		{
			logf("Failed to connect socket: %d\n", err);
			return false;
		}
	}
	return true;
}

Socket * Socket::accept()
{
	if (!(socketType & SOCKET_TCP))
	{
		logf("Can't accept() with a UDP socket.\n");
		return NULL;
	}
	if (skt == NULL)
	{
		logf("Can't accept(). Invalid socket!\n");
		return NULL;
	}
	SocketData * sock = (SocketData*)skt;
	sockaddr_in newAddr;
	int len = sizeof(sockaddr_in);
	SOCKET newSock = ::accept(sock->sock, (sockaddr*)&newAddr, &len);
	if (newSock == INVALID_SOCKET)
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
			return NULL;

		logf("Socket accept failed: %d\n", err);
		return NULL;
	}

	Socket * newClient = new Socket();
	newClient->socketType = this->socketType;
	SocketData* newData = (SocketData*)newClient->skt;
	newData->addr = sock->addr;
	newData->dest = newAddr;
	newData->sock = newSock;

	return newClient;
}

// only called for server sockets
bool Socket::bind()
{
	if (skt == NULL)
	{
		logf("Can't bind. Invalid socket!\n");
		return false;
	}
	SocketData * sock = (SocketData*)skt;

	int iResult = ::bind( sock->sock, sock->addr->ai_addr, (int)sock->addr->ai_addrlen);

	if (iResult == SOCKET_ERROR) 
	{
		logf("Socket bind failed with error: %d\n", WSAGetLastError());
		closesocket(sock->sock);
		sock->sock = INVALID_SOCKET;
		return false;
	}

	if (socketType & SOCKET_TCP)
	{
		iResult = listen(sock->sock, SOMAXCONN);
		if (iResult == SOCKET_ERROR)
		{
			logf("Socket listen failed with error: %d\n", WSAGetLastError());
			closesocket(sock->sock);
			sock->sock = INVALID_SOCKET;
			return false;
		}
	}
	return true;
}

bool Socket::recv(Packet& p)
{
	p.data = NULL;
	p.sz = -1;

	if (skt == NULL)
	{
		logf("Can't receive. Invalid socket!\n");
		return false;
	}
	SocketData * sock = (SocketData*)skt;
	sockaddr_in addr = sock->dest;
	int addrLen = sizeof(addr);
	char dat[MAX_PACKET_SIZE];
	int packetLen;

	if (socketType & SOCKET_TCP) 
		packetLen = ::recv(sock->sock, dat, MAX_PACKET_SIZE, 0);
	else                         
		packetLen = ::recvfrom(sock->sock, dat, MAX_PACKET_SIZE, 0, (sockaddr*)&addr, &addrLen);

	if (packetLen == 0)
		return false;
	if (packetLen == -1)
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
			return false;
		logf("Socket recv failed with error: %d\n", err);
		return false;
	}

	byte b1 = addr.sin_addr.S_un.S_un_b.s_b1;
	byte b2 = addr.sin_addr.S_un.S_un_b.s_b2;
	byte b3 = addr.sin_addr.S_un.S_un_b.s_b3;
	byte b4 = addr.sin_addr.S_un.S_un_b.s_b4;
	IPV4 sender(b1, b2, b3, b4, ntohs(addr.sin_port));

	if (p.data != NULL)
		delete [] p.data;
	p.addr = sender;
	p.data = new char[packetLen];
	memcpy(p.data, dat, packetLen);
	p.sz = packetLen;
	recvBytes += packetLen + (socketType & SOCKET_TCP) ? IPV4_TCP_HEADER_SIZE : IPV4_UDP_HEADER_SIZE;
	return true;
}

bool Socket::send( const Packet& p )
{
	if (skt == NULL)
	{
		logf("Can't send. Invalid socket!\n");
		return false;
	}
	if (p.sz > MAX_PACKET_SIZE)
	{
		logf("Packet too big to send! (%d > %d)\n", p.sz, MAX_PACKET_SIZE);
		return false;
	}

	SocketData * sock = (SocketData*)skt;
	sockaddr_in addr;
	if (!p.addr.isEmpty())
	{
		string saddr = p.addr.getHostString();
		memset(&addr.sin_zero, 0, sizeof(addr.sin_zero));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr( saddr.c_str() );
		addr.sin_port = htons(p.addr.port);
	}
	else
		addr = sock->dest;

	int ret;
	if (socketType & SOCKET_TCP) ret = ::send(sock->sock, p.data, p.sz, 0);
	else						 ret = sendto(sock->sock, p.data, p.sz, 0, (sockaddr*)&addr, sizeof(addr));

	if (ret == 0)
		return false;
	if ( ret < 0 )
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
			return false;
		logf("Socket send failed with error: %d\n", err);
		return false;
	}
	sendBytes += ret + (socketType & SOCKET_TCP) ? IPV4_TCP_HEADER_SIZE : IPV4_UDP_HEADER_SIZE;
	return true;
}

bool Socket::send(const void * dat, int sz )
{
	return send(Packet(dat, sz));
}
