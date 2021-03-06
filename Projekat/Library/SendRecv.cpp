#include "header.h"

rSocket* rInitialize()
{
	int iResult = 0;		/* Flag greske */	

	/* Inicijalizuje WSA */
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup failed with error: %d\n", WSAGetLastError());
		return NULL;
	}
	
	/* Zauzimanje memorije za rSocket strukturu */
	rSocket* s;
	s = (rSocket*)malloc(sizeof(rSocket));
	if (s == NULL)
		return NULL;

	/* SOCKET je za UDP */
	s->socket = socket(AF_INET,
		SOCK_DGRAM,
		IPPROTO_UDP);

	/* Zauzimanje memorije za adresu klijenta/servera */
	s->adresa = (sockaddr_in*)calloc(1, sizeof(sockaddr_in));
	if (s->adresa == NULL)
		return NULL;

	/* LOCK za rSocket */
	s->lock = CreateSemaphore(0, 1, 1, NULL);

	/* Podaci za algoritam */
	s->cwnd = INITIAL_CWND;
	s->ssthresh = 0;
	s->recv = 0;
	s->slowstart = true;
	
	/* Inicijalizacija buffera */
	s->sendBuffer = (Kruzni_Buffer*)malloc(2 * sizeof(Kruzni_Buffer));
	rInitBuffer(s->sendBuffer, MAX_BUFFER_SIZE);
	if (s->sendBuffer->buffer_start == NULL)
		return NULL;
	s->recvBuffer = s->sendBuffer + 1;
	rInitBuffer(s->recvBuffer, MAX_BUFFER_SIZE);
	if (s->recvBuffer->buffer_start == NULL)
		return NULL;

	/* Pokretanje Threadova */
	s->activeThreads = true;
	s->sendThread = (HANDLE*)malloc(2* sizeof(HANDLE));
	s->recvThread = s->sendThread + 1;
	*(s->sendThread) = CreateThread(NULL, 0, &SendThread, s, 0, NULL);
	*(s->recvThread) = CreateThread(NULL, 0, &RecvThread, s, 0, NULL);
	
	/* Inicijalizacija pomocnih podataka */
	s->brojPoslednjePoslatih = 0;
	s->idPoslednjePoslato = 0;
	s->idOcekivanog = 1;
	s->timedOut = false;
	s->canSend = true;
	s->state = DISCONNECTED;
	s->sockAddrLen = sizeof(sockaddr_in);

	return s;
}

int rDeinitialize(rSocket* s)
{	
	/* Gasenje threadova */
	WaitForSingleObject(s->lock, INFINITE);
	s->activeThreads = false;
	ReleaseSemaphore(s->lock, 1, NULL);

	/* Ceka da recv thread zavrsi timeout */
	WaitForSingleObject(&(s->recvThread), TIMEOUT_SEC);
	
	/* Oslobadjanje buffera */
	rFreeBuffer(s->sendBuffer);
	rFreeBuffer(s->recvBuffer);

	/* Oslobadjanje struktura za buffere i threadove */
	free(s->sendBuffer);
	free(s->sendThread);

	free(s->adresa);

	/* Zatvaranje WSA */
	if (WSACleanup() == SOCKET_ERROR)
	{
		printf("closesocket failed with error: %ld\n", WSAGetLastError());
		return 1;
	}

	/* Ciscenje rSocket strukture */
	free(s);

	return 0;
}

int rConnect(rSocket* s, char* serverAddress, short port)
{
	WaitForSingleObject(s->lock, INFINITE);
	int iResult;		/* Flag greske */	
	
	/* Podesavanje adrese servera */
	(s->adresa)->sin_family = AF_INET;
	(s->adresa)->sin_addr.s_addr = inet_addr(serverAddress);
	(s->adresa)->sin_port = htons((u_short)port);

	/* Podesavanje Timeout-a */
	DWORD timeout = TIMEOUT_SEC;
	setsockopt(s->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	/* Slanje REQUEST-a */
	rMessageHeader header;
	header.type = REQUEST;
	header.id = 0;
	header.size = 0;

	iResult = sendto(s->socket, (char*)&header, sizeof(rMessageHeader), 0, (LPSOCKADDR)s->adresa, s->sockAddrLen);
	if (iResult == SOCKET_ERROR)
	{
		printf("//recvfrom failed with error: %d\n", WSAGetLastError());
		return -1;
	}

	ReleaseSemaphore(s->lock, 1, NULL);

	return 0;
}

int rAccept(rSocket* s, short port)
{
	WaitForSingleObject(s->lock, INFINITE);

	/* Podesavanje adrese servera */
	s->adresa->sin_family = AF_INET;
	s->adresa->sin_addr.s_addr = INADDR_ANY;
	s->adresa->sin_port = htons((u_short)port);

	/* Bind adrese i socketa */
	bind(s->socket, (LPSOCKADDR)s->adresa, s->sockAddrLen);

	/* Podesavanje Timeout-a */
	DWORD timeout = TIMEOUT_SEC;
	setsockopt(s->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	ReleaseSemaphore(s->lock, 1, NULL);

	return 0;
}

int rSend(rSocket* s, char* data, int len)
{
	WaitForSingleObject(s->lock, INFINITE);

	int iResult;		/* Flag greske */

	/* Ceka dok se ne uspostavi konekcija */
	while (s->state == DISCONNECTED)
	{
		ReleaseSemaphore(s->lock, 1, NULL);
		Sleep(100);
		WaitForSingleObject(s->lock, INFINITE);
		continue;
	}

	/* Povecava send buffer dokle god nije dovoljno velik da primi poruku */
	while (s->sendBuffer->free < len)
	{
		iResult = rResize(s->sendBuffer, (s->sendBuffer->buffer_end - s->sendBuffer->buffer_start) * 2);
	}

	/* Stavlja poruku na send buffer */
	iResult = rPush(s->sendBuffer, data, len);
	if (iResult != len)
	{
		printf("nije pushovano na buffer");
		return -1;
	}

	ReleaseSemaphore(s->lock, 1, NULL);

	return 0;
}

int rRecv(rSocket* s, char* data, int len)
{
	WaitForSingleObject(s->lock, INFINITE);

	int iResult;		/* Flag greske */

	/* Pokusava da preuzme sve sa buffera */
	do {
		iResult = rRead(s->recvBuffer, data, len);
		ReleaseSemaphore(s->lock, 1, NULL);
		Sleep(100);
		WaitForSingleObject(s->lock, INFINITE);
	} while (iResult != len);

	/* Brise preuzete podatke sa buffera */
	printf("Preuzeto %d sa buffera", iResult);
	rDelete(s->recvBuffer, len);

	ReleaseSemaphore(s->lock, 1, NULL);

	return iResult;
}