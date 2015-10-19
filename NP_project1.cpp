//
//  main.cpp
//  NP_Project1_CPP
//
//  Created by Denny H. on 10/15/15.
//  Copyright © 2015 Denny H. All rights reserved.
//

#include <string>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <vector>
using namespace std;

#define DEBUG

#define MAX_SIZE 15001
char buffer[MAX_SIZE];

namespace NP {
	void log(string str, bool error = 0) {
		
		if (error) {
			cout << "ERROR: ";
		} else {
			cout << "LOG: ";
		}
		
		cout << str << endl;
		cout.flush();
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

        int n = write(sockfd, buffer, size);
#ifdef DEBUG
        NP::log("write(size = " + to_string(size) + ", n = " + to_string(n) + "): \n" + string(buffer));
#endif
        
        if (n < 0) {
            NP::err("write error: " + string(buffer));
        }
    }
	
	string readWrapper(int sockfd) {
		
		// display prompt
		writeWrapper(sockfd, "% ", 2);
		
		resetBuffer();
		int n = read(sockfd, buffer, sizeof(buffer));
		if (n < 0) {
			NP::err("read error");
		}
#ifdef DEBUG
		NP::log("read(size = " + to_string(sizeof(buffer)) + ", n = " + to_string(n) + "): \n" + string(buffer));
#endif

		return string(buffer);
	}
	
	/// Individual command
	class Command {
	public:
		const char* name;	// command name
		const char* arg;	// arguments
		int stdoutOutput = 0;	// stdout to next `stdoutOutput` cmd
		int stderrOutput = 0;	// stderr to next `stderrOutput` cmd
	};
	
	/// The whole command line
	class CommandLine {
	public:
		vector<Command> cmds;	// not suppose to change the order (i.e. append only) to prevent error
		int numberSuppose = 0;	// the number of cmds that suppose to have
		int numberGot = 0;		// the number of cmds that got so far
		
		
	};
}

int main(int argc, const char * argv[]) {
	
	int sockfd, newsockfd, childpid, portnum;
	struct sockaddr_in cli_addr, serv_addr;
	socklen_t clilen;

	if (argc < 2) {	// if no port provided
		
		portnum = 4410;
		
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
		
		clilen = sizeof(cli_addr);
		newsockfd = ::accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if (newsockfd < 0) {
			NP::err("accept error");
		}

        if ((childpid = fork()) < 0) {
        
            NP::err("fork error");

        } else if (childpid == 0) { // child process
            
            close(sockfd);
            
            NP::processRequest(newsockfd);
            
            exit(0);
            
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
    
    string str;
    
    // Welcome msg
	char welcomeMsg[] = "\
**************************************** \n\
** Welcome to the information server. ** \n\
****************************************\n";
    NP::writeWrapper(sockfd, welcomeMsg, sizeof(welcomeMsg));

	// read cmd
	while ((str = NP::readWrapper(sockfd)).find("exit") == string::npos) {
		
		NP::CommandLine cmdLine = CommandLine();
		
	}
	
#ifdef DEBUG
	NP::log("exit detected.");
#endif
}
