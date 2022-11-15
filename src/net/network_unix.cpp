#include "Socket.h"
#include "IPV4.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ifaddrs.h>

bool initNet()
{
	return true;
}

void netStop()
{
	
}

IPV4 getLocalIP()
{
	struct ifaddrs *ifaddr;

	if (getifaddrs(&ifaddr) == -1) 
	{
		println("[Radio] getifaddrs() failed");
		return IPV4();
	}

	bool found = false;
	IPV4 out;

	struct ifaddrs *ifa = ifaddr;
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
	{
		if (ifa->ifa_addr != NULL) 
		{
			int family = ifa->ifa_addr->sa_family;
			if (family == AF_INET)// || family == AF_INET6) 
			{
				char ip_addr[NI_MAXHOST];
				int s = getnameinfo(ifa->ifa_addr, ((family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)), ip_addr, sizeof(ip_addr), NULL, 0, NI_NUMERICHOST);
				if (s != 0) 
				{
					println("[Radio] getnameinfo() failed: %s", gai_strerror(s));
					freeifaddrs(ifaddr);
					return IPV4();
				} 
				else 
				{
					//print(ifa->ifa_name + " " + ip_addr);
					IPV4 addr(ip_addr);
					if (addr == IPV4(127,0,0,1))
						continue;
					if (found)
					{
						println("[Radio] Multiple local ip addresses found!");
						continue;
					}
					out = addr;
				}
			} 
		}
	}

	return out;
}