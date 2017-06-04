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
#define CHATTING   1000                   // 메시지 타입: 채팅
#define MSGSIZE     (BUFSIZE-sizeof(int))  // 채팅 메시지 최대 길이
#define SHOWUSERS  "#!ShowUser"
// 공통 메시지 형식
// sizeof(COMM_MSG) == 256
struct CHAT_MSG
{
	int  type;
	char buf[MSGSIZE];
};

// 소켓 정보 저장을 위한 구조체와 변수
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
static CHAT_MSG      g_chatmsg; // 채팅 메시지 저장
SOCKETINFO *SocketInfoArray[FD_SETSIZE];

// 소켓 관리 함수
BOOL AddSocketInfo(SOCKET sock);
void RemoveSocketInfo(int nIndex);

#pragma region ErrorFunc
// 소켓 함수 오류 출력 후 종료
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
// 소켓 함수 오류 출력
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
			sprintf(g_chatmsg.buf, "같은 이름의 접속자가 있습니다. 닉네임을 바꿔주세요", g_chatmsg.buf, ptr3->name);
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
	// 변수 초기화(일부)
	g_chatmsg.type = CHATTING;
	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	/*----- IPv4 소켓 초기화 시작 -----*/
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
	/*----- IPv4 소켓 초기화 끝 -----*/

	// 데이터 통신에 사용할 변수(공통)
	FD_SET rset;
	SOCKET client_sock;
	int addrlen, i, j;
	// 데이터 통신에 사용할 변수(IPv4)
	SOCKADDR_IN clientaddrv4;

	while (1) {
		// 소켓 셋 초기화
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

		// 소켓 셋 검사(1): 클라이언트 접속 수용
		if (FD_ISSET(listen_sockv4, &rset)) {
			addrlen = sizeof(clientaddrv4);
			client_sock = accept(listen_sockv4, (SOCKADDR *)&clientaddrv4, &addrlen);
			if (client_sock == INVALID_SOCKET) {
				err_display("accept()");
				break;
			}
			else {
				// 접속한 클라이언트 정보 출력
				printf("[TCPv4 서버] 클라이언트 접속: [%s]:%d\n",
					inet_ntoa(clientaddrv4.sin_addr), ntohs(clientaddrv4.sin_port));
				// 소켓 정보 추가
				AddSocketInfo(client_sock);
			}
		}

		char tempBuf[BUFSIZE + 1];
		char *splitBuf[2] = { NULL };

		// 소켓 셋 검사(2): 데이터 통신
		for (i = 0; i < nTotalSockets; i++) {
			SOCKETINFO *ptr = SocketInfoArray[i];
			if (FD_ISSET(ptr->sock, &rset)) {
				// 데이터 받기
				retval = recv(ptr->sock, ptr->buf + ptr->recvbytes,
					BUFSIZE - ptr->recvbytes, 0);
				if (retval == 0 || retval == SOCKET_ERROR) {
					RemoveSocketInfo(i);
					continue;
				}

				// 받은 바이트 수 누적
				ptr->recvbytes += retval;

				// Login일 경우 판별하기 위해 tempBuf에 User초기화 내용 저장(Room,Name)
				strncpy(tempBuf, ptr->buf + 4, BUFSIZE-4);
				// User가 처음 들어왔을 경우 room 초기화 
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
				// User 보여달라 요청했을 경우
				else if (!strcmp(tempBuf, SHOWUSERS))
				{
					sprintf(g_chatmsg.buf, "현재 접속한 User입니다.\n");
					showUsersCheck = true;
				}
				// 1:1 귓속말 체크하기
				else if (tempBuf[strlen(tempBuf) - 1] == '!'&& tempBuf[strlen(tempBuf) - 2] == '@'
					&& tempBuf[strlen(tempBuf) - 3] == '#'  && tempBuf[strlen(tempBuf) - 4] == '$'
					&& tempBuf[strlen(tempBuf) - 5] == '!')
				{
					oneTonOneCheck = true;
				}

					if (ptr->recvbytes == BUFSIZE) {
						// 받은 바이트 수 리셋
						ptr->recvbytes = 0;

						// 현재 접속한 모든 클라이언트에게 데이터를 보냄!
						for (j = 0; j < nTotalSockets; j++) {
							SOCKETINFO *ptr2 = SocketInfoArray[j];

							if (showUsersCheck == true) {
								sprintf(g_chatmsg.buf, "%s %s", g_chatmsg.buf, ptr2->name);
								if (j == nTotalSockets - 1) {
									retval = send(ptr->sock, (char *)&g_chatmsg, BUFSIZE, 0);
								}
								continue;
							}
							// 방이 같은 경우에만 데이타 전송
							else if (ptr->room == ptr2->room) {
								// Client가 처음 채팅방에 접속한 경우
								if (loginCheck == true) {
									//
									if (checkSameNameUser(ptr, retval)) {
										break;
									}
									sprintf(g_chatmsg.buf, "닉네임 %s님이 채팅방%d에 %s", ptr->name, ptr->room, "접속하셨습니다!");
									retval = send(ptr2->sock, (char *)&g_chatmsg, BUFSIZE, 0);
								}
								else {
									sprintf(g_chatmsg.buf, "%s : %s", ptr->name, tempBuf);
									retval = send(ptr2->sock, (char *)&g_chatmsg, BUFSIZE, 0);
								}
								if (retval == SOCKET_ERROR) {
									err_display("send()");
									RemoveSocketInfo(j);
									--j; // 루프 인덱스 보정
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

// 소켓 정보 추가
BOOL AddSocketInfo(SOCKET sock)
{
	if (nTotalSockets >= FD_SETSIZE) {
		printf("[오류] 소켓 정보를 추가할 수 없습니다!\n");
		return FALSE;
	}

	SOCKETINFO *ptr = new SOCKETINFO;
	if (ptr == NULL) {
		printf("[오류] 메모리가 부족합니다!\n");
		return FALSE;
	}
	//시간에 따른 랜덤 UserID값 생성
	srand(time(NULL));
	userID = rand();

	ptr->sock = sock;
	ptr->recvbytes = 0;
	ptr->ID = userID;
	SocketInfoArray[nTotalSockets++] = ptr;
	loginCheck = true;


	return TRUE;
}

// 소켓 정보 삭제
void RemoveSocketInfo(int nIndex)
{
	SOCKETINFO *ptr = SocketInfoArray[nIndex];

	// 종료한 클라이언트 정보 출력
	SOCKADDR_IN clientaddrv4;
	int addrlen = sizeof(clientaddrv4);
	getpeername(ptr->sock, (SOCKADDR *)&clientaddrv4, &addrlen);
	printf("[TCPv4 서버] 클라이언트 종료: [%s]:%d\n",
		inet_ntoa(clientaddrv4.sin_addr), ntohs(clientaddrv4.sin_port));

	closesocket(ptr->sock);
	delete ptr;

	if (nIndex != (nTotalSockets - 1))
		SocketInfoArray[nIndex] = SocketInfoArray[nTotalSockets - 1];

	--nTotalSockets;
}

