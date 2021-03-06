//  NCTU CS. Network Programming Assignment 4 SOCKS4 Server

//  Code by Denny Chien-Yu Huang, 01/02/16.
//  Github: https://cyhuang1230.github.io/

/**
 *	Main idea:
 *      - CONNECT mode:
 *          accept -> read SOCKS req -> check firewall rules ->
 *              connect to dest host -> send SOCKS reply -> redirect data
 *      - BIND mode:
 *          accept -> read SOCKS req -> check firewall rules ->
 *              bind socket -> listen -> send SOCKS reply -> 
 *              accpet from server -> send SOCKS reply (to original client) -> redirect data
 *      - When redirect data, should avoid using `strncpy` since it may contain `'\0'` in data,
 *          which occasionally leads to data error (less bytes are copied than expected).
 *      - When `bind` with system-assigned port, give `0` to `sockaddr_in.sin_port`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <list>
#include <utility>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

#define BUFFER_SIZE 150001
#define DEFAULT_PORT 4411
#define MAX_USER 100
#define MAX_USER_ID_LEN 50
#define MAX_DOMAIN_NAME_LEN 1024
//#define DEBUG 1

// for log flag
#define IS_LOG (1 << 0)
#define IS_ERROR (1 << 1)
#define NEED_NEWLINE (1 << 2)
#define NEED_BOLD (1<<3)
char buffer[BUFFER_SIZE+1];
enum SOCKS_TYPE {CONNECT = 1, BIND, DONTCARE};

namespace NP {
    
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
    
    void log_ch(char* ch, bool error = false, bool newline = true, bool prefix = true) {
        
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
		bzero(buffer, BUFFER_SIZE);
		strcpy(buffer, ch);
	}
	
	/**
	 *  Reset buffer content
	 */
	void resetBuffer() {
		bzero(buffer, BUFFER_SIZE);
	}
	
    void writeWrapper(int sockfd, const char buffer[], size_t size) {
        
       ssize_t n = write(sockfd, buffer, size);
#ifdef DEBUG
        NP::log("write(size = " + to_string(size) + ", n = " + to_string(n) + ", via fd " + to_string(sockfd) +")\n");
//        NP::log("write(size = " + to_string(size) + ", n = " + to_string(n) + ", via fd " + to_string(sockfd) +"): [Only show first 10 characters]\n" + string(buffer).substr(0, 10) );
#endif
        
        if (n < 0) {
            NP::err("write error: " + string(buffer));
        }
    }
    
    ssize_t readWrapper(int sockfd) {
        
        resetBuffer();
        ssize_t n = read(sockfd, buffer, sizeof(buffer));
        if (n < 0) {
            
            NP::err("read error");
            
        } else if (n == 0) {
            
            return -1;
        }
        
#ifdef DEBUG
        NP::log("read(size = " + to_string(sizeof(buffer)) + ", n = " + to_string(n) + ", via fd " + to_string(sockfd) +")\n");
//        NP::log("read(size = " + to_string(sizeof(buffer)) + ", n = " + to_string(n) + ", via fd " + to_string(sockfd) +"):  [Only show first 10 characters]\n" + string(buffer).substr(0, 10));
#endif
        
        return n;
    }
    
    void dup2(const int newfd, const int oldfd, bool needOutput = true) {
        
        if (::dup2(newfd, oldfd) == -1) {
            NP::err("dup2(" + to_string(newfd) + "," + to_string(oldfd) + ") errror " + to_string(errno));
        }
        
        if (needOutput) {
            NP::log("dup2(" + to_string(newfd) + "," + to_string(oldfd) + ") done");
        }
    }
    
/// HW4
    string clientAddr = "(null)";
    unsigned short clientPort = 0;
    
    void processRequest(int);

    void sendSockResponse(int ssock, bool isGranted, SOCKS_TYPE type, char* sockreq);

    void redirectData(int ssock, int rsock);

    bool isAllowedToConnect(SOCKS_TYPE type, char* ip);
    
    int connectDestHost(char dst_ip[INET_ADDRSTRLEN], unsigned short dst_port);
    
    void print(int ssock, string msg, int flag = 0);
}

int main(int argc, const char * argv[]) {
	
	int sockfd, newsockfd, childpid, portnum;
	struct sockaddr_in client_addr, serv_addr;
    char client_addr_str[INET_ADDRSTRLEN]; // client ip addr
    int client_port;   // client port
	socklen_t client_addr_len;
    
	if (argc < 2) {	// if no port provided
		
		portnum = DEFAULT_PORT;
		
	} else {
		
		// read port from input
		portnum = atoi(argv[1]);
	}
    
    /**
     *	Initialization
     */
#ifndef LOCAL
    // set PATH to `bin:.` to satisfy requirement
    setenv("PATH", "bin:.", 1);
    
    // change directory to ras to satisfy requirement
    if (chdir("ras") == -1) {
        NP::err("chdir error");
    }
#endif
    
    // SIGCHLD to prevnet zombie process
	struct sigaction signal_action;
    signal_action.sa_handler = SIG_DFL;
	signal_action.sa_flags = SA_NOCLDWAIT | SA_RESTART;
    sigaction(SIGCHLD, &signal_action, NULL);
    
#ifdef DEBUG
	NP::log("Starting server using port: " + to_string(portnum) + " [HW2]");
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
	listen(sockfd, 30);
	
	/**
	 *  Accept
	 */
	while (1) {
#ifdef DEBUG
        NP::log("Waiting for connections...");
#endif
		client_addr_len = sizeof(client_addr);
		newsockfd = ::accept(sockfd, (struct sockaddr *) &client_addr, &client_addr_len);
        
        // get client addr and port
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_addr_str, INET_ADDRSTRLEN);
        client_port = client_addr.sin_port;

        NP::clientAddr = string(client_addr_str);
        NP::clientPort = client_port;
        
#ifdef DEBUG
        NP::log("Connection from " + NP::clientAddr + ":" + to_string(NP::clientPort) + " accepted.");
#endif
		if (newsockfd < 0) {
			NP::err("accept error");
		}

        if ((childpid = fork()) < 0) {
        
            NP::err("fork error");

        } else if (childpid == 0) { // child process
            
            close(sockfd);

            NP::processRequest(newsockfd);
            
#ifdef DEBUG
            NP::log("processRequest return, leaving...");
#endif
            
            exit(EXIT_SUCCESS);
            
        } else {

            close(newsockfd);
        }
	}
	
	return 0;
}

/**
 *  Process connection request
 *
 *  @param sockfd
 */
void NP::processRequest(int ssock) {
    
    NP::readWrapper(ssock);
    
    char socksreq[262];
    memcpy(socksreq, buffer, 262);
    
    if (socksreq[0] != 4) { // wrong version
        return;
    }
    
    int vn = socksreq[0];
    int cd = socksreq[1];
    unsigned short dst_port;
    char dst_ip[INET_ADDRSTRLEN];
    char user_id[MAX_USER_ID_LEN+1];
//    char domain_name[MAX_DOMAIN_NAME_LEN+1];  // no need to handle domain name
    
    // decapsulate the damn port the way it encapsulated
    dst_port = (unsigned short) ntohs(*(unsigned short*)&socksreq[2]);
    sprintf(dst_ip, "%u.%u.%u.%u", (unsigned char)socksreq[4], (unsigned char)socksreq[5], (unsigned char)socksreq[6], (unsigned char)socksreq[7]);
    char* user_id_pch = strtok(&socksreq[8], "\0");
    strncpy(user_id, (user_id_pch == NULL ? "(no name)" : user_id_pch), MAX_USER_ID_LEN);  // USER_ID
    
    sprintf(buffer, "SOCKS Request:\nVN: %d, CD: %d, DST_IP: %s, DST_PORT: %hu, USER_ID: %s\n", vn, cd, dst_ip, dst_port, user_id);
    log(buffer);

    sprintf(buffer, "Check firewall rules: (%s:%hu) -> (%s:%hu)\n", NP::clientAddr.c_str(), NP::clientPort, dst_ip, dst_port);
    log(buffer);


    // check firewall rules
    if (!NP::isAllowedToConnect((SOCKS_TYPE)cd, dst_ip)) {

        sprintf(buffer, "firewall didnt pass(%s:%hu), return\n", dst_ip, dst_port);
        log(buffer);
        
        NP::sendSockResponse(ssock, false, DONTCARE, socksreq);
        return;
    }
    
    switch (cd) {
        case CONNECT: // CONNECT
        {
            // connect to dest host
            sprintf(buffer, "[SOCKS_CONNECT] firewall granted, start connecting to %s:%hu...\n", dst_ip, dst_port);
            log(buffer);
            int rsock = -1;

            if ((rsock = NP::connectDestHost(dst_ip, dst_port)) == -1) {
                NP::sendSockResponse(ssock, false, CONNECT, socksreq);
                return;
            }
            
            // send client response
            NP::sendSockResponse(ssock, true, CONNECT, socksreq);

            // Redirect data
            sprintf(buffer, "[SOCKS_CONNECT] connected to %s:%hu, redirecting data...\n", dst_ip, dst_port);
            log(buffer);
            
            NP::redirectData(ssock, rsock);
            
            sprintf(buffer, "[SOCKS_CONNECT] done. Connection to %s:%hu closed.\n", dst_ip, dst_port);
            log(buffer);

            close(rsock);
            break;
        }
            
        case BIND: // BIND
        {
            sprintf(buffer, "[SOCKS_BIND] firewall granted, start connecting to %s:%hu...\n", dst_ip, dst_port);

            // get socket
            int bindsock = 0;
            if ((bindsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                NP::err("[SOCKS_BIND] Cannot open socket!");
            }
            
            // bind
            struct sockaddr_in ftpsv_addr, ftpcli_addr;
            socklen_t ftpcli_len, ftpsv_len;
            bzero((char*)&ftpsv_addr, sizeof(ftpsv_addr));
            ftpsv_addr.sin_family = AF_INET;
            ftpsv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            ftpsv_addr.sin_port = 0;
            
            if (::bind(bindsock, (struct sockaddr*) &ftpsv_addr, sizeof(ftpsv_addr)) < 0) {
                NP::err("[SOCKS_BIND] Bind error");
            }
            ftpsv_len = sizeof(ftpsv_addr);
            ::getsockname(bindsock, (struct sockaddr*) &ftpsv_addr, &ftpsv_len);
            
            NP::log("[SOCKS_BIND] socket " + to_string(bindsock) + " binded to port " + to_string(ntohs(ftpsv_addr.sin_port)));
            
            /**
             *  Listen
             */
            if (::listen(bindsock, 5) < 0) {
                NP::err("[SOCKS_BIND] listen error");
            }
            NP::log("[SOCKS_BIND] linstening to port " + to_string(ntohs(ftpsv_addr.sin_port)));

            
            // send client response
            *((unsigned short*)&socksreq[2]) = (ftpsv_addr.sin_port);
            socksreq[4] = 0x0;
            socksreq[5] = 0x0;
            socksreq[6] = 0x0;
            socksreq[7] = 0x0;
            NP::sendSockResponse(ssock, true, BIND, socksreq);

            NP::log("[SOCKS_BIND]sent socks response, waiting for connections...");

            
            ftpcli_len = sizeof(ftpcli_addr);
            int newbindsock = ::accept(bindsock, (struct sockaddr *) &ftpcli_addr, &ftpcli_len);
            if (newbindsock == -1) {
                NP::err("[SOCKS_BIND] accept error");
            }
            
            NP::log("[SOCKS_BIND] accepted a connection");
            
            // send client response
            char ftpcli_addr_str[INET_ADDRSTRLEN];
            unsigned short ip[4];
            inet_ntop(AF_INET, &(ftpcli_addr.sin_addr), ftpcli_addr_str, INET_ADDRSTRLEN);
            sscanf(ftpcli_addr_str, "%hu.%hu.%hu.%hu", &ip[0], &ip[1], &ip[2], &ip[3]);
            
            *((unsigned short*)&socksreq[2]) = (ftpcli_addr.sin_port);
            socksreq[4] = (unsigned char)ip[0];
            socksreq[5] = (unsigned char)ip[1];
            socksreq[6] = (unsigned char)ip[2];
            socksreq[7] = (unsigned char)ip[3];

            NP::sendSockResponse(ssock, true, BIND, socksreq);
            
            // Redirect data
            sprintf(buffer, "[SOCKS_BIND] connected to %s:%hu, redirecting data...\n", dst_ip, dst_port);
            log(buffer);
            
            NP::redirectData(ssock, newbindsock);
            
            sprintf(buffer, "[SOCKS_BIND] done. Connection to %s:%hu closed.\n", dst_ip, dst_port);
            log(buffer);
            
            close(newbindsock);
            
            break;
        }
    }
    
    
}

bool NP::isAllowedToConnect(SOCKS_TYPE type, char* ip) {
    
    ifstream rulefile("socks.conf", ifstream::in);
    string ruletype, ruleip;
    size_t i, ruleipsize;
    
    if (!rulefile) {
        printf("socks.conf open filed\n");
        return true;
    }
    
    while (!rulefile.eof()) {
        
        rulefile >> ruletype >> ruleip;
        printf("type = %s, ip = %s, checking %s\n", ruletype.c_str(), ruleip.c_str(), ip);
        ruleipsize = ruleip.size();
        
        if (ruletype == "c" && type == CONNECT) {
            
            for (i = 0; i < ruleipsize; i++) {
                if (ruleip[i] == '*') {
                    
                    return true;
                    
                } else if (ruleip[i] != ip[i]) {
                    
                    break;
                }
            }
            
            if (i == ruleipsize) {
                return true;
            }
            
        } else if (ruletype == "b" && type == BIND) {
        
            for (i = 0; i < ruleipsize; i++) {
                if (ruleip[i] == '*') {
                    
                    return true;
                    
                } else if (ruleip[i] != ip[i]) {
                    
                    break;
                }
            }
            
            if (i == ruleipsize) {
                return true;
            }
        }
        
    }
    
    return false;
}

int NP::connectDestHost(char dst_ip[INET_ADDRSTRLEN], unsigned short dst_port) {
    
    /**
     *	Get server info
     */
    int status;
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_TCP;
    
    char port[6];
    sprintf(port, "%hu", dst_port);
    if ((status = getaddrinfo(dst_ip, port, &hints, &res)) != 0) {
        log("getaddrinfo: " + string(gai_strerror(status)));
        return -1;
    }
    
    /**
     *	Get sockfd
     */
    int rsock = socket(AF_INET, SOCK_STREAM, 0);
    if (rsock == -1) {
        log("Socket error: " + string(strerror(errno)));
        return -1;
    }
    
    /**
     *	Connect
     */
    if (::connect(rsock, res->ai_addr, res->ai_addrlen) == -1) {
        log("Connect error: " + string(strerror(errno)));
        return -1;
    }
    
    freeaddrinfo(res);
    return rsock;
}

void NP::print(int ssock, string msg, int flag) {
    NP::writeWrapper(ssock, msg.c_str(), msg.length());
}

void NP::sendSockResponse(int ssock, bool isGranted, SOCKS_TYPE type, char* socksreq) {

    char socksresp[8];
    socksresp[0] = 0x00;
    socksresp[1] = isGranted ? 0x5A : 0x5B;
    switch (type) {
        case CONNECT:
        case BIND:
            for (int i = 2; i <= 7; i++) {
                socksresp[i] = socksreq[i];
            }
            break;
            
        default:
            break;
    }
//    for (int i = 0; i < 8; i++) {
//        printf("sockrep[%d] = %u\n", i, (unsigned char)sockrep[i]);
//    }
    NP::writeWrapper(ssock, socksresp, 8);
}

void NP::redirectData(int ssock, int rsock) {
    
    // use `select` to get data
    // more importantly, use two `select`s to separate `rfds` and `wfds`
    // or would result in infinite loop (select always returns 2, i.e. always can write)
    
    int nfds = FD_SETSIZE;
    fd_set rfds;    // readable fds used in select
    fd_set wfds;    // writable fds used in select
    fd_set rs;      // active read fds
    fd_set ws;      // actvie write fds
    
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&rs);
    FD_ZERO(&ws);
    
    FD_SET(ssock, &rs);
    FD_SET(ssock, &ws);
    FD_SET(rsock, &rs);
    FD_SET(rsock, &ws);
    
    char rsockbuffer[BUFFER_SIZE];
    char ssockbuffer[BUFFER_SIZE];
    size_t rsockbuffersize = 0;
    size_t ssockbuffersize = 0;
    bzero(rsockbuffer, BUFFER_SIZE);
    bzero(ssockbuffer, BUFFER_SIZE);

    bool rsockbufempty = true;
    bool ssockbufempty = true;
    
    struct timeval timeout;
    timeout.tv_sec = 1200;
    int toCheck = 0;
    
    while (1) {
        
        memcpy(&rfds, &rs, sizeof(rfds));
        memcpy(&wfds, &ws, sizeof(wfds));

        toCheck = select(nfds, &rfds, NULL, NULL, &timeout);
        if (toCheck == 0) {
            break;
        }

        if (ssockbufempty && FD_ISSET(ssock, &rfds)) {

            ssockbuffersize = NP::readWrapper(ssock);
            char* readStr = buffer;
            // manually strcpy
            for (int i = 0; i < ssockbuffersize; i++) {
                ssockbuffer[i] = readStr[i];
            }
            ssockbufempty = false;
//            strncpy(ssockbuffer, readStr, BUFFER_SIZE);
        }
        
        
        if (rsockbufempty && FD_ISSET(rsock, &rfds)) {

            rsockbuffersize = NP::readWrapper(rsock);
            char* readStr = buffer;
            // manually strcpy
            for (int i = 0; i < rsockbuffersize; i++) {
                rsockbuffer[i] = readStr[i];
            }
            rsockbufempty = false;
//            strncpy(rsockbuffer, readStr, BUFFER_SIZE);
        }
        
        toCheck = select(nfds, NULL, &wfds, NULL, &timeout);
        if (toCheck == 0) {
            break;
        }
        
        if (!ssockbufempty && FD_ISSET(rsock, &wfds)) {
            NP::writeWrapper(rsock, ssockbuffer, ssockbuffersize);
            bzero(ssockbuffer, BUFFER_SIZE);
            ssockbufempty = true;
        }

        
        if (!rsockbufempty && FD_ISSET(ssock, &wfds)) {
            NP::writeWrapper(ssock, rsockbuffer, rsockbuffersize);
            bzero(rsockbuffer, BUFFER_SIZE);
            rsockbufempty = true;
        }
    }


}