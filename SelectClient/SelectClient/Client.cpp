// 2017년 1학기 네트워크프로그래밍 숙제 3번
// 성명: 박민현 학번: 122179 

#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"

#define SERVERIPV4  "127.0.0.1"
#define SERVERPORT  9000
#define BUFSIZE     256                    // 전송 메시지 전체 크기
#define MSGSIZE     (BUFSIZE-sizeof(int))  // 채팅 메시지 최대 길이
#define CHATTING    1000                   // 메시지 타입: 채팅
#define NAMESIZE    20

// 공통 메시지 형식
// sizeof(COMM_MSG) == 256
struct COMM_MSG
{
	int  type;
	char dummy[MSGSIZE];
};

// 채팅 메시지 형식
// sizeof(CHAT_MSG) == 256
struct CHAT_MSG
{
	int  type;
	char buf[MSGSIZE];
};

static HWND          g_hDlg;
static HINSTANCE     g_hInst;						// 응용 프로그램 인스턴스 핸들
static HWND          g_hButtonSendMsg;				// '메시지 전송' 버튼
static HWND          g_hEditStatus;					// 받은 메시지 출력
static char          g_ipaddr[64];					// 서버 IP 주소
static u_short       g_port;						// 서버 포트 번호
static HANDLE        g_hClientThread;				// 스레드 핸들
static volatile BOOL g_bStart;						// 통신 시작 여부
static SOCKET        g_sock;						// 클라이언트 소켓
static HANDLE        g_hReadEvent, g_hWriteEvent;	// 이벤트 핸들
static CHAT_MSG      g_chatmsg;						// 채팅 메시지 저장
static HWND hButtonConnect;

static bool room1 = false;
static bool room2 = false;
static bool oneToOneCheck = false;

char *multicastIP;
char *multicastPort;
char name[NAMESIZE];
char oneToOneName[NAMESIZE];

bool ipCheck = false;
bool portCheck = false;
bool connectCheck = false;

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// 자식 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);


bool checkIP(char *ip);
bool checkPort(char inputPort[]);
#pragma region EtcFunc
// 에디트 컨트롤에 문자열 출력
void DisplayText(char *fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(g_hEditStatus);
	SendMessage(g_hEditStatus, EM_SETSEL, nLength, nLength);
	SendMessage(g_hEditStatus, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

	va_end(arg);
}
// 사용자 정의 데이터 수신 함수
int recvn(SOCKET s, char *buf, int len, int flags)
{
	int received;
	char *ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if (received == 0)
			break;
		left -= received;
		ptr += received;
	}

	return (len - left);
}
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

// 메인 함수
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// IP,PORT 메모리 할당
	multicastIP = (char *)malloc(sizeof(char) * 15);
	multicastPort = (char *)malloc(sizeof(char) * 6);

	// 이벤트 생성
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;

	// 변수 초기화(일부)
	g_chatmsg.type = CHATTING;

	// 대화상자 생성
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// 이벤트 제거
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// 윈속 종료
	WSACleanup();
	return 0;
}

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HWND hEditIPaddr;
	static HWND hCheckIP;
	static HWND hEditPort;
	static HWND hCheckPort;
	static HWND hName;
	static HWND hEditMsg;
	static HWND hRoom1RadioBtn;
	static HWND hRoom2RadioBtn;
	static HWND hShowUserBtn;
	static HWND hOneToOneCheckBox;
	static HWND hOneToOneTextbox;

	switch (uMsg) {
	case WM_INITDIALOG:
#pragma region getTheControlHandle
		g_hDlg = hDlg;
		hEditIPaddr = GetDlgItem(hDlg, IDC_IPADDR);
		hCheckIP = GetDlgItem(hDlg, IDC_IPCHECK);
		hEditPort = GetDlgItem(hDlg, IDC_PORT);
		hCheckPort = GetDlgItem(hDlg, IDC_PORTCHECK);
		hName = GetDlgItem(hDlg, IDC_Name);
		hShowUserBtn = GetDlgItem(hDlg, IDC_SHOWUSER);
		hRoom1RadioBtn = GetDlgItem(hDlg, IDC_ROOM1);
		hRoom2RadioBtn = GetDlgItem(hDlg, IDC_ROOM2);
		hButtonConnect = GetDlgItem(hDlg, IDC_CONNECT);
		hEditMsg = GetDlgItem(hDlg, IDC_MSG);
		g_hButtonSendMsg = GetDlgItem(hDlg, IDC_SENDMSG);
		g_hEditStatus = GetDlgItem(hDlg, IDC_STATUS);
		hOneToOneCheckBox = GetDlgItem(hDlg, IDC_ONETONECHECK);
		hOneToOneTextbox = GetDlgItem(hDlg, IDC_ONETOONENAME);
#pragma endregion

		// 컨트롤 초기화
		SendMessage(hEditMsg, EM_SETLIMITTEXT, MSGSIZE, 0);
		EnableWindow(g_hButtonSendMsg, FALSE);
		EnableWindow(hShowUserBtn, FALSE);
		SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
		SetDlgItemText(hDlg, IDC_ONETOONENAME, "귓속말 할 User를 입력하세요.");
		SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {

		case IDC_IPCHECK:
			GetDlgItemText(hDlg, IDC_IPADDR, multicastIP, BUFSIZE + 1);
			if (checkIP(multicastIP) == false)
			{
				MessageBox(hDlg, "올바른 IP만 입력할 수 있습니다", "IP오류", MB_OK);
				SetFocus(hCheckIP);
				return TRUE;
			}
			EnableWindow(hEditIPaddr, FALSE);    //주소 편집 버튼 비활성화
			EnableWindow(hCheckIP, FALSE);       //주소 편집 컨트롤 비활성화
			ipCheck = true;
			return TRUE;

		case IDC_PORTCHECK:
			GetDlgItemText(hDlg, IDC_PORT, multicastPort, BUFSIZE + 1);
			if (checkPort(multicastPort) == false)
			{
				MessageBox(hDlg, "올바른 Port번호만 입력하세요", "Port오류", MB_OK);
				SetFocus(hEditPort);
				return TRUE;
			}
			EnableWindow(hCheckPort, FALSE); //포트 편집 버튼 비활성화
			EnableWindow(hEditPort, FALSE);  //포트 편집 컨트롤 비활성화
			portCheck = true;
			return TRUE;


		case IDC_ROOM1:
			if (connectCheck == false) {
				room1 = true;
				room2 = false;
			}
			else {
				GetDlgItemText(hDlg, IDC_Name, name, NAMESIZE + 1);
				if (strlen(name) == 0) {
					MessageBox(hDlg, "NickName을 설정하세요", "접속 불가", MB_OK);
					return TRUE;
				}
				room1 = true;
				room2 = false;
				WaitForSingleObject(g_hReadEvent, INFINITE);
				char loginStatusSend[BUFSIZE];

				strcpy(loginStatusSend, "1@");
				strcat(loginStatusSend, name);
				sprintf(g_chatmsg.buf, loginStatusSend);
				// 쓰기 완료를 알림
				SetEvent(g_hWriteEvent);
			}
			return TRUE;

		case IDC_ROOM2:
			if (connectCheck == false) {
				room1 = false;
				room2 = true;
			}
			else {
				GetDlgItemText(hDlg, IDC_Name, name, NAMESIZE + 1);
				if (strlen(name) == 0) {
					MessageBox(hDlg, "NickName을 설정하세요", "접속 불가", MB_OK);
					return TRUE;
				}
				room1 = false;
				room2 = true;
				WaitForSingleObject(g_hReadEvent, INFINITE);
				char loginStatusSend[BUFSIZE];

				strcpy(loginStatusSend, "2@");
				strcat(loginStatusSend, name);
				sprintf(g_chatmsg.buf, loginStatusSend);
				// 쓰기 완료를 알림
				SetEvent(g_hWriteEvent);
			}
			return TRUE;

		case IDC_CONNECT:
			if (ipCheck == false) {
				MessageBox(hDlg, "IP를 체크하세요", "접속 불가", MB_OK);
				SetFocus(hEditIPaddr);
				return TRUE;
			}
			if (portCheck == false) {
				MessageBox(hDlg, "Port를 체크하세요", "접속 불가", MB_OK);
				SetFocus(hEditPort);
				return TRUE;
			}

			// name 입력 안했으면 return
			GetDlgItemText(hDlg, IDC_Name, name, NAMESIZE + 1);
			if (strlen(name) == 0) {
				MessageBox(hDlg, "NickName을 설정하세요", "접속 불가", MB_OK);
				return TRUE;
			}

			// room 체크 안하면 return
			if (room1 == false && room2 == false) {
				MessageBox(hDlg, "방을 설정하세요", "접속 불가", MB_OK);
				return TRUE;
			}

			GetDlgItemText(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));
			g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);

			// 소켓 통신 스레드 시작
			g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
			if (g_hClientThread == NULL) {
				MessageBox(hDlg, "클라이언트를 시작할 수 없습니다."
					"\r\n프로그램을 종료합니다.", "실패!", MB_ICONERROR);
				EndDialog(hDlg, 0);
			}
			else {
				connectCheck = true;
				EnableWindow(hButtonConnect, FALSE);
				while (g_bStart == FALSE); // 서버 접속 성공 기다림
				EnableWindow(hEditIPaddr, FALSE);
				EnableWindow(hEditPort, FALSE);
				EnableWindow(g_hButtonSendMsg, TRUE);
				EnableWindow(hShowUserBtn, TRUE);
				SetFocus(hEditMsg);
			}
			return TRUE;

		case IDC_SENDMSG:
			// 읽기 완료를 기다림
			WaitForSingleObject(g_hReadEvent, INFINITE);
			GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);

			// 귓속말일 경우 따로 표시해주기
			if (oneToOneCheck == true) {
				GetDlgItemText(hDlg, IDC_ONETOONENAME, oneToOneName, NAMESIZE + 1);
				sprintf(g_chatmsg.buf, "%s@%s@%s", g_chatmsg.buf, oneToOneName, "!^");
			}
			// 쓰기 완료를 알림
			SetEvent(g_hWriteEvent);
			// 입력된 텍스트 전체를 선택 표시
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;

			// Show User 버튼을 클릭했을 때
		case IDC_SHOWUSER:
			if (connectCheck == false) {
				return TRUE;
			}

			// 읽기 완료를 기다림
			WaitForSingleObject(g_hReadEvent, INFINITE);
			sprintf(g_chatmsg.buf, "#!ShowUser");
			// 쓰기 완료를 알림
			SetEvent(g_hWriteEvent);
			return TRUE;

			// 1:1 대화를 체크 했을 때
		case IDC_ONETONECHECK:
			if (oneToOneCheck == false)
				oneToOneCheck = true;
			else
				oneToOneCheck = false;
			return TRUE;

		case IDCANCEL:
			closesocket(g_sock);
			EndDialog(hDlg, IDCANCEL);
			return TRUE;

		}
		return FALSE;
	}

	return FALSE;
}

// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	// socket()
	g_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (g_sock == INVALID_SOCKET) err_quit("socket()");

	// connect()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(g_ipaddr);
	serveraddr.sin_port = htons(g_port);
	retval = connect(g_sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) err_quit("connect()");

	// 읽기 & 쓰기 스레드 생성
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "스레드를 시작할 수 없습니다."
			"\r\n프로그램을 종료합니다.",
			"실패!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

#pragma region Login
	// 읽기 완료를 기다림
	WaitForSingleObject(g_hReadEvent, INFINITE);
	char loginStatusSend[BUFSIZE];

	if (room1 == true)
		strcpy(loginStatusSend, "1@");
	else
		strcpy(loginStatusSend, "2@");

	strcat(loginStatusSend, name);
	sprintf(g_chatmsg.buf, loginStatusSend);
	// 쓰기 완료를 알림
	SetEvent(g_hWriteEvent);
#pragma endregion

	// 스레드 종료 대기
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;

	if (retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);

	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	//MessageBox(NULL, "서버가 접속을 끊었습니다", "알림", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

// 데이터 받기
DWORD WINAPI ReadThread(LPVOID arg)
{
	int retval;
	COMM_MSG comm_msg;
	CHAT_MSG *chat_msg;

	while (1) {
		retval = recvn(g_sock, (char *)&comm_msg, BUFSIZE, 0);
		if (retval == 0 || retval == SOCKET_ERROR) {
			break;
		}

		if (comm_msg.type == CHATTING) {
			chat_msg = (CHAT_MSG *)&comm_msg;
			DisplayText("%s\r\n", chat_msg->buf);
		}
	}

	if (!strcmp(chat_msg->buf, "같은 이름의 접속자가 있습니다. 닉네임을 바꿔주세요")) {
		connectCheck = false;
		EnableWindow(hButtonConnect, TRUE);
	}
	return 0;
}

// 데이터 보내기
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;

	// 서버와 데이터 통신
	while (1) {
		// 쓰기 완료 기다리기
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// 문자열 길이가 0이면 보내지 않음
		if (strlen(g_chatmsg.buf) == 0) {
			// '메시지 전송' 버튼 활성화
			EnableWindow(g_hButtonSendMsg, TRUE);
			// 읽기 완료 알리기
			SetEvent(g_hReadEvent);
			continue;
		}

		// 데이터 보내기
		retval = send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
		if (retval == SOCKET_ERROR) {
			break;
		}

		// '메시지 전송' 버튼 활성화
		EnableWindow(g_hButtonSendMsg, TRUE);
		// 읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}

	return 0;
}

bool checkIP(char *ip)
{
	// 공백 예외처리
	if (strcmp(ip, " ") == 0)
		return false;

	// IP 길이 예외처리
	int len = strlen(ip);
	if (len > 15 || len < 7)
		return false;

	int nNumCount = 0;
	int nDotCount = 0;
	char checkIP1[3], checkIP2[3], checkIP3[3], checkIP4[3];
	int i = 0, k = 0;

	for (i = 0; i < len; i++)
	{
		// 0~9 아닌 숫자 걸르기
		if (ip[i] < '0' || ip[i] > '9')
		{
			// . 기준으로 IP 분리하기
			if (ip[i] == '.')
			{
				++nDotCount;
				k = 0;
				nNumCount = 0;
			}
			else
				return false;
		}
		// 올바른 숫자 입력했을 경우 . 기준으로 해당 문자열에 IP 집어 넣기
		else
		{
			if (nDotCount == 0) {
				checkIP1[k++] = ip[i];
			}
			else if (nDotCount == 1) {
				checkIP2[k++] = ip[i];
			}
			else if (nDotCount == 2) {
				checkIP3[k++] = ip[i];
			}
			else if (nDotCount == 3) {
				checkIP4[k++] = ip[i];
			}
			if (++nNumCount > 3)
				return false;
		}
	}
	// .이 3개 아니면 false
	if (nDotCount != 3)
		return false;

	int convertInputIP1 = atoi(checkIP1);
	int convertInputIP2 = atoi(checkIP2);
	int convertInputIP3 = atoi(checkIP3);
	int convertInputIP4 = atoi(checkIP4);

	// class IP 검사하기
	if (convertInputIP1 >= 0 && convertInputIP1 <= 255) {
		if (convertInputIP2 >= 0 && convertInputIP2 <= 255) {
			if (convertInputIP3 >= 0 && convertInputIP3 <= 255) {
				if (convertInputIP4 >= 0 && convertInputIP4 <= 255) {
					return true;
				}
				return false;
			}
			return false;
		}
		return false;
	}
	return false;
}
bool checkPort(char inputPort[]) {
	int convertInputPort = atoi(inputPort);

	if (convertInputPort >= 1024 && convertInputPort <= 65535)
		return true;
	else
		return false;
}