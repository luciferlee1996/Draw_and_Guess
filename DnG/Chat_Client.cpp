#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "stdafx.h"
#include "Chat_Client.h"
#include "Resource.h"
using namespace std;

Chat_Client* Chat_Client::self = nullptr;
int test_pull_cnt = 0;

Chat_Client::Chat_Client(char* ipad, int port):  port(port), quit(false), status(SEND_Empty){
	self = this;
	strcpy_s(ip, strnlen_s(ipad, 15) + 1, ipad);
	event = CreateEvent(0, 0, 0, 0);
	InitializeCriticalSection(&lock);
}

int Chat_Client::SetHWND(HWND hWnd) {
	this->hWnd = hWnd;
	return 0;
}


bool Chat_Client::ClientInit(){
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.S_un.S_addr = inet_addr(ip);

	if (WSAStartup(MAKEWORD(2, 2), &wsaData)){
		MessageBox(0, L"Socket startup failed\n", 0, MB_OK);
		return 1;
	}

	clt = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clt == INVALID_SOCKET) {
		MessageBox(0, L"Invalid socket\n", 0, MB_OK);
		return 1;
	}
	return 0;
}

bool Chat_Client::ClientConnect(){

	if (connect(clt, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in))) {
		printf("Connect failed %u", WSAGetLastError());
		MessageBox(0, L"Connect failed: ", 0, MB_OK);
		MessageBox(0,std::to_wstring(WSAGetLastError()).c_str(), 0, MB_OK);
		return 1;
	}
	
	CreateThread(0, 0, PullFromServer, this, 0, 0);
	CreateThread(0, 0, SendToServer, this, 0, 0);

	return 0;
}

void Chat_Client::SendMsg(const char* msg){
	send(clt, msg, 500, 0);
}

bool Chat_Client::Close(){
	send(clt, "t", 1, 0);
	quit = true;
	closesocket(clt);
	WSACleanup();
	return 0;
}

unsigned long Chat_Client::PullFromServer(void* data) {
	Chat_Client* self = static_cast<Chat_Client*>(data);
	char send_buf[3];
	send_buf[2] = 0;
	while (!self->quit && recv(self->clt, send_buf, 2, 0) > 0){
		if (!strcmp(send_buf, MY_SEND_DRAW_LINE)) {
			if (self->DrawLine()) break;
		}
		else if (!strcmp(send_buf, MY_SEND_GUESS_RIGHT)) {
			if (self->GuessResult(true)) break;
		}
		else if (!strcmp(send_buf, MY_SEND_GUESS_WRONG)) {
			if (self->GuessResult(false)) break;
		}
		else {
			// !!!HERE
		}
		test_pull_cnt++;
	}
	MessageBox(0, L"Server disconnected when pull\n", 0, MB_OK);
	MessageBox(0, std::to_wstring(WSAGetLastError()).c_str(), 0, MB_OK);
	self->quit = true;

	return 0;
}



unsigned long Chat_Client::SendToServer(void* data) {
	Chat_Client* self = static_cast<Chat_Client*>(data);	
	while (!self->quit && !WaitForSingleObject(self->event, INFINITE)) {
		EnterCriticalSection(&self->lock);
		switch (self->status) {
			case SEND_DrawLine:
			{
				unsigned int size = self->V_vec.size() * sizeof(VALUE);
				send(self->clt, MY_SEND_DRAW_LINE, 2, 0);
				send(self->clt, reinterpret_cast<char*>(&size), 4, 0);
				send(self->clt, reinterpret_cast<char*>(&(self->V_vec[0])), size, 0);
				self->V_vec.clear();
				self->status = SEND_Empty;
			}
			break;

			case SEND_Guess:
			{
				MessageBox(0, self->GuessStr, 0, MB_OK);
				unsigned int size_with_ending= (wcslen(self->GuessStr) + 1)*sizeof(wchar_t);
				send(self->clt, MY_SEND_GUESS, 2, 0);
				send(self->clt, reinterpret_cast<char*>(&size_with_ending), 4, 0);
				send(self->clt, reinterpret_cast<char*>(self->GuessStr), size_with_ending, 0);
				self->status = SEND_Empty;
			}
			break;

			default:
			{
				self->status = SEND_Empty;
			}
			break;
		}
		LeaveCriticalSection(&self->lock);
	}
	MessageBox(0, L"Server disconnected when send\n", 0, MB_OK);
	MessageBox(0, std::to_wstring(WSAGetLastError()).c_str(), 0, MB_OK);
	self->quit = true;
	
	
	return 0;
}


bool Chat_Client::pack(UINT msg, LPARAM lparam) {
	EnterCriticalSection(&lock);
	VALUE V;
	*reinterpret_cast<UINT*>(V.value) = msg;
	*reinterpret_cast<LPARAM*>(V.value + 4) = lparam;
	V_vec.push_back(V);
	LeaveCriticalSection(&lock);
	if (WM_LBUTTONUP == msg)
		// Send to Server
		status = SEND_DrawLine;
		SetEvent(event);

	return true;
}

void Chat_Client::Disconnect() {
	send(clt, MY_SEND_DISCONNECT, 2, false);
}


int Chat_Client::DrawLine() {
	char size_buf[5];
	size_buf[4] = 0;
	if (recv(self->clt, size_buf, 4, 0) <= 0) return 1;
	unsigned int size = *reinterpret_cast<unsigned int*>(size_buf);
	MessageBoxA(0, std::to_string(size).c_str(), 0, MB_OK);
	char* buffer = new char[size + 1];

	char* current_pos = buffer;
	unsigned int current_size = 0;
	while (current_size < size) {
		int fragment_size = recv(clt, current_pos, size - current_size, 0);
		if (fragment_size <= 0) {
			break;
		}
		current_size += fragment_size;
		current_pos += fragment_size;
		cout << fragment_size << endl;
	}
	for (unsigned int i = 0; i < size; i += 12) {
		UINT tmp_msg = *reinterpret_cast<UINT*>(buffer + i);
		LPARAM tmp_lparam = *reinterpret_cast<LPARAM*>(buffer + i + 4);
		if (!i)
			PostMessage(self->hWnd, MY_WM_START_LINE, 0, tmp_lparam);
		else
			PostMessage(self->hWnd, MY_WM_SYNC_LINE, 0, tmp_lparam);
	}
	delete[] buffer;
	return 0;
}


int Chat_Client::guess(const wchar_t* str) {
	// there are at most 5 wide characters in str
	unsigned int i = wcslen(str);
	//MessageBoxA(0, std::to_string(i).c_str(), 0, MB_OK);
	status = SEND_Guess;
	wcsncpy_s(GuessStr, str, 5);
	SetEvent(event);
	return 0;
}


int Chat_Client::GuessResult(bool right) {
	if (right) {
		// guess right
		MessageBoxA(0, "Someone Guesses Right", 0, MB_OK);
		PostMessage(self->hWnd, MY_WM_GUESS_RIGHT, 0, 0);
	}
	else {
		// guess wrong
		MessageBoxA(0, "Someone Guesses Wrong", 0, MB_OK);
		PostMessage(self->hWnd, MY_WM_GUESS_WRONG, 0, 0);

	}

	return 0;
}