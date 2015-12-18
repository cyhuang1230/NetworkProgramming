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

// NP
#define DEBUG 1

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
#define IS_ERROR (1 << 1)
#define NEED_NEWLINE (1 << 2)
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

		// for Server
		SockStatus sockstatus = CONNECTING;
		bool canRead = false;
		bool canWrite = false;
		bool sentExit = false;
		char ip[INET_ADDRSTRLEN];
		char port[6];
		char filename[FILENAME_LENGTH];
		string domId;
		ifstream& file = ifstream();

	public:

		SockInfo() {}

		SockInfo(SOCKET newSock) {
			sockfd = newSock;
			socktype = Browser;
		}

		SockInfo(int id, const char* iIp, const char* iPort, const char* iFile) {

			socktype = Server;
			domId = "m" + to_string(id);
			strncpy(ip, iIp, INET_ADDRSTRLEN);
			strncpy(port, iPort, 6);
			strncpy(filename, iFile, FILENAME_LENGTH);
		}

		// assign operator
		SockInfo& operator=(const SockInfo& newSock) {

			sockstatus = newSock.sockstatus;
			canRead = newSock.canRead;
			canWrite = newSock.canWrite;
			sentExit = newSock.sentExit;
			strncpy(ip, newSock.ip, INET_ADDRSTRLEN);
			strncpy(port, newSock.port, 6);
			strncpy(filename, newSock.filename, FILENAME_LENGTH);
			domId = newSock.domId;

			// we *logically* only use assign operator when bootstrap
			// therefore, we open file in ifstream
			file = ifstream(filename, ifstream::in);

			return *this;
		}

		string getIp() {
			return string(ip);
		}

		string getDomId() {
			return domId;
		}

		SOCKET getSockFd() {
			return sockfd;
		}

		SockType getSockType() {
			return socktype;
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

		void setSockStatus(SockStatus newStatus) {
			sockstatus = newStatus;
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

	void requestHandler(int sockfd);

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

	void cleanup(SOCKET sockfd);

	void afterSelect(HWND hwnd);

	namespace CGI {

		int numberOfMachines = 0;

		void handler(int sockfd, char* param);

		void printHeader(int sockfd);

		void printBody(int sockfd);

		void printFooter(int sockfd);
	}
	
}

using namespace NP;
list<NP::SockInfo> Socks;
vector<NP::SockInfo> clients;
char buffer[MAX_SIZE];

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf(HWND, TCHAR *, ...);
static HWND hwndEdit;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	
	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;

	static SOCKET msock, ssock;
	static struct sockaddr_in sa;

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
						WSACleanup();
						return TRUE;
					}

					err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE);

					if ( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
						closesocket(msock);
						WSACleanup();
						return TRUE;
					}

					//fill the address info about server
					sa.sin_family		= AF_INET;
					sa.sin_port			= htons(SERVER_PORT);
					sa.sin_addr.s_addr	= INADDR_ANY;

					//bind socket
					err = bind(msock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));

					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
						WSACleanup();
						return FALSE;
					}

					err = listen(msock, 2);
		
					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
						WSACleanup();
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
			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_CONNECT:
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_CONNECT ===\r\n"), curSockfd);

					// connection is established
					// start listening FD_READ for prompt
					WSAAsyncSelect(curSockfd, hwnd, WM_SOCKET_NOTIFY, FD_CLOSE | FD_READ);
					NP::findSockInfoBySockfd(curSockfd).setSockStatus(READING);
					break;

				case FD_ACCEPT:
					ssock = accept(msock, NULL, NULL);
					Socks.push_back(NP::SockInfo(ssock));
					EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, Socks.size());
					break;

				case FD_READ:
				{
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_READ ===\r\n"), curSockfd);
					
					if (NP::findSockInfoBySockfd(curSockfd).getSockType() == Browser) {

						requestHandler(NP::findSockInfoBySockfd(curSockfd).getSockFd());
						cleanup(curSockfd);

					} else {

						NP::findSockInfoBySockfd(curSockfd).setCanRead(true);
					}

					break;
				}

				case FD_WRITE:
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_WRITE ===\r\n"), curSockfd);
					if (NP::findSockInfoBySockfd(curSockfd).getSockType() == Server) {
						NP::findSockInfoBySockfd(curSockfd).setCanWrite(true);
					}
					break;

				case FD_CLOSE:
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_CLOSE ===\r\n"), curSockfd);
					if (NP::findSockInfoBySockfd(curSockfd).getSockType() == Server) {
						cleanup(curSockfd);
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
void NP::CGI::handler(int sockfd, char* param) {

	time(&start);

	printHeader(sockfd);

	printBody(sockfd);

	//executeClientPrograms();

	printFooter(sockfd);

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


void NP::afterSelect(HWND hwnd) {

	for (list<SockInfo>::iterator it = Socks.begin(); it != Socks.end(); it++) {

		// this is only for Server type
		if (it->getSockType() == Browser) {
			continue;
		}

		if (it->getSockStatus() == READING && it->getCanRead() == true) {

			string strRead = readWrapper(it->getSockFd());

			// write to webpage
			it->print(strRead);

			// only write when prompt is read
			if (strRead.find("% ") != string::npos) {

				// finish reading, write next time.
				it->setSockStatus(WRITING);

				// remove from read fd & put in write fd
				WSAAsyncSelect(it->getSockFd(), hwnd, WM_SOCKET_NOTIFY, FD_CLOSE | FD_WRITE);
				it->setCanRead(false);

			} else if (it->hasSentExit()) {

				// if the client is ready to exit
				// remove from read fd and set status to F_DONE
				WSAAsyncSelect(it->getSockFd(), hwnd, WM_SOCKET_NOTIFY, FD_CLOSE);

				it->setSockStatus(DONE);
			}

		} else if (it->getSockStatus() == WRITING && it->getCanWrite() == true) {

			// get input from file
			string input;
			getline(it->getFile(), input);
			input += "\n";  // indicate EOL

			// if exit is sent, exit after next write
			if (input.find("exit") == 0) {
				it->setExitStatus(true);
			}

			// write to webpage
			it->print(input);

			// write to server
			writeWrapper(it->getSockFd(), input.c_str(), input.size());

			// finish writing, read next time.
			it->setSockStatus(READING);

			// remove from write fd & put in read fd
			WSAAsyncSelect(it->getSockFd(), hwnd, WM_SOCKET_NOTIFY, FD_CLOSE | FD_READ);
		}

	}

}

void NP::log(string ch, bool error, bool newline, bool prefix) {
	log(ch.c_str(), error, newline, prefix);
}

void NP::log(const char* ch, bool error, bool newline, bool prefix) {

	if (error && prefix) {
		EditPrintf(hwndEdit, TEXT("ERROR: "));
	} else if (prefix) {
		EditPrintf(hwndEdit, TEXT("LOG: "));
	}

	EditPrintf(hwndEdit, TEXT("%s"), ch);

	if (newline) {
		EditPrintf(hwndEdit, TEXT("\r\n"));
	}

	fflush(stdout);
}

void NP::err(string str) {

	log(str.c_str(), true);
	perror(str.c_str());
	exit(1);
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

		NP::err("read error");

	}
	else if (n == 0) {

		return NULL;
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

	if (n < 0) {
		NP::err("write error: " + string(buffer));
	}
}

void NP::requestHandler(int sockfd) {

	/**
	*  Read HTTP request
	*/
	NP::readWrapper(sockfd);
	char req[MAX_SIZE];
	strcpy(req, buffer);

	// GET / HTTP/1.1
	char* method = strtok(req, " ");
	char* pathAndParam = strtok(NULL, " ");

	char* paramFromGet = strchr(pathAndParam, '?');    // param mat be null
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
		//_putenv_s("QUERY_STRING", paramFromGet);
	}

	// if hw3.cgi i.e. path = "hw3.cgi"
	if (!strncmp(path, "hw3.cgi", 7)) {   

		NP::log("this is hw3.cgi");

		sendHeader(sockfd, 200, "OK", getMimeType(ext));
		CGI::handler(sockfd, paramFromGet);
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

	NP::log("after header is sent");

	/**
	*  Read file
	*/
	if (strcmp(ext, MIME_CGI)) {    // if not cgi
		
		NP::log("this is NOT cgi");

		// read file content and write to web
		HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, 
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
		OVERLAPPED ol = { 0 };

		if (FALSE == ReadFileEx(hFile, buffer, sizeof(buffer), &ol, NULL))
		{

			log("Terminal failure: Unable to read from file.\n GetLastError=%08x\n", GetLastError());
			CloseHandle(hFile);
			return;
		}

		writeWrapper(sockfd, buffer, strlen(buffer), BUFFER_DONT_RESET);

		CloseHandle(hFile);
	} 
	
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

void NP::cleanup(SOCKET sockfd) {

	closesocket(sockfd);

	if (Socks.size() == 0) {
		return;
	}

	// remove sockinfo
	for (list<NP::SockInfo>::iterator it = Socks.begin(); Socks.size() && it != Socks.end();) {

		if (it->getSockFd() == sockfd) {
			
			it = Socks.erase(it);
		
		} else {
		
			it++;
		}
	}
}

NP::SockInfo& NP::findSockInfoBySockfd(SOCKET sockfd) {

	if (Socks.size() == 0) {
		return SockInfo();
	}

	// remove sockinfo
	for (list<NP::SockInfo>::iterator it = Socks.begin(); Socks.size() && it != Socks.end(); it++) {

		if (it->getSockFd() == sockfd) {
			return *it;
		}

		if (!Socks.size()) {
			break;
		}
	}
}

void NP::SockInfo::print(string msg, int flag) {
	NP::print(domId, msg, flag);
}

void NP::print(string domId, string msg, int flag) {

	if (flag & IS_LOG) {
		time(&now);
		log("<script>document.all['log'].innerHTML += \"[" + domId + ", " + to_string(difftime(now, start)) + "s] )");
	}
	else {
		log("<script>document.all['" + domId + "'].innerHTML += \"");
	}

	if (flag & IS_ERROR) {
		log("<span style=\'color: red; font-weight:700;\'>ERROR:</span> ");
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

	log(msg);

	if (flag & NEED_NEWLINE) {
		log("<br/>");
	}

	log("\";</script>\n");
}

