#include <windows.h>
#include <list>
#include "atlstr.h"

#include <string>
#include <cstring>
#include <fstream>
#include <ctime>
#include <vector>
using namespace std;

#include "resource.h"

#define SERVER_PORT 4410

#define WM_SOCKET_NOTIFY (WM_USER + 1)
#define WM_SERVER_NOTIFY (WM_USER + 2)

// NP
#ifdef DEBUG
	//#undef DEBUG
#endif

#ifndef UNICODE
#define UNICODE
#endif

#define MAX_SIZE 15001
#define INET_ADDRSTRLEN 16
#define FILENAME_LENGTH 100
#define CLIENT_MAX_NUMBER 5

#define PROTOCOL "HTTP/1.1"
#define SERVER "CYH_NP_SERVER/44.10"
#define HEADER_SIZE 8192
#define MIME_CGI ".cgi"

// for log flag
#define IS_LOG (1 << 0)
#define NP_IS_ERROR (1 << 1)
#define NEED_NEWLINE (1 << 2)
#define NEED_BOLD (1<<3)

// for log time
time_t start, now;

#define BUFFER_DONT_RESET (1 << 0)

// Linux to windows
#define strcasecmp _stricmp
#define S_ISDIR(m) (((m) & 0170000) == (0040000))  

namespace NP {

	enum SockType { Browser, Server };
	enum SockStatus { CONNECTING, READING, WRITING, DONE };
	class SockInfo {

		SOCKET sockfd = -1;
		SockType socktype;
		
		// for Browser
		char buffer[MAX_SIZE];
		bool hasRead = false;
		bool hasWrite = false;

		// for Server
		SockStatus sockstatus = CONNECTING;
		bool canRead = false;
		bool canWrite = false;
		bool sentExit = false;
		char ip[INET_ADDRSTRLEN];
		char port[6];
		char filename[FILENAME_LENGTH];
		int domId;
		ifstream file;
		bool isDone = false;

	public:

		SockInfo() {}

		SockInfo(SOCKET newSock) {
			sockfd = newSock;
			socktype = Browser;
		}

		SockInfo(int id, const char* iIp, const char* iPort, const char* iFile) {

			socktype = Server;
			domId = id;
			strncpy(ip, iIp, INET_ADDRSTRLEN);
			strncpy(port, iPort, 6);
			strncpy(filename, iFile, FILENAME_LENGTH);

			file = ifstream(filename, ifstream::in);
		}
		
		SOCKET getSockFd() {
			return sockfd;
		}

		SockType getSockType() {
			return socktype;
		}

		// For Servers
		string getIp() {
			return string(ip);
		}

		string getDomId() {
			return "m" + to_string(domId);
		}

		int getDomIdInInt() {
			return domId;
		}

		SockStatus getSockStatus() {
			return sockstatus;
		}

		void setCanRead(bool ifCanRead) {
			canRead = ifCanRead;
		}
		
		bool getCanRead() {
			return canRead;
		}

		void setCanWrite(bool ifCanWrite) {
			canWrite = ifCanWrite;
		}

		bool getCanWrite() {
			return canWrite;
		}

		void setHasWrite(bool isWritten) {
			hasWrite = isWritten;
		}

		void setHasRead(bool isRead) {
			hasRead = isRead;
		}

		bool getHasWrite() {
			return hasWrite;
		}

		bool getHasRead() {
			return hasRead;
		}

		void setSockStatus(SockStatus newStatus) {
			sockstatus = newStatus;
		}

		bool getIsDone() {
			return isDone;
		}

		void setIsDone(bool ifDone) {
			isDone = ifDone;
		}

		ifstream& getFile() {
			return file;
		}

		bool hasSentExit() {
			return sentExit;
		}

		void setExitStatus(bool status) {
			sentExit = status;
		}

		bool connectServer(HWND& hwnd);

		void print(string msg, int flag = 0);
	};

	SockInfo& findSockInfoBySockfd(SOCKET sockfd);

	void print(string domId, string msg, int flag = 0);

	void log(string ch, bool error = false, bool newline = true, bool prefix = true);

	void log(const char* ch, bool error = false, bool newline = true, bool prefix = true);

	void err(string str);

	/**
	*  Set buffer content
	*
	*  @param ch Character array
	*/
	void setBuffer(const char* ch);

	/**
	*  Reset buffer content
	*/
	void resetBuffer();

	char* readWrapper(int sockfd);

	void writeWrapper(int sockfd, const char buffer[], size_t size, bool flag = 0);


	/// HW3 Related

	void requestHandler(int sockfd, char req[MAX_SIZE], HWND hwnd);

	/**
	*	Send header.
	*  For `MIME_CGI`: won't output `Content-Type:` line and the newline after header.
	*
	*	@param status	HTTP status code
	*	@param title	status description
	*	@param mime		MIME type ('MIME_CGI')
	*/
	void sendHeader(int sockfd, int status, const char* title, const char* mime);

	void generateErrorPage(int sockfd, int status, const char* title);

	const char* getMimeType(char* name);

	void cleanupWebsock();

	void afterSelect(HWND& hwnd);

	namespace CGI {

		int numberOfMachines = 0, doneMachines = 0;

		void handler(int sockfd, HWND& hwnd);

		void printHeader(int sockfd);

		void printBody(int sockfd);

		bool connectServers(HWND& hwnd);

		void printFooter(int sockfd);
	}
	
}

using namespace NP;

NP::SockInfo* websock;
vector<NP::SockInfo> clients;
char buffer[MAX_SIZE];
char req[MAX_SIZE];
SOCKET ssock;
ofstream output;
BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf(HWND, TCHAR *, ...);
HWND hwndEdit;
SOCKET msock;
struct sockaddr_in sa;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	output = ofstream("log.txt", ofstream::out | ofstream::app);
	output << "--------------------------------------" << endl;

	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;
	int err;

	switch(Message) 
	{
		case WM_INITDIALOG:
			hwndEdit = GetDlgItem(hwnd, IDC_RESULT);
			break;
		case WM_COMMAND:
			switch(LOWORD(wParam))
			{
				case ID_LISTEN:

					WSAStartup(MAKEWORD(2, 0), &wsaData);

					//create master socket
					msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
					if( msock == INVALID_SOCKET ) {
						EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
						return TRUE;
					}

					err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE);
					if ( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
						closesocket(msock);
						//WSACleanup();
						return TRUE;
					}

					//fill the address info about server
					sa.sin_family		= AF_INET;
					sa.sin_port			= htons(SERVER_PORT);
					sa.sin_addr.s_addr	= INADDR_ANY;

					//bind socket
					err = bind(msock, (LPSOCKADDR)&sa, sizeof(sa));
					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
						//WSACleanup();
						return FALSE;
					}

					err = listen(msock, SOMAXCONN);
					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
						//WSACleanup();
						return FALSE;
					}
					else {
						EditPrintf(hwndEdit, TEXT("=== Server START ===\r\n"));
					}

					break;

				case ID_EXIT:
					EndDialog(hwnd, 0);
					break;
			};
			break;

		case WM_CLOSE:
			EndDialog(hwnd, 0);
			break;

		case WM_SOCKET_NOTIFY:
		{
			SOCKET curSockfd = wParam;
//			if (WSAGETSELECTERROR(lParam))
	//			EditPrintf(hwndEdit, TEXT("=== WM_SOCKET_NOTIFY: WSAGETSELECTERROR %d ===\r\n"), WSAGETSELECTERROR(lParam));

			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_ACCEPT:
				{
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_ACCEPT ===\r\n"), curSockfd);
					WSAStartup(MAKEWORD(2, 0), &wsaData);

					SOCKET temp = accept(msock, NULL, NULL);
					if (websock != NULL && !websock->getIsDone()) {
						if (ssock != temp) {
							closesocket(temp);
						}
						break;
					}

					ssock = temp;
					websock = new NP::SockInfo(ssock);
					WSAAsyncSelect(ssock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE);

					NP::CGI::numberOfMachines = 0;
					NP::CGI::doneMachines = 0;

					EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d) ===\r\n"), ssock);
					break;
				}

				case FD_READ:
				{
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_READ ===\r\n"), curSockfd);
					
					NP::readWrapper(curSockfd);
					strcpy(req, buffer);
					websock->setHasRead(true);
					WSAAsyncSelect(ssock, hwnd, WM_SOCKET_NOTIFY, FD_CLOSE | FD_WRITE);
					
					// check if canWrite, if so, do write
					if (websock->getCanWrite() && !websock->getHasWrite()) {
						NP::requestHandler(ssock, req, hwnd);
						websock->setHasWrite(true);
					}

					break;
				}

				case FD_WRITE:
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_WRITE ===\r\n"), curSockfd);
					
					websock->setCanWrite(true);

					// check if hasRead, 
					// if not, set canWrite
					// else, write!
					if (websock->getHasRead() && !websock->getHasWrite()) {
						NP::requestHandler(ssock, req, hwnd);
						websock->setHasWrite(true);
					}

					break;

				case FD_CLOSE:
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_CLOSE ===\r\n"), curSockfd);
					//cleanup(curSockfd);
					break;
			};

			break;
		}

		case WM_SERVER_NOTIFY:
		{
			SOCKET curSockfd = wParam;
			if(WSAGETSELECTERROR(lParam))
				EditPrintf(hwndEdit, TEXT("=== WM_SERVER_NOTIFY: WSAGETSELECTERROR %x ===\r\n"), WSAGETSELECTERROR(lParam));

			switch (WSAGETSELECTEVENT(lParam)) {

			case FD_CONNECT:
			{
				EditPrintf(hwndEdit, TEXT("=== WM_SERVER_NOTIFY Sock #%d FD_CONNECT ===\r\n"), curSockfd);

				// connection is established
				// check if success
				int error = 0;
				int error_len = sizeof(error);
				if (getsockopt(curSockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &error_len) == SOCKET_ERROR ||
					error != 0) {
					EditPrintf(hwndEdit, TEXT("=== Error: connect error %x (in FD_CONNECT) ===\r\n", WSAGetLastError()));
					NP::print("ALL", "Socket `" + to_string(curSockfd) + "` connect error.", IS_LOG | NP_IS_ERROR | NEED_NEWLINE);
					NP::CGI::doneMachines++;

					if (NP::CGI::doneMachines == NP::CGI::numberOfMachines) {
						NP::cleanupWebsock();
						clients.clear();
					}
					break;
				}

				// start listening FD_READ for prompt
				WSAAsyncSelect(curSockfd, hwnd, WM_SERVER_NOTIFY, FD_CLOSE | FD_READ | FD_WRITE);
				NP::findSockInfoBySockfd(curSockfd).setSockStatus(READING);
				break;
			}

			case FD_ACCEPT:
				EditPrintf(hwndEdit, TEXT("=== WM_SERVER_NOTIFY Sock #%d FD_ACCEPT ===\r\n"), curSockfd);
				break;

			case FD_READ:
			{
				EditPrintf(hwndEdit, TEXT("=== WM_SERVER_NOTIFY Sock #%d FD_READ ===\r\n"), curSockfd);
				NP::findSockInfoBySockfd(curSockfd).setCanRead(true);
				break;
			}

			case FD_WRITE:
				EditPrintf(hwndEdit, TEXT("=== WM_SERVER_NOTIFY Sock #%d FD_WRITE ===\r\n"), curSockfd);
				NP::findSockInfoBySockfd(curSockfd).setCanWrite(true);	
				break;

			case FD_CLOSE:
				EditPrintf(hwndEdit, TEXT("=== WM_SERVER_NOTIFY Sock #%d FD_CLOSE (%d/%d) ===\r\n"), curSockfd, NP::CGI::doneMachines, NP::CGI::numberOfMachines);
				
				/*char str[15000];
				sprintf(str, "=== WM_SERVER_NOTIFY Sock #%d FD_CLOSE (%d/%d) ===\n", curSockfd, NP::CGI::doneMachines, NP::CGI::numberOfMachines);
				output << str;
				output.flush();
				*/
				if (NP::findSockInfoBySockfd(curSockfd).getIsDone()) {
					return true;
				}

 				NP::findSockInfoBySockfd(curSockfd).setIsDone(true);
				closesocket(curSockfd);
				
				// check if all machines are done
				// if so, print footer & close socket
				NP::CGI::doneMachines++;
				if (NP::CGI::doneMachines == NP::CGI::numberOfMachines) {
					NP::CGI::printFooter(websock->getSockFd());
					NP::cleanupWebsock();
					clients.clear();
				}
				
				break;
			};

			// go through every socket to take corresponding action
			NP::afterSelect(hwnd);
			break;
		}
		
		default:
			return FALSE;
	};

	return TRUE;
}

int EditPrintf (HWND hwndEdit, TCHAR * szFormat, ...)
{
     TCHAR   szBuffer [MAX_SIZE] ;
     va_list pArgList ;

     va_start (pArgList, szFormat) ;
     wvsprintf (szBuffer, szFormat, pArgList) ;
     va_end (pArgList) ;

     SendMessage (hwndEdit, EM_SETSEL, (WPARAM) -1, (LPARAM) -1) ;
     SendMessage (hwndEdit, EM_REPLACESEL, FALSE, (LPARAM) szBuffer) ;
     SendMessage (hwndEdit, EM_SCROLLCARET, 0, 0) ;
	 return SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0); 
}



/*
	NP
*/
using namespace NP;
using namespace NP::CGI;

// CGI
void NP::CGI::handler(int sockfd, HWND& hwnd) {

	time(&start);

	printHeader(sockfd);

	printBody(sockfd);

	connectServers(hwnd);

	///printFooter(sockfd);
}

void NP::CGI::printHeader(int sockfd) {

	char header[] =
		"Content-Type: text/html\n\n"
		"<html>"
		"<head>"
		"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />"
		"<title>Network Programming Homework 3</title>"
		"</head>";

	writeWrapper(sockfd, header, strlen(header));
}

void NP::CGI::printFooter(int sockfd) {

	char footer[] =
		"</font>"
		"</body>"
		"</html>";

	writeWrapper(sockfd, footer, strlen(footer));
}

void NP::CGI::printBody(int sockfd) {
	//    setenv("QUERY_STRING", "h1=127.0.0.1&p1=4414&f1=t5.txt&h2=127.0.0.1&p2=4413&f2=t6.txt&h3=127.0.0.1&p3=4415&f3=t7.txt&h4=127.0.0.1&p4=4410&f4=t4.txt&h5=127.0.0.1&p5=4410&f5=t1.txt", 1);
	char* data = getenv("QUERY_STRING");
	char ip[CLIENT_MAX_NUMBER][INET_ADDRSTRLEN];
	char port[CLIENT_MAX_NUMBER][6];
	char file[CLIENT_MAX_NUMBER][FILENAME_LENGTH];

	char* token = strtok(data, "&");
	while (token != NULL && numberOfMachines < 5) {

		if (strlen(token) <= 3) {
			// first 3 letters must be sth like `h1=`,
			// so if there's info for us to read,
			// `token` must longer than 3 words
			break;
		}

		// ip
		strncpy(ip[numberOfMachines], &token[3], 16);

		// port
		token = strtok(NULL, "&");
		strncpy(port[numberOfMachines], &token[3], 6);

		// file
		token = strtok(NULL, "&");
		strncpy(file[numberOfMachines], &token[3], 100);

		token = strtok(NULL, "&");
		numberOfMachines++;
	}

	for (int i = 0; i < numberOfMachines; i++) {
		// insert clients
		clients.push_back(SockInfo(i, ip[i], port[i], file[i]));
	}

	char beforeTableHeader[] =
		"<body bgcolor=#336699>"
		"<font face=\"Courier New\" size=2 color=#FFFF99>"
		"<table width=\"800\" border=\"1\">"
		"<tr>";

	string tableHeader;
	for (int i = 0; i < numberOfMachines; i++) {
		tableHeader += "<td>" + clients[i].getIp() + "</td>";
	}

	string tableData = "</tr><tr>";
	for (int i = 0; i < numberOfMachines; i++) {
		tableData += "<td valign=\"top\" id=\"" + clients[i].getDomId() + "\"></td>";
	}

	char afterTableData[] =
		"</tr>"
		"</table>"
		"<span id=\"log\"style=\"color: azure;\"></span>";

	writeWrapper(sockfd, beforeTableHeader, strlen(beforeTableHeader));
	writeWrapper(sockfd, tableHeader.c_str(), tableHeader.length());
	writeWrapper(sockfd, tableData.c_str(), tableData.length());
	writeWrapper(sockfd, afterTableData, strlen(afterTableData));
}

bool NP::CGI::connectServers(HWND& hwnd) {

	for (int i = 0; i < numberOfMachines; i++) {
		if (!(clients[i].connectServer(hwnd))) {
			print("ALL", "Client `" + clients[i].getDomId() + "` encounters error. Stop program.", IS_LOG | NP_IS_ERROR | NEED_NEWLINE);
			EditPrintf(hwndEdit, TEXT("Client `%d` encounters error. Stop program.\r\n", clients[i].getDomIdInInt()));
			return false;
		}
		// print intro
//		clients[i].print("Client id #" + to_string(i) + ": sockfd: " + to_string(clients[i].getSockFd()), IS_LOG | NEED_NEWLINE);
	}

	EditPrintf(hwndEdit, TEXT("ConnectServers completed.\r\n"));
	return true;
}

bool NP::SockInfo::connectServer(HWND& hwnd) {

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) {
		
		EditPrintf(hwndEdit, TEXT("=== Error: socket creation error ===\r\n"));
		return false;
	}

	this->sockfd = sock;

	if (WSAAsyncSelect(sock, hwnd, WM_SERVER_NOTIFY, FD_CONNECT | FD_CLOSE) == SOCKET_ERROR) {

		EditPrintf(hwndEdit, TEXT("=== Error: set select on FD_CONNECT error (%ld) ===\r\n", WSAGetLastError()));
		return false;
	}

	// Set up our socket address structure
	sockaddr_in res;
	res.sin_port = htons(atoi(this->port));
	res.sin_family = AF_INET;
	res.sin_addr.s_addr = inet_addr(this->ip);

	/*int err = bind(sock, (SOCKADDR *)&res, sizeof(res));
	if (err == SOCKET_ERROR) {
		EditPrintf(hwndEdit, TEXT("=== Error: binding error %ld ===\r\n", WSAGetLastError()));
		//WSACleanup();
		return FALSE;
	}*/

	if (connect(sock, (LPSOCKADDR)(&res), sizeof(res)) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
		EditPrintf(hwndEdit, TEXT("=== Error: connect error %ld ===\r\n", WSAGetLastError()));
		return false;
	}

	return true;
}

void NP::afterSelect(HWND& hwnd) {

	list<int> toCheck;
	for (int i = 0; i < clients.size(); i++) {
		if (!clients[i].getIsDone()) {
			toCheck.push_back(i);
		}
	}
	
	for (list<int>::iterator it = toCheck.begin(); it != toCheck.end(); it++) {
		
		if (clients[*it].getIsDone()) {
			continue;
		}
		
		EditPrintf(hwndEdit, TEXT("This is id %d, status = %d, canRead = %d, canWrite = %d\r\n"), *it, clients[*it].getSockStatus(), clients[*it].getCanRead(), clients[*it].getCanWrite());
		
		/*char str[15000];
		sprintf(str, "This is id %d, status = %d, canRead = %d, canWrite = %d\n", *it, clients[*it].getSockStatus(), clients[*it].getCanRead(), clients[*it].getCanWrite());
		output << str;
		output.flush();
		*/
		if (clients[*it].getSockStatus() == READING && clients[*it].getCanRead() == true) {

			string strRead = readWrapper(clients[*it].getSockFd());

			if (strRead.length() == 0) {
				// dont check this server immediately to avoid being stuck
				//toCheck.push_back(*it);
				continue;
			}

			// write to webpage
			clients[*it].print(strRead);
			//clients[*it].print(strRead, IS_LOG | NEED_NEWLINE);

			// only write when prompt is read
			if (strRead.find("% ") != string::npos) {

				// finish reading, write next time.
				clients[*it].setSockStatus(WRITING);

				// remove from read fd & put in write fd
				WSAAsyncSelect(clients[*it].getSockFd(), hwnd, WM_SERVER_NOTIFY, FD_CLOSE | FD_WRITE);
				clients[*it].setCanRead(false);

				// if now can write, write now
				if (clients[*it].getCanWrite()) {
					toCheck.push_back(*it);
					continue;
				}

			} else if (clients[*it].hasSentExit()) {

				// if the client is ready to exit
				// remove from read fd and set status to F_DONE
				WSAAsyncSelect(clients[*it].getSockFd(), hwnd, WM_SERVER_NOTIFY, FD_CLOSE);

				clients[*it].setSockStatus(DONE);
			}

		} else if (clients[*it].getSockStatus() == WRITING && clients[*it].getCanWrite() == true) {

			// get input from file
			string input;
			getline(clients[*it].getFile(), input);
			input += "\n";  // indicate EOL

			// if exit is sent, exit after next write
			if (input.find("exit") == 0) {
				clients[*it].setExitStatus(true);
			}

			// write to webpage
			clients[*it].print(input, NEED_BOLD);
			//clients[*it].print(input, IS_LOG | NEED_NEWLINE);  // log

			// write to server
			writeWrapper(clients[*it].getSockFd(), input.c_str(), input.size());

			// finish writing, read next time.
			clients[*it].setSockStatus(READING);

			// remove from write fd & put in read fd
			WSAAsyncSelect(clients[*it].getSockFd(), hwnd, WM_SERVER_NOTIFY, FD_CLOSE | FD_READ);

			clients[*it].setCanWrite(false);
		}

	}

}

void NP::log(string ch, bool error, bool newline, bool prefix) {
	log(ch.c_str(), error, newline, prefix);
}

void NP::log(const char* ch, bool error, bool newline, bool prefix) {

#ifndef DEBUG
    return;
#endif
    
	if (error && prefix) {
		EditPrintf(hwndEdit, TEXT("ERROR: "));
		output << "ERROR: ";
	} else if (prefix) {
		EditPrintf(hwndEdit, TEXT("LOG: "));
		output << "LOG: ";
	}

	EditPrintf(hwndEdit, TEXT("%s"), ch);
	output << ch;

	if (newline) {
		EditPrintf(hwndEdit, TEXT("\r\n"));
		output << "\n";
	}

	output.flush();
}

void NP::err(string str) {

	log(str.c_str(), true);
	perror(str.c_str());
	//exit(1);
}

/**
*  Set buffer content
*
*  @param ch Character array
*/
void NP::setBuffer(const char* ch) {
	ZeroMemory(buffer, sizeof(buffer));
	strcpy(buffer, ch);
}

/**
*  Reset buffer content
*/
void NP::resetBuffer() {
	ZeroMemory(buffer, sizeof(buffer));
}

char* NP::readWrapper(int sockfd) {

	resetBuffer();
	int n = recv(sockfd, buffer, sizeof(buffer), 0);
	if (n < 0) {

//		NP::err("read error");
		return "";

	} else if (n == 0) {

		return "";
	}

#ifdef DEBUG
	NP::log("read(size = " + to_string(sizeof(buffer)) + ", n = " + to_string(n) + "): \n" + string(buffer));
#endif

	return buffer;
}

void NP::writeWrapper(int sockfd, const char buffer[], size_t size, bool flag) {
	
	if (!(flag & BUFFER_DONT_RESET)) {
		resetBuffer();
	}

	int n = send(sockfd, buffer, size, 0);
#ifdef DEBUG
	NP::log("write(size = " + to_string(size) + ", n = " + to_string(n) + ", via fd " + to_string(sockfd) + "): \n" + string(buffer));
#endif

	if (n == SOCKET_ERROR) {
		NP::err("write error[" + to_string(WSAGetLastError()) + "]: " + string(buffer));
	}
}

void NP::requestHandler(int sockfd, char req[MAX_SIZE], HWND hwnd) {

	// GET / HTTP/1.1
	char* method = strtok(req, " ");
	char* pathAndParam = strtok(NULL, " ");

	char* paramFromGet = strchr(pathAndParam, '?');    // param may be null
	if (paramFromGet != NULL) {
		paramFromGet++; // get the next character from '?'
	}
	char* path = strtok(pathAndParam, "?");
	char* ext = strrchr(path, '.');  // ext may be null
	path = &path[1];    // change to cwd
	log("path = " + string(path) + ", get = " + (paramFromGet == NULL ? string("NULL") : string(paramFromGet)) + ", ext = " + (ext == NULL ? string("NULL") : string(ext)));

	// validate request
	// only GET is supported
	if (strcasecmp(method, "GET")) {
		generateErrorPage(sockfd, 405, "Method Not Allowed");
		return;
	}

	// handle GET param
	if (paramFromGet != NULL) {
		_putenv_s("QUERY_STRING", paramFromGet);
	}

	// if hw3.cgi i.e. path = "hw3.cgi"
	if (!strncmp(path, "hw3.cgi", 7)) {   

		NP::log("this is hw3.cgi");

		sendHeader(sockfd, 200, "OK", getMimeType(ext));
		CGI::handler(sockfd, hwnd);
		return;
	}

	// validate path
	int lenPath = strlen(path);
	for (int i = 1; i < lenPath; i++) {
		if (path[i - 1] == '.' && path[i] == '.') {
			generateErrorPage(sockfd, 403, "Forbidden (parent directory)");
			return;
		}
	}

	struct stat statbuf;
	if (stat(path, &statbuf) == -1) {
		generateErrorPage(sockfd, 404, "Not Found");
		return;

	} else if (S_ISDIR(statbuf.st_mode)) {
		generateErrorPage(sockfd, 403, "Forbidden (directory listing)");
		return;
	}

	if (ext == NULL || getMimeType(ext) == NULL) {
		generateErrorPage(sockfd, 415, "Unsupported Media Type");
		return;
	}


	/**
	*  Send headers
	*/
	sendHeader(sockfd, 200, "OK", getMimeType(ext));

	/**
	*  Read file
	*/
	if (strcmp(ext, MIME_CGI)) {    // if not cgi
		
		NP::log("this is NOT cgi");

		// read file content and write to web
		HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, 
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
		DWORD nBytesToRead = MAX_SIZE;
		DWORD dwBytesRead = 0;
		DWORD dwFileSize = GetFileSize(hFile, NULL);
		OVERLAPPED stOverlapped = { 0 };
		
		if (FALSE == ReadFile(hFile, buffer, nBytesToRead, &dwBytesRead, &stOverlapped))
		{

			log("Terminal failure: Unable to read from file.\n GetLastError=%08x\n", GetLastError());
			CloseHandle(hFile);
			return;
		}

		writeWrapper(sockfd, buffer, dwBytesRead, BUFFER_DONT_RESET);

		CloseHandle(hFile);
	} 

	cleanupWebsock();
}

/**
*	Send header.
*  For `MIME_CGI`: won't output `Content-Type:` line and the newline after header.
*
*	@param status	HTTP status code
*	@param title	status description
*	@param mime		MIME type ('MIME_CGI')
*/
void NP::sendHeader(int sockfd, int status, const char* title, const char* mime) {

	// HTTP/1.1 200 OK\r\n
	// Server: CYH_SERVER/44.10\r\n
	// Connection: close\r\n
	// Content-Type: text/html\r\n\n

	char header[HEADER_SIZE];
	// HTTP/1.1 200 OK\r\n
	sprintf(header, "%s %d %s\r\n", PROTOCOL, status, title);
	// Server: CYH_SERVER/44.10\r\n
	sprintf(header + strlen(header), "Server: %s\r\n", SERVER);
	// Connection: close\r\n
	sprintf(header + strlen(header), "Connection: close\r\n");

	// Content-Type: text/html\r\n\n
	if (mime == NULL || strcmp(mime, MIME_CGI)) {   // if not cgi
		sprintf(header + strlen(header), "Content-Type: %s\r\n\n", mime);
	}

	writeWrapper(sockfd, header, strlen(header));
}

void NP::generateErrorPage(int sockfd, int status, const char* title) {

	sendHeader(sockfd, status, title, "text/html");

	char body[1000];
	sprintf(body, "<html><title>%d %s</title>", status, title);
	sprintf(body + strlen(body), "<body><h1>%d %s</h1></body></html>", status, title);
	writeWrapper(sockfd, body, strlen(body));
	cleanupWebsock();
}

const char* NP::getMimeType(char* name) {

	if (name == NULL) {
		return NULL;
	}

	// get extension
	char* ext = strrchr(name, '.');

	if (!ext) {
		return NULL;
	}

	if (strcmp(ext, ".cgi") == 0) return MIME_CGI;
	if (strcmp(ext, ".txt") == 0) return "text/plain";
	if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)  return "text/html";
	if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)  return "image/jpeg";
	if (strcmp(ext, ".gif") == 0)   return "image/gif";
	if (strcmp(ext, ".png") == 0)   return "image/png";

	return NULL;
}

void NP::cleanupWebsock() {

	if (websock == NULL) {
		return;
	}

	int iResult = closesocket(websock->getSockFd());
	if (iResult == SOCKET_ERROR) {
		wprintf(L"closesocket failed with error = %ld\n", WSAGetLastError());
	}
	websock->setIsDone(true);
	delete websock;
	WSACleanup();
}

NP::SockInfo& NP::findSockInfoBySockfd(SOCKET sockfd) {

	for (vector<NP::SockInfo>::iterator it = clients.begin(); clients.size() && it != clients.end(); it++) {

		if (it->getSockFd() == sockfd) {
			return *it;
		}

	}

	return SockInfo();
}

void NP::SockInfo::print(string msg, int flag) {
	NP::print(getDomId(), msg, flag);
}

void NP::print(string domId, string msg, int flag) {
	
	string toSend = "";

	if (flag & IS_LOG) {
		time(&now);
		log("<script>document.all['log'].innerHTML += \"[" + domId + ", " + to_string(difftime(now, start)) + "s] ");
		toSend += ("<script>document.all['log'].innerHTML += \"[" + domId + ", " + to_string(difftime(now, start)) + "s] ");
	} else {
		log("<script>document.all['" + domId + "'].innerHTML += \"");
		toSend += ("<script>document.all['" + domId + "'].innerHTML += \"");
	}

	if (flag & NP_IS_ERROR) {
		log("<span style=\'color: red; font-weight:700;\'>ERROR:</span> ");
		toSend += ("<span style=\'color: red; font-weight:700;\'>ERROR:</span> ");
	}

	/**
	*	Process msg
	*/
	size_t found;

	// replace `&` with `&amp;`
	found = msg.find("&");
	while (found != string::npos) {
		msg.replace(found, 1, "&amp;");
		found = msg.find("&");
	}

	// replace `"` with `&quot;`
	found = msg.find("\"");
	while (found != string::npos) {
		msg.replace(found, 1, "&quot;");
		found = msg.find("\"");
	}

	// replace `'` with `&apos;`
	found = msg.find("'");
	while (found != string::npos) {
		msg.replace(found, 1, "&apos;");
		found = msg.find("'");
	}

	// replace ` ` with `&nbsp;`
	found = msg.find(" ");
	while (found != string::npos) {
		msg.replace(found, 1, "&nbsp;");
		found = msg.find(" ");
	}

	// replace `<` with `&lt;`
	found = msg.find("<");
	while (found != string::npos) {
		msg.replace(found, 1, "&lt;");
		found = msg.find("<");
	}

	// replace `>` with `&gt;`
	found = msg.find(">");
	while (found != string::npos) {
		msg.replace(found, 1, "&gt;");
		found = msg.find(">");
	}

	// replace `\n` with `<br/>`
	found = msg.find("\n");
	while (found != string::npos) {
		msg.replace(found, 1, "<br/>");
		found = msg.find("\n");
	}

	// remove `\r`
	found = msg.find("\r");
	while (found != string::npos) {
		msg.replace(found, 1, "");
		found = msg.find("\r");
	}

	if (flag & NEED_BOLD) {
		msg = "<b>" + msg + "</b>";
	}

	log(msg);
	toSend += msg;

	if (flag & NEED_NEWLINE) {
		log("<br/>");
		toSend += "<br/>";
	}

	log("\";</script>\n");
	toSend += ("\";</script>\n");

	writeWrapper(ssock, toSend.c_str(), toSend.length());
}

