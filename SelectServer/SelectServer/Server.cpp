// 2017년 1학기 네트워크프로그래밍 숙제 3번
// 성명: 박민현 학번: 122179

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
bool room1PrintFlag = false;
bool room2PrintFlag = false;
int nTotalSockets = 0;
int userID;
static CHAT_MSG      g_chatmsg; // 채팅 메시지 저장
SOCKETINFO *SocketInfoArray[FD_SETSIZE];

// 소켓 관리 함수
BOOL AddSocketInfo(SOCKET sock);
void RemoveSocketInfo(int nIndex);
bool checkSameNameUser(SOCKETINFO *ptr, int retval);
bool checkOneToOneUser(SOCKETINFO *ptr, char *msg, char *name, int retval); 
void PrintUsers(SOCKETINFO *ptr, SOCKETINFO *ptr2, char *room2UserBuf, int j);
void flagCheckInit();

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
int main(int argc, char *argv[])
{
#pragma region InitServer

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
#pragma endregion
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
		char room2UserBuf[BUFSIZE + 1] = { NULL };				// 사용자 출력할 때 방2에 있는 사용자는 나중에 보여주기 위해 임시 저장하는 Buf
		char *splitBuf[2] = { NULL };
		char *oneToOneSplitBuf[2] = { NULL };

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

				// Buf 앞 부분 짜르기
				strncpy(tempBuf, ptr->buf + 4, BUFSIZE - 4);
				// temBufSize 측정하기 
				int tempBufSize = strlen(tempBuf) + 4;

				// User가 처음 들어왔을 경우 room 초기화 
				if (tempBuf[1] == ROOMCHECK)
				{
					char *splitChar = strtok(tempBuf, "@");
					for (int i = 0; i < 2; i++) {
						splitBuf[i] = splitChar;
						splitChar = strtok(NULL, "@");
					}

					// 방 체크
					if (tempBuf[0] == ROOM1)
						ptr->room = 1;
					else if (tempBuf[0] == ROOM2)
						ptr->room = 2;

					strcpy(ptr->name, splitBuf[1]);
				}

				// 1:1 대화일 경우
				else if (ptr->buf[tempBufSize - 2] == '!' && ptr->buf[tempBufSize - 1] == '^') {
					// @로 문자 나누기
					char *splitChar = strtok(tempBuf, "@");
					for (int i = 0; i < 2; i++) {
						oneToOneSplitBuf[i] = splitChar;
						splitChar = strtok(NULL, "@");
					}

					// 닉네임 체크
					if (checkOneToOneUser(ptr, oneToOneSplitBuf[0], oneToOneSplitBuf[1], retval) == false) {
						sprintf(g_chatmsg.buf, "일치하는 이름의 사용자가 없습니다.");
						retval = send(ptr->sock, (char *)&g_chatmsg, BUFSIZE, 0);
					}
					break;
				}

				// User 보여달라 요청했을 경우
				else if (!strcmp(tempBuf, SHOWUSERS))
				{
					sprintf(g_chatmsg.buf, "현재 접속한 User입니다.\n");
					showUsersCheck = true;
				}

				if (ptr->recvbytes == BUFSIZE) {
					// 받은 바이트 수 리셋
					ptr->recvbytes = 0;

					// 현재 접속한 모든 클라이언트에게 데이터를 보냄!
					for (j = 0; j < nTotalSockets; j++) {
						SOCKETINFO *ptr2 = SocketInfoArray[j];

						// 접속자 보여달라 했을 경우
						if (showUsersCheck == true) {
							PrintUsers(ptr,ptr2,room2UserBuf,j);
							continue;
						}

						// 방이 같은 경우에만 데이타 전송
						else if (ptr->room == ptr2->room) {
							// Client가 처음 채팅방에 접속한 경우
							if (loginCheck == true) {
								// 같은 이름의 사용자 있는지 체크
								if (checkSameNameUser(ptr, retval)) {
									break;
								}
								sprintf(g_chatmsg.buf, "닉네임 %s님이 채팅방%d에 %s", ptr->name, ptr->room, "접속하셨습니다!");
								retval = send(ptr2->sock, (char *)&g_chatmsg, BUFSIZE, 0);
							}
							// 일반 통신
							else {
								sprintf(g_chatmsg.buf, "%s : %s", ptr->name, tempBuf);
								retval = send(ptr2->sock, (char *)&g_chatmsg, BUFSIZE, 0);
							}
							// 에러 났을 경우
							if (retval == SOCKET_ERROR) {
								err_display("send()");
								RemoveSocketInfo(j);
								--j; // 루프 인덱스 보정
								continue;
							}
						}
					}
					flagCheckInit();
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

void PrintUsers(SOCKETINFO *ptr, SOCKETINFO *ptr2, char *room2UserBuf, int j) {
	int retval;

	// 방 1일 경우
	if (ptr2->room == 1) {
		if (room1PrintFlag == false) {
			sprintf(g_chatmsg.buf, "%s %s: ",g_chatmsg.buf, "room1" );
			room1PrintFlag = true;
		}
		sprintf(g_chatmsg.buf, "%s %s", g_chatmsg.buf, ptr2->name);
	}
	// 방 2일 경우
	else if (ptr2->room == 2) {
		if (room2PrintFlag == false) {
			sprintf(g_chatmsg.buf, "%s %s: ",g_chatmsg.buf, "room2");
			room2PrintFlag = true;
		}
		sprintf(room2UserBuf, "%s %s", room2UserBuf, ptr2->name);
	}
	if (j == nTotalSockets - 1) {
		sprintf(g_chatmsg.buf, "%s %s", g_chatmsg.buf, room2UserBuf);
		retval = send(ptr->sock, (char *)&g_chatmsg, BUFSIZE, 0);
	}
}

bool checkOneToOneUser(SOCKETINFO *ptr, char *msg, char *name, int retval) {
	// 받은 바이트 수 리셋
	ptr->recvbytes = 0;
	for (int i = 0; i < nTotalSockets; i++) {
		SOCKETINFO *ptr3 = SocketInfoArray[i];

		if (!strcmp(ptr3->name, name))
		{
			sprintf(g_chatmsg.buf, "(귓속말) %s : %s", ptr->name, msg);
			retval = send(ptr->sock, (char *)&g_chatmsg, BUFSIZE, 0);
			retval = send(ptr3->sock, (char *)&g_chatmsg, BUFSIZE, 0);
			return true;
		}
	}
	return false;
}

void flagCheckInit() {
	loginCheck = false;
	showUsersCheck = false;
	room1PrintFlag = false;
	room2PrintFlag = false;
}