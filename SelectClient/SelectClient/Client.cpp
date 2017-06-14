// 2017�� 1�б� ��Ʈ��ũ���α׷��� ���� 3��
// ����: �ڹ��� �й�: 122179 

#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"

#define SERVERIPV4  "127.0.0.1"
#define SERVERPORT  9000
#define BUFSIZE     256                    // ���� �޽��� ��ü ũ��
#define MSGSIZE     (BUFSIZE-sizeof(int))  // ä�� �޽��� �ִ� ����
#define CHATTING    1000                   // �޽��� Ÿ��: ä��
#define NAMESIZE    20

// ���� �޽��� ����
// sizeof(COMM_MSG) == 256
struct COMM_MSG
{
	int  type;
	char dummy[MSGSIZE];
};

// ä�� �޽��� ����
// sizeof(CHAT_MSG) == 256
struct CHAT_MSG
{
	int  type;
	char buf[MSGSIZE];
};

static HWND          g_hDlg;
static HINSTANCE     g_hInst;						// ���� ���α׷� �ν��Ͻ� �ڵ�
static HWND          g_hButtonSendMsg;				// '�޽��� ����' ��ư
static HWND          g_hEditStatus;					// ���� �޽��� ���
static char          g_ipaddr[64];					// ���� IP �ּ�
static u_short       g_port;						// ���� ��Ʈ ��ȣ
static HANDLE        g_hClientThread;				// ������ �ڵ�
static volatile BOOL g_bStart;						// ��� ���� ����
static SOCKET        g_sock;						// Ŭ���̾�Ʈ ����
static HANDLE        g_hReadEvent, g_hWriteEvent;	// �̺�Ʈ �ڵ�
static CHAT_MSG      g_chatmsg;						// ä�� �޽��� ����
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

// ��ȭ���� ���ν���
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// ���� ��� ������ �Լ�
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// �ڽ� ������ ���ν���
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);


bool checkIP(char *ip);
bool checkPort(char inputPort[]);
#pragma region EtcFunc
// ����Ʈ ��Ʈ�ѿ� ���ڿ� ���
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
// ����� ���� ������ ���� �Լ�
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

// ���� �Լ�
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// ���� �ʱ�ȭ
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// IP,PORT �޸� �Ҵ�
	multicastIP = (char *)malloc(sizeof(char) * 15);
	multicastPort = (char *)malloc(sizeof(char) * 6);

	// �̺�Ʈ ����
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;

	// ���� �ʱ�ȭ(�Ϻ�)
	g_chatmsg.type = CHATTING;

	// ��ȭ���� ����
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// �̺�Ʈ ����
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// ���� ����
	WSACleanup();
	return 0;
}

// ��ȭ���� ���ν���
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

		// ��Ʈ�� �ʱ�ȭ
		SendMessage(hEditMsg, EM_SETLIMITTEXT, MSGSIZE, 0);
		EnableWindow(g_hButtonSendMsg, FALSE);
		EnableWindow(hShowUserBtn, FALSE);
		SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
		SetDlgItemText(hDlg, IDC_ONETOONENAME, "�ӼӸ� �� User�� �Է��ϼ���.");
		SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {

		case IDC_IPCHECK:
			GetDlgItemText(hDlg, IDC_IPADDR, multicastIP, BUFSIZE + 1);
			if (checkIP(multicastIP) == false)
			{
				MessageBox(hDlg, "�ùٸ� IP�� �Է��� �� �ֽ��ϴ�", "IP����", MB_OK);
				SetFocus(hCheckIP);
				return TRUE;
			}
			EnableWindow(hEditIPaddr, FALSE);    //�ּ� ���� ��ư ��Ȱ��ȭ
			EnableWindow(hCheckIP, FALSE);       //�ּ� ���� ��Ʈ�� ��Ȱ��ȭ
			ipCheck = true;
			return TRUE;

		case IDC_PORTCHECK:
			GetDlgItemText(hDlg, IDC_PORT, multicastPort, BUFSIZE + 1);
			if (checkPort(multicastPort) == false)
			{
				MessageBox(hDlg, "�ùٸ� Port��ȣ�� �Է��ϼ���", "Port����", MB_OK);
				SetFocus(hEditPort);
				return TRUE;
			}
			EnableWindow(hCheckPort, FALSE); //��Ʈ ���� ��ư ��Ȱ��ȭ
			EnableWindow(hEditPort, FALSE);  //��Ʈ ���� ��Ʈ�� ��Ȱ��ȭ
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
					MessageBox(hDlg, "NickName�� �����ϼ���", "���� �Ұ�", MB_OK);
					return TRUE;
				}
				room1 = true;
				room2 = false;
				WaitForSingleObject(g_hReadEvent, INFINITE);
				char loginStatusSend[BUFSIZE];

				strcpy(loginStatusSend, "1@");
				strcat(loginStatusSend, name);
				sprintf(g_chatmsg.buf, loginStatusSend);
				// ���� �ϷḦ �˸�
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
					MessageBox(hDlg, "NickName�� �����ϼ���", "���� �Ұ�", MB_OK);
					return TRUE;
				}
				room1 = false;
				room2 = true;
				WaitForSingleObject(g_hReadEvent, INFINITE);
				char loginStatusSend[BUFSIZE];

				strcpy(loginStatusSend, "2@");
				strcat(loginStatusSend, name);
				sprintf(g_chatmsg.buf, loginStatusSend);
				// ���� �ϷḦ �˸�
				SetEvent(g_hWriteEvent);
			}
			return TRUE;

		case IDC_CONNECT:
			if (ipCheck == false) {
				MessageBox(hDlg, "IP�� üũ�ϼ���", "���� �Ұ�", MB_OK);
				SetFocus(hEditIPaddr);
				return TRUE;
			}
			if (portCheck == false) {
				MessageBox(hDlg, "Port�� üũ�ϼ���", "���� �Ұ�", MB_OK);
				SetFocus(hEditPort);
				return TRUE;
			}

			// name �Է� �������� return
			GetDlgItemText(hDlg, IDC_Name, name, NAMESIZE + 1);
			if (strlen(name) == 0) {
				MessageBox(hDlg, "NickName�� �����ϼ���", "���� �Ұ�", MB_OK);
				return TRUE;
			}

			// room üũ ���ϸ� return
			if (room1 == false && room2 == false) {
				MessageBox(hDlg, "���� �����ϼ���", "���� �Ұ�", MB_OK);
				return TRUE;
			}

			GetDlgItemText(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));
			g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);

			// ���� ��� ������ ����
			g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
			if (g_hClientThread == NULL) {
				MessageBox(hDlg, "Ŭ���̾�Ʈ�� ������ �� �����ϴ�."
					"\r\n���α׷��� �����մϴ�.", "����!", MB_ICONERROR);
				EndDialog(hDlg, 0);
			}
			else {
				connectCheck = true;
				EnableWindow(hButtonConnect, FALSE);
				while (g_bStart == FALSE); // ���� ���� ���� ��ٸ�
				EnableWindow(hEditIPaddr, FALSE);
				EnableWindow(hEditPort, FALSE);
				EnableWindow(g_hButtonSendMsg, TRUE);
				EnableWindow(hShowUserBtn, TRUE);
				SetFocus(hEditMsg);
			}
			return TRUE;

		case IDC_SENDMSG:
			// �б� �ϷḦ ��ٸ�
			WaitForSingleObject(g_hReadEvent, INFINITE);
			GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);

			// �ӼӸ��� ��� ���� ǥ�����ֱ�
			if (oneToOneCheck == true) {
				GetDlgItemText(hDlg, IDC_ONETOONENAME, oneToOneName, NAMESIZE + 1);
				sprintf(g_chatmsg.buf, "%s@%s@%s", g_chatmsg.buf, oneToOneName, "!^");
			}
			// ���� �ϷḦ �˸�
			SetEvent(g_hWriteEvent);
			// �Էµ� �ؽ�Ʈ ��ü�� ���� ǥ��
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;

			// Show User ��ư�� Ŭ������ ��
		case IDC_SHOWUSER:
			if (connectCheck == false) {
				return TRUE;
			}

			// �б� �ϷḦ ��ٸ�
			WaitForSingleObject(g_hReadEvent, INFINITE);
			sprintf(g_chatmsg.buf, "#!ShowUser");
			// ���� �ϷḦ �˸�
			SetEvent(g_hWriteEvent);
			return TRUE;

			// 1:1 ��ȭ�� üũ ���� ��
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

// ���� ��� ������ �Լ�
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

	// �б� & ���� ������ ����
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "�����带 ������ �� �����ϴ�."
			"\r\n���α׷��� �����մϴ�.",
			"����!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

#pragma region Login
	// �б� �ϷḦ ��ٸ�
	WaitForSingleObject(g_hReadEvent, INFINITE);
	char loginStatusSend[BUFSIZE];

	if (room1 == true)
		strcpy(loginStatusSend, "1@");
	else
		strcpy(loginStatusSend, "2@");

	strcat(loginStatusSend, name);
	sprintf(g_chatmsg.buf, loginStatusSend);
	// ���� �ϷḦ �˸�
	SetEvent(g_hWriteEvent);
#pragma endregion

	// ������ ���� ���
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;

	if (retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);

	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	//MessageBox(NULL, "������ ������ �������ϴ�", "�˸�", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

// ������ �ޱ�
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

	if (!strcmp(chat_msg->buf, "���� �̸��� �����ڰ� �ֽ��ϴ�. �г����� �ٲ��ּ���")) {
		connectCheck = false;
		EnableWindow(hButtonConnect, TRUE);
	}
	return 0;
}

// ������ ������
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;

	// ������ ������ ���
	while (1) {
		// ���� �Ϸ� ��ٸ���
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// ���ڿ� ���̰� 0�̸� ������ ����
		if (strlen(g_chatmsg.buf) == 0) {
			// '�޽��� ����' ��ư Ȱ��ȭ
			EnableWindow(g_hButtonSendMsg, TRUE);
			// �б� �Ϸ� �˸���
			SetEvent(g_hReadEvent);
			continue;
		}

		// ������ ������
		retval = send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
		if (retval == SOCKET_ERROR) {
			break;
		}

		// '�޽��� ����' ��ư Ȱ��ȭ
		EnableWindow(g_hButtonSendMsg, TRUE);
		// �б� �Ϸ� �˸���
		SetEvent(g_hReadEvent);
	}

	return 0;
}

bool checkIP(char *ip)
{
	// ���� ����ó��
	if (strcmp(ip, " ") == 0)
		return false;

	// IP ���� ����ó��
	int len = strlen(ip);
	if (len > 15 || len < 7)
		return false;

	int nNumCount = 0;
	int nDotCount = 0;
	char checkIP1[3], checkIP2[3], checkIP3[3], checkIP4[3];
	int i = 0, k = 0;

	for (i = 0; i < len; i++)
	{
		// 0~9 �ƴ� ���� �ɸ���
		if (ip[i] < '0' || ip[i] > '9')
		{
			// . �������� IP �и��ϱ�
			if (ip[i] == '.')
			{
				++nDotCount;
				k = 0;
				nNumCount = 0;
			}
			else
				return false;
		}
		// �ùٸ� ���� �Է����� ��� . �������� �ش� ���ڿ��� IP ���� �ֱ�
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
	// .�� 3�� �ƴϸ� false
	if (nDotCount != 3)
		return false;

	int convertInputIP1 = atoi(checkIP1);
	int convertInputIP2 = atoi(checkIP2);
	int convertInputIP3 = atoi(checkIP3);
	int convertInputIP4 = atoi(checkIP4);

	// class IP �˻��ϱ�
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