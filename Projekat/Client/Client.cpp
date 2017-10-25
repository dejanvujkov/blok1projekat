#include "Client.h"

// UDP client that uses blocking sockets
int main(int argc, char* argv[])
{
	char* buffer;
	buffer = (char*)malloc(100000000);
	memset(buffer, 77, 100000000);

	WSADATA wsaData;
	int iResult = 0;
	// Initialize windows sockets library for this process
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup failed with error: %d\n", WSAGetLastError());
		return 1;
	}

	rSocket socket;
	socket.addr = SERVER_IP_ADDERESS;
	socket.port = SERVER_PORT;

	Send(socket, buffer, 100000000);

	iResult = WSACleanup();
	if (iResult == SOCKET_ERROR)
	{
		printf("closesocket failed with error: %ld\n", WSAGetLastError());
		return 1;
	}

	return 0;
}