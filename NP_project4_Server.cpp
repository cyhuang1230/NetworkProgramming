//  NCTU CS. Network Programming Assignment 2

//  Code by Denny Chien-Yu Huang, 01/02/16.
//  Github: https://cyhuang1230.github.io/

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
#include <algorithm>
#include <vector>
#include <list>
#include <utility>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

#define MAX_SIZE 15001
#define DEFAULT_PORT 4411
#define MAX_USER 100
#define MAX_USER_ID_LEN 50
#define MAX_DOMAIN_NAME_LEN 1024
//#define DEBUG 1

char buffer[MAX_SIZE];

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
    
    void processRequest(int);
	
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
	
    void writeWrapper(int sockfd, const char buffer[], size_t size) {

        resetBuffer();
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
    
    void close(const int fd, const int i, const int j, string errmsg = "", string sucmsg = "", bool needOutput = true) {
        
        if (::close(fd) == -1) {

            if (errmsg.empty()) {
            
                NP::err("close pipes[" + to_string(i) + "][" + to_string(j) + "] error " + to_string(errno));
                
            } else {
                
                NP::err(errmsg);
            }
        }
        
#ifdef DEBUG
        if (needOutput) {
            sucmsg.empty() ? NP::log("close pipes[" + to_string(i) + "][" + to_string(j) + "]") : NP::log(sucmsg);
        }
#endif
    }
    
    class Client;
    class ClientHandler;
    
    Client* iAm = NULL; // for client to store his own info

    
    /// Required info of each client
    class Client {
    public:
        int id = 0;
        int pid;
        int sockfd;
        char ip[INET_ADDRSTRLEN];
        int port;
        char*  msg;
        // if a fd opened for pipe, store that fd here
        // so that as soon as the pipe is cleared, we close fd
        
        Client() {}
        
        void set(int iId, int iPid, int iSockfd, char cIp[INET_ADDRSTRLEN], int iPort) {
#ifdef DEBUG
            NP::log("set client.. (" + to_string(id) + ", " + to_string(iPid) + ", " + to_string(iSockfd) + ", " + string(cIp) + "/" + to_string(iPort) + ")");
#endif
            id = iId;
            pid = iPid;
            sockfd = iSockfd;
            strncpy(ip, cIp, INET_ADDRSTRLEN);
            port = iPort;
        }
        
        string getIpRepresentation() {
            // as TA required
//            return "CGILAB/511";
            return string(ip) + "/" + to_string(port);
        }
        
        void markAsInvalid() {
            id = 0;
        }
        
        string print() {
            return "(" + to_string(id) + ", " + to_string(pid) + ", " + to_string(sockfd) + ", " + string(ip) + ":" + to_string(port) + ")";
        }
    };
    
    /// Wrapper class for clients
    class ClientHandler {
        
        // initialize each element with empty client
        Client clients[MAX_USER+1] = {};
        
        // Check if the user id is valid
        bool isUserIdValid(int clientId);
        
    public:

        /**
         *	Insert new client to `clients` array
         *
         *	@return pointer to new client; NULL if error
         */
        Client* insertClient(int pid, int sockfd, char ip[INET_ADDRSTRLEN], int port);
        
        /**
         *	Remove client from `clients`
         *
         *	@param id	client id
         */
        void removeClient(int id);
    };
    
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
    
    // Initialize client handler
    NP::ClientHandler clientHandler = NP::ClientHandler();
    
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

#ifdef DEBUG
        NP::log("Connection from " + string(client_addr_str) + ":" + to_string(client_port) + " accepted.");
#endif
		if (newsockfd < 0) {
			NP::err("accept error");
		}

        if ((childpid = fork()) < 0) {
        
            NP::err("fork error");

        } else if (childpid == 0) { // child process
            
            close(sockfd);

            // insert new client
            NP::iAm = clientHandler.insertClient(getpid(), newsockfd, client_addr_str, client_port);
            if (NP::iAm == NULL) {
                NP::err("insert client error");
            }
            NP::processRequest(newsockfd);
            
            
#ifdef DEBUG
            NP::log("processRequest return, leaving...");
#endif
            
            clientHandler.removeClient(NP::iAm->id);
            
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
void NP::processRequest(int sockfd) {
    
    char* sockreq = NP::readWrapper(sockfd);
    for (int i = 0; i < 8; i++) {
        printf("sockreq[%d] = %u\n", i, (unsigned char)sockreq[i]);
    }
    int cd = sockreq[1];
    unsigned short dst_port;
    char dst_ip[INET_ADDRSTRLEN];
//    char user_id[MAX_USER_ID_LEN+1];
    char domain_name[MAX_DOMAIN_NAME_LEN+1];
    
    // decapsulate the damn port the way it encapsulated
    dst_port = (unsigned short) ntohs(*(unsigned short*)&sockreq[2]);
    sprintf(dst_ip, "%u.%u.%u.%u\n", (unsigned char)sockreq[4], (unsigned char)sockreq[5], (unsigned char)sockreq[6], (unsigned char)sockreq[7]);
    
    //    strncpy(user_id, strtok(&sockreq[8], "\0"), MAX_USER_ID_LEN);
    printf("get:\nVN: %d, CD: %d, DST_PORT: %hu, DST_IP: %s\n", sockreq[0], cd, dst_port, dst_ip);

    if (strcmp(dst_ip, "0.0.0.")) {    // DST IP = 0.0.0.x
//        int i = 8;
//        while (sockreq[i] != '\0') {
//            i++;
//        }
        strncpy(domain_name, strtok(&sockreq[8+1], "\0"), MAX_DOMAIN_NAME_LEN);
        printf("Domain name = %s\n", domain_name);
    }
    fflush(stdout);
}


/// NP::ClientHandler
bool NP::ClientHandler::isUserIdValid(int clientId) {

    return (clients[clientId].id > 0);
}

NP::Client* NP::ClientHandler::insertClient(int pid, int sockfd, char ip[INET_ADDRSTRLEN], int port) {
    
    // get an id for new client
    int newId = -1;
    for (int i = 1; i <= MAX_USER; i++) {
        if (!isUserIdValid(i)) {
            newId = i;
            break;
        }
    }
    
    if (newId == -1) {
        return NULL;
    }
    
    clients[newId].set(newId, pid, sockfd, ip, port);
    
#ifdef DEBUG
    NP::log("inserted client with id = " + to_string(clients[newId].id));
#endif
    
    return &clients[newId];
}

void NP::ClientHandler::removeClient(int id) {
#ifdef DEBUG
    NP::log("removing client " + to_string(id));
#endif
    
    if (!isUserIdValid(id)) {
        NP::err("Try to remove invalid id " + to_string(id));
    }
    
    clients[id].markAsInvalid();
}
