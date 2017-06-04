#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#define SERVERPORT 9000
#define BUFSIZE    256
#define NAMESIZE   20
#define ROOMCHECK  64
#define ROOM1      49
#define ROOM2      50
#define CHATTING   1000                   // �޽��� Ÿ��: ä��
#define MSGSIZE     (BUFSIZE-sizeof(int))  // ä�� �޽��� �ִ� ����
#define SHOWUSERS  "#!ShowUser"
// ���� �޽��� ����
// sizeof(COMM_MSG) == 256
struct CHAT_MSG
{
	int  type;
	char buf[MSGSIZE];
};

// ���� ���� ������ ���� ����ü�� ����
struct SOCKETINFO
{
	SOCKET sock;
	char   buf[BUFSIZE];
	int    recvbytes;
	int	   room;
	char   name[NAMESIZE];
	int    ID;
};

bool loginCheck = false;
bool showUsersCheck = false;
bool oneTonOneCheck = false;
int nTotalSockets = 0;
int userID;
static CHAT_MSG      g_chatmsg; // ä�� �޽��� ����
SOCKETINFO *SocketInfoArray[FD_SETSIZE];

// ���� ���� �Լ�
BOOL AddSocketInfo(SOCKET sock);
void RemoveSocketInfo(int nIndex);

#pragma region ErrorFunc
// ���� �Լ� ���� ��� �� ����
void err_quit(char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}
// ���� �Լ� ���� ���
void err_display(char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}
#pragma endregion


bool checkSameNameUser(SOCKETINFO *ptr, int retval) {
	for (int i = 0; i < nTotalSockets; i++) {
		SOCKETINFO *ptr3 = SocketInfoArray[i];
		if (!strcmp(ptr3->name, ptr->name) && ptr3->room == ptr->room && ptr3->ID != ptr->ID) {
			sprintf(g_chatmsg.buf, "���� �̸��� �����ڰ� �ֽ��ϴ�. �г����� �ٲ��ּ���", g_chatmsg.buf, ptr3->name);
			retval = send(ptr->sock, (char *)&g_chatmsg, BUFSIZE, 0);
			RemoveSocketInfo(nTotalSockets - 1);
			return true;
		}
	}
	return false;
}

int main(int argc, char *argv[])
{
	int retval;
	// ���� �ʱ�ȭ(�Ϻ�)
	g_chatmsg.type = CHATTING;
	// ���� �ʱ�ȭ
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	/*----- IPv4 ���� �ʱ�ȭ ���� -----*/
	// socket()
	SOCKET listen_sockv4 = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sockv4 == INVALID_SOCKET) err_quit("socket()");

	// bind()
	SOCKADDR_IN serveraddrv4;
	ZeroMemory(&serveraddrv4, sizeof(serveraddrv4));
	serveraddrv4.sin_family = AF_INET;
	serveraddrv4.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddrv4.sin_port = htons(SERVERPORT);
	retval = bind(listen_sockv4, (SOCKADDR *)&serveraddrv4, sizeof(serveraddrv4));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	// listen()
	retval = listen(listen_sockv4, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit("listen()");
	/*----- IPv4 ���� �ʱ�ȭ �� -----*/

	// ������ ��ſ� ����� ����(����)
	FD_SET rset;
	SOCKET client_sock;
	int addrlen, i, j;
	// ������ ��ſ� ����� ����(IPv4)
	SOCKADDR_IN clientaddrv4;

	while (1) {
		// ���� �� �ʱ�ȭ
		FD_ZERO(&rset);
		FD_SET(listen_sockv4, &rset);
		for (i = 0; i < nTotalSockets; i++) {
			FD_SET(SocketInfoArray[i]->sock, &rset);
		}

		// select()
		retval = select(0, &rset, NULL, NULL, NULL);
		if (retval == SOCKET_ERROR) {
			err_display("select()");
			break;
		}

		// ���� �� �˻�(1): Ŭ���̾�Ʈ ���� ����
		if (FD_ISSET(listen_sockv4, &rset)) {
			addrlen = sizeof(clientaddrv4);
			client_sock = accept(listen_sockv4, (SOCKADDR *)&clientaddrv4, &addrlen);
			if (client_sock == INVALID_SOCKET) {
				err_display("accept()");
				break;
			}
			else {
				// ������ Ŭ���̾�Ʈ ���� ���
				printf("[TCPv4 ����] Ŭ���̾�Ʈ ����: [%s]:%d\n",
					inet_ntoa(clientaddrv4.sin_addr), ntohs(clientaddrv4.sin_port));
				// ���� ���� �߰�
				AddSocketInfo(client_sock);
			}
		}

		char tempBuf[BUFSIZE + 1];
		char *splitBuf[2] = { NULL };

		// ���� �� �˻�(2): ������ ���
		for (i = 0; i < nTotalSockets; i++) {
			SOCKETINFO *ptr = SocketInfoArray[i];
			if (FD_ISSET(ptr->sock, &rset)) {
				// ������ �ޱ�
				retval = recv(ptr->sock, ptr->buf + ptr->recvbytes,
					BUFSIZE - ptr->recvbytes, 0);
				if (retval == 0 || retval == SOCKET_ERROR) {
					RemoveSocketInfo(i);
					continue;
				}

				// ���� ����Ʈ �� ����
				ptr->recvbytes += retval;

				// Login�� ��� �Ǻ��ϱ� ���� tempBuf�� User�ʱ�ȭ ���� ����(Room,Name)
				strncpy(tempBuf, ptr->buf + 4, BUFSIZE-4);
				// User�� ó�� ������ ��� room �ʱ�ȭ 
				if (tempBuf[1] == ROOMCHECK)
				{
					char *splitChar = strtok(tempBuf, "@");
					for (int i = 0; i < 2; i++) {
						splitBuf[i] = splitChar;
						splitChar = strtok(NULL, "@");
					}

					if (tempBuf[0] == ROOM1)
						ptr->room = 1;
					else if (tempBuf[0] == ROOM2)
						ptr->room = 2;

					strcpy(ptr->name, splitBuf[1]);
				}
				// User �����޶� ��û���� ���
				else if (!strcmp(tempBuf, SHOWUSERS))
				{
					sprintf(g_chatmsg.buf, "���� ������ User�Դϴ�.\n");
					showUsersCheck = true;
				}
				// 1:1 �ӼӸ� üũ�ϱ�
				else if (tempBuf[strlen(tempBuf) - 1] == '!'&& tempBuf[strlen(tempBuf) - 2] == '@'
					&& tempBuf[strlen(tempBuf) - 3] == '#'  && tempBuf[strlen(tempBuf) - 4] == '$'
					&& tempBuf[strlen(tempBuf) - 5] == '!')
				{
					oneTonOneCheck = true;
				}

					if (ptr->recvbytes == BUFSIZE) {
						// ���� ����Ʈ �� ����
						ptr->recvbytes = 0;

						// ���� ������ ��� Ŭ���̾�Ʈ���� �����͸� ����!
						for (j = 0; j < nTotalSockets; j++) {
							SOCKETINFO *ptr2 = SocketInfoArray[j];

							if (showUsersCheck == true) {
								sprintf(g_chatmsg.buf, "%s %s", g_chatmsg.buf, ptr2->name);
								if (j == nTotalSockets - 1) {
									retval = send(ptr->sock, (char *)&g_chatmsg, BUFSIZE, 0);
								}
								continue;
							}
							// ���� ���� ��쿡�� ����Ÿ ����
							else if (ptr->room == ptr2->room) {
								// Client�� ó�� ä�ù濡 ������ ���
								if (loginCheck == true) {
									//
									if (checkSameNameUser(ptr, retval)) {
										break;
									}
									sprintf(g_chatmsg.buf, "�г��� %s���� ä�ù�%d�� %s", ptr->name, ptr->room, "�����ϼ̽��ϴ�!");
									retval = send(ptr2->sock, (char *)&g_chatmsg, BUFSIZE, 0);
								}
								else {
									sprintf(g_chatmsg.buf, "%s : %s", ptr->name, tempBuf);
									retval = send(ptr2->sock, (char *)&g_chatmsg, BUFSIZE, 0);
								}
								if (retval == SOCKET_ERROR) {
									err_display("send()");
									RemoveSocketInfo(j);
									--j; // ���� �ε��� ����
									continue;
								}
							}
						}
						loginCheck = false;
						showUsersCheck = false;
					}
			}
		}
	}

	return 0;
}

// ���� ���� �߰�
BOOL AddSocketInfo(SOCKET sock)
{
	if (nTotalSockets >= FD_SETSIZE) {
		printf("[����] ���� ������ �߰��� �� �����ϴ�!\n");
		return FALSE;
	}

	SOCKETINFO *ptr = new SOCKETINFO;
	if (ptr == NULL) {
		printf("[����] �޸𸮰� �����մϴ�!\n");
		return FALSE;
	}
	//�ð��� ���� ���� UserID�� ����
	srand(time(NULL));
	userID = rand();

	ptr->sock = sock;
	ptr->recvbytes = 0;
	ptr->ID = userID;
	SocketInfoArray[nTotalSockets++] = ptr;
	loginCheck = true;


	return TRUE;
}

// ���� ���� ����
void RemoveSocketInfo(int nIndex)
{
	SOCKETINFO *ptr = SocketInfoArray[nIndex];

	// ������ Ŭ���̾�Ʈ ���� ���
	SOCKADDR_IN clientaddrv4;
	int addrlen = sizeof(clientaddrv4);
	getpeername(ptr->sock, (SOCKADDR *)&clientaddrv4, &addrlen);
	printf("[TCPv4 ����] Ŭ���̾�Ʈ ����: [%s]:%d\n",
		inet_ntoa(clientaddrv4.sin_addr), ntohs(clientaddrv4.sin_port));

	closesocket(ptr->sock);
	delete ptr;

	if (nIndex != (nTotalSockets - 1))
		SocketInfoArray[nIndex] = SocketInfoArray[nTotalSockets - 1];

	--nTotalSockets;
}

