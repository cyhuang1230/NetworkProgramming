//  NCTU CS. Network Programming Assignment 3 Part II
//  HTTP Server with CGI support. Please refer to hw3spec.txt for more details.

//  Code by Denny Chien-Yu Huang, 10/25/15.
//  Github: http://cyhuang1230.github.io/

/**
 *	Main idea:
 *      -
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <utility>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

#define MAX_SIZE 15001
#define DEFAULT_PORT 4411

//#define DEBUG 1

/// HW3 Related
#define PROTOCOL "HTTP/1.1"
#define SERVER "CYH_NP_SERVER/44.10"
#define HEADER_SIZE 8192
#define MIME_CGI ".cgi"

#define BUFFER_DONT_RESET (1 << 0)

char buffer[MAX_SIZE];

namespace NP {
    
    int curSockfd = STDOUT_FILENO;
    
   	void log(string str, bool error = false, bool newline = true, bool prefix = true) {
		
		if (error && prefix) {
			cout << "[" << getpid() << "] ERROR: ";
		} else if (prefix) {
			cout << "[" << getpid() << "] LOG: ";
		}
		
		cout << str;
        if (newline) {
            cout << endl;
        }
        
		cout.flush();
	}
    
    void log(char* ch, bool error = false, bool newline = true, bool prefix = true) {
        
        if (error && prefix) {
            printf("ERROR: ");
        } else if (prefix) {
            printf("LOG: ");
        }
        
        printf("%s", ch);
        if (newline) {
            printf("\n");
        }
        
        fflush(stdout);
    }
    
	void err(string str) {

		log(str, true);
		perror(str.c_str());
		exit(1);
	}
    
	/**
	 *  Set buffer content
	 *
	 *  @param ch Character array
	 */
	void setBuffer(const char* ch) {
		bzero(buffer, MAX_SIZE);
		strcpy(buffer, ch);
	}
	
	/**
	 *  Reset buffer content
	 */
	void resetBuffer() {
		bzero(buffer, MAX_SIZE);
	}
	
    void writeWrapper(int sockfd, const char buffer[], size_t size, bool flag = 0) {

        if (!(flag & BUFFER_DONT_RESET)) {
            resetBuffer();
        }
        
        int n = write(sockfd, buffer, size);
#ifdef DEBUG
        NP::log("write(size = " + to_string(size) + ", n = " + to_string(n) + ", via fd " + to_string(sockfd) +"): \n" + string(buffer));
#endif
        
        if (n < 0) {
            NP::err("write error: " + string(buffer));
        }
    }
	
	char* readWrapper(int sockfd, bool needPrompt = true) {
		
        resetBuffer();
		int n = read(sockfd, buffer, sizeof(buffer));
		if (n < 0) {
		
            NP::err("read error");

        } else if (n == 0) {
            
            return NULL;
        }
        
#ifdef DEBUG
		NP::log("read(size = " + to_string(sizeof(buffer)) + ", n = " + to_string(n) + "): \n" + string(buffer));
#endif

		return buffer;
	}
    
    void dup2(const int newfd, const int oldfd, bool needOutput = true) {
        
        if (::dup2(newfd, oldfd) == -1) {
            NP::err("dup2(" + to_string(newfd) + "," + to_string(oldfd) + ") errror " + to_string(errno));
        }
        
        if (needOutput) {
            NP::log("dup2(" + to_string(newfd) + "," + to_string(oldfd) + ") done");
        }
    }
    
    /// HW3 Related
    
    void requestHandler();

    /**
     *	Send header.
     *  For `MIME_CGI`: won't output `Content-Type:` line and the newline after header.
     *
     *	@param status	HTTP status code
     *	@param title	status description
     *	@param mime		MIME type ('MIME_CGI')
     */
    void sendHeader(int status, const char* title, const char* mime);
    
    void generateErrorPage(int status, const char* title);
    
    const char* getMimeType(char* name);
}

int main(int argc, const char * argv[]) {
	
	int sockfd, newsockfd, childpid, portnum;
	struct sockaddr_in cli_addr, serv_addr;
	socklen_t clilen;

	if (argc < 2) {	// if no port provided
		
		portnum = DEFAULT_PORT;
		
	} else {
		
		// read port from input
		portnum = atoi(argv[1]);
	}
    
    // SIGCHLD to prevnet zombie process
	struct sigaction sigchld_action;
	sigchld_action.sa_handler = SIG_DFL;
	sigchld_action.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sigchld_action, NULL);
    
#ifdef DEBUG
	NP::log("Starting server using port: " + to_string(portnum));
#endif
	
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		NP::err("Cannot open socket!");
	}

	/**
	 *  Bind
	 */
	bzero((char*)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(portnum);
	
	if (::bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
		NP::err("Bind error");
	}
	
	/**
	 *  Listen
	 */
	listen(sockfd, 5);
	
	/**
	 *  Accept
	 */
	while (1) {
#ifdef DEBUG
        NP::log("Waiting for connections...");
#endif
		clilen = sizeof(cli_addr);
		newsockfd = ::accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
#ifdef DEBUG
        NP::log("Connection accepted (newsockfd = " + to_string(newsockfd) + ")");
#endif
		if (newsockfd < 0) {
			NP::err("accept error");
		}

        if ((childpid = fork()) < 0) {
        
            NP::err("fork error");

        } else if (childpid == 0) { // child process
            
            close(sockfd);

            // prepare cgi
            NP::curSockfd = newsockfd;
            NP::requestHandler();
            
            close(newsockfd);
            exit(EXIT_SUCCESS);
            
        } else {
            
            close(newsockfd);
        }
	}
	
	return 0;
}

void NP::requestHandler() {

    /**
     *  Read HTTP request
     */
    
    readWrapper(curSockfd);
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
        generateErrorPage(405, "Method Not Allowed");
    }
    
    // validate path
    int lenPath = strlen(path);
    for (int i = 1; i < lenPath; i++) {
        if (path[i-1] == '.' && path[i] == '.') {
            generateErrorPage(403, "Forbidden (parent directory)");
        }
    }
    
    struct stat statbuf;
    if (stat(path, &statbuf) == -1) {
        generateErrorPage(404, "Not Found");
    } else if (S_ISDIR(statbuf.st_mode)) {
        generateErrorPage(403, "Forbidden (directory listing)");
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
        while ((readLen = read(fileFd, buffer, sizeof(buffer)))) {
            writeWrapper(curSockfd, buffer, readLen, BUFFER_DONT_RESET);
        }
        
    } else {    // if cgi
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

void NP::sendHeader(int status, const char* title, const char* mime) {

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

    writeWrapper(curSockfd, header, strlen(header));
}

void NP::generateErrorPage(int status, const char* title) {
    
    sendHeader(status, title, "text/html");
    
    char body[1000];
    sprintf(body, "<html><title>%d %s</title>", status, title);
    sprintf(body + strlen(body), "<body><h1>%d %s</h1></body></html>", status, title);
    writeWrapper(curSockfd, body, strlen(body));
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