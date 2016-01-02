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
#define MAX_USER 30
#define MAX_PUBLIC_PIPE 101 // 1~100
#define PIPE_SIZE (1025*sizeof(char))
#define PIPE_SIZE_TOTAL ((PIPE_SIZE)*(MAX_PUBLIC_PIPE))
#define USER_MSG_BUFFER (1025*sizeof(char))
#define USER_MSG_BUFFER_TOTAL (USER_MSG_BUFFER*(MAX_USER+1))
#define USER_NAME_SIZE 20
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
	
	string readWrapper(int sockfd, bool needPrompt = true) {
		
		// display prompt
        if (needPrompt) {
            writeWrapper(sockfd, "% ", 2);
        }

        resetBuffer();
		int n = read(sockfd, buffer, sizeof(buffer));
		if (n < 0) {
		
            NP::err("read error");
        
        } else if (n == 0) {
            
            return string();
        }
        
#ifdef DEBUG
		NP::log("read(size = " + to_string(sizeof(buffer)) + ", n = " + to_string(n) + "): \n" + string(buffer));
#endif

		return string(buffer);
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
    
    /// for shared memory management
    int client_counter = 0;
    int shmIdClientData;
    ClientHandler* ptrShmClientData;
    Client* iAm = NULL; // for client to store his own info
    
    void shmdt(const void* shm) {
        if (::shmdt(shm) == -1) {
            NP::err("shmdt error with errno " + to_string(errno));
        }
    }
    
    void markShmToBeDestroyed(int shmId) {
        if (shmctl(shmId, IPC_RMID, NULL) == -1) {
            NP::err("shmctl error with errno " + to_string(errno));
        }
    }
    
    // handler for SIGCHLD
    void signal_handler(int signum);
    
    /// Required info of each client
    class Client {
    public:
        int id = 0;
        char userName[USER_NAME_SIZE+1] = "(no name)";
        int pid;
        int sockfd;
        char ip[INET_ADDRSTRLEN];
        int port;
        char*  msg;
        // if a fd opened for pipe, store that fd here
        // so that as soon as the pipe is cleared, we close fd
        int fdOpenForPipe[MAX_PUBLIC_PIPE] = {};
        
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
            
            // reset name
            memset(userName, 0, sizeof(userName));
            strcpy(userName, "(no name)");
        }
        
        string getIpRepresentation() {
            // as TA required
            return "CGILAB/511";
//            return string(ip) + "/" + to_string(port);
        }
        
        void markAsInvalid() {
            id = 0;
        }
        
        string print() {
            return "(" + to_string(id) + ", " + to_string(pid) + ", " + to_string(sockfd) + ", " + string(ip) + ":" + to_string(port) + ")";
        }
        
        string getName() {
            return string(userName);
        }
    };
    
    /// Wrapper class for clients
    class ClientHandler {
        
        // initialize each element with empty client
        Client clients[MAX_USER+1] = {};
        
        bool pipeInUseFlag[MAX_PUBLIC_PIPE] = {};
        
        // Check if the user id is valid
        bool isUserIdValid(int clientId);
        
        // Signal specific child
        void signalClient(int clientId, int signal);
        
        // Signal every child
        void signalEveryClient(int signal);
        
    public:
        char curCmd[10000]; // to store command that needs to broadcast
        int curPublicPipe = 0;
        bool isJustReadPublicPipe = 0;  // true: just read; false: just wrote
        friend void debug(int);
        
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
    signal_action.sa_handler = NP::signal_handler;
	signal_action.sa_flags = SA_NOCLDWAIT | SA_RESTART;
    sigaction(SIGCHLD, &signal_action, NULL);
    sigaction(SIGUSR1, &signal_action, NULL);
    sigaction(SIGUSR2, &signal_action, NULL);
    
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
        
        // check how many clients are connected,
        // if it's the first one, create shm.
        if (NP::client_counter == 0) {
            
            // Create shared memory
            // 1. for client data
            NP::shmIdClientData = shmget(IPC_PRIVATE, sizeof(clientHandler), IPC_CREAT | IPC_EXCL | 0666);
            if (NP::shmIdClientData < 0) {
                NP::err("shmIdClientData <0 with errno " + to_string(errno));
            }
            // prepare data
            NP::ptrShmClientData = (NP::ClientHandler*) shmat(NP::shmIdClientData, NULL, 0);
            memcpy(NP::ptrShmClientData, &clientHandler, sizeof(clientHandler));
        }
        
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

            // reset sigaction
            struct sigaction sigchld_action;
            sigchld_action.sa_handler = SIG_DFL;
            sigchld_action.sa_flags = SA_NOCLDWAIT;
            sigaction(SIGCHLD, &sigchld_action, NULL);
            
            // insert new client
            NP::iAm = NP::ptrShmClientData->insertClient(getpid(), newsockfd, client_addr_str, client_port);
            if (NP::iAm == NULL) {
                NP::err("insert client error");
            }
            NP::processRequest(newsockfd);
            
            
#ifdef DEBUG
            NP::log("processRequest return, leaving...");
#endif
            
            NP::ptrShmClientData->removeClient(NP::iAm->id);
            
            exit(EXIT_SUCCESS);
            
        } else {

            NP::client_counter++;
            
#ifdef DEBUG
            NP::log("[parent] client_counter incremented to " + to_string(NP::client_counter));
#endif
            
            close(newsockfd);
        }
	}
	
	return 0;
}

/**
 *  Process connection request
 *
 *  @param sockfd
 *
 *  @return boolean value that indicates if we should exit
 */
void NP::processRequest(int sockfd) {
    
    
}


/* For HW2 */

void NP::signal_handler(int signum) {
    
    switch (signum) {
        case SIGCHLD:
            client_counter--;
#ifdef DEBUG
            NP::log("SIGCHLD received, counter changed to " + to_string(client_counter));
#endif
            // if there's no client left, detach shm and mark it as removable
            if (!client_counter) {
#ifdef DEBUG
                NP::log("No more client... detach shm");
#endif
                shmdt(ptrShmClientData);
                markShmToBeDestroyed(shmIdClientData);
            }
    }
}

/// NP::ClientHandler
bool NP::ClientHandler::isUserIdValid(int clientId) {

    return (clients[clientId].id > 0);
}

void NP::ClientHandler::signalClient(int clientId, int signal) {
#ifdef DEBUG
    NP::log("signal-ing client " + to_string(clientId) + " with pid " + to_string(clients[clientId].pid));
#endif
    
    if (!isUserIdValid(clientId)) {
        NP::err("Try to send signal to invalid client with id " + to_string(clientId));
    }
    
    if(kill(clients[clientId].pid, SIGUSR1) == -1) {
        NP::err("Failed to send to client " + to_string(clientId) + ", errno = " + to_string(errno));
    }
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
    memset(clients[newId].fdOpenForPipe, 0, sizeof(clients[newId].fdOpenForPipe));
    
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
