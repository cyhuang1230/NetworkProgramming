#include <windows.h>
#include <list>

#include <string>
#include <cstring>
using namespace std;

#include "resource.h"

#define SERVER_PORT 7799

#define WM_SOCKET_NOTIFY (WM_USER + 1)

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf (HWND, TCHAR *, ...);
//=================================================================
//	Global Variables
//=================================================================
list<SOCKET> Socks;

// NP
#define DEBUG 1
#define strcasecmp _stricmp
#define MAX_SIZE 15001

#define PROTOCOL "HTTP/1.1"
#define SERVER "CYH_NP_SERVER/44.10"
#define HEADER_SIZE 8192
#define MIME_CGI ".cgi"

#define BUFFER_DONT_RESET (1 << 0)

char buffer[MAX_SIZE];

namespace NP {

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
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	
	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;

	static HWND hwndEdit;
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
			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_ACCEPT:
					ssock = accept(msock, NULL, NULL);
					Socks.push_back(ssock);
					EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, Socks.size());
					break;

				case FD_READ:
				//Write your code for read event here.
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_READ\r\n"), ssock);
					
					break;
				case FD_WRITE:
				//Write your code for write event here
					EditPrintf(hwndEdit, TEXT("=== Sock #%d FD_WRITE\r\n"), ssock);
					send(ssock, "AH!!!", 5, 0);
					break;

				case FD_CLOSE:
					break;
			};
			break;
		
		default:
			return FALSE;


	};

	return TRUE;
}

int EditPrintf (HWND hwndEdit, TCHAR * szFormat, ...)
{
     TCHAR   szBuffer [1024] ;
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

void NP::log(string ch, bool error, bool newline, bool prefix) {
	log(ch.c_str(), error, newline, prefix);
}

void NP::log(const char* ch, bool error, bool newline, bool prefix) {

	if (error && prefix) {
		printf("ERROR: ");
	}
	else if (prefix) {
		printf("LOG: ");
	}

	printf("%s", ch);
	if (newline) {
		printf("\n");
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
	memset(buffer, 0, sizeof(buffer));
	strcpy(buffer, ch);
}

/**
*  Reset buffer content
*/
void NP::resetBuffer() {
	memset(buffer, 0, sizeof(buffer));
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

	readWrapper(sockfd);
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
	printf("path = %s, get = %s, ext = %s\n", path, paramFromGet, ext);

	// validate request
	// only GET is supported
	if (strcasecmp(method, "GET")) {
		generateErrorPage(sockfd, 405, "Method Not Allowed");
	}

	// validate path
	int lenPath = strlen(path);
	for (int i = 1; i < lenPath; i++) {
		if (path[i - 1] == '.' && path[i] == '.') {
			generateErrorPage(sockfd, 403, "Forbidden (parent directory)");
		}
	}

	struct stat statbuf;
	if (stat(path, &statbuf) == -1) {
		generateErrorPage(sockfd, 404, "Not Found");
	}
	else if (S_ISDIR(statbuf.st_mode)) {
		generateErrorPage(sockfd, 403, "Forbidden (directory listing)");
	}

	// handle GET param
	if (paramFromGet != NULL) {
		setenv("QUERY_STRING", paramFromGet, 1);
	}

	/**
	*  Send headers
	*/
	sendHeader(200, "OK", getMimeType(ext));

	NP::log("after header is sent");

	/**
	*  Read file
	*/
	if (strcmp(ext, MIME_CGI)) {    // if not cgi
		NP::log("this is NOT cgi");

		// read file content and write to web
		int fileFd = open(path, O_RDONLY);
		int readLen;
		while ((readLen = recv(fileFd, buffer, sizeof(buffer), 0))) {
			writeWrapper(sockfd, buffer, readLen, BUFFER_DONT_RESET);
		}

	}
	else {    // if cgi
		NP::log("this is cgi");

		// fork & dup sockfd to stdout
		switch (fork()) {
		case -1:
			NP::err("fork error");
			break;

		case 0: // child
			NP::log("this is child");
			// set fd
			dup2(curSockfd, STDOUT_FILENO, false);

			// exec
			if (execl(path, NULL) == -1) {

				NP::err("Child exec error");
			}


		default:    // parent
					//                close(curSockfd);                
			break;
		}
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
	if (strcmp(mime, MIME_CGI)) {   // if not cgi
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
	exit(EXIT_SUCCESS);
}

const char* NP::getMimeType(char* name) {

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