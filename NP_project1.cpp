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
using namespace std;

namespace NP {
	void log(string str, int error = 0) {
		
		if (error) {
			cout << "ERROR: ";
		} else {
			cout << "LOG: ";
		}
		
		cout << str << endl;
	}
	
	void err(string str) {

		log(str, 1);
		perror(str.c_str());
		exit(1);
	}
    
    void processRequest(int);
    
    void writeWrapper(int sockfd, const char buffer[], size_t size) {

        int n = write(sockfd, buffer, size);
#ifdef DEBUG
        NP::log("write(size = " + to_string(size) + ", n = " + to_string(n) + "): \n" + string(buffer));
#endif
        
        if (n < 0) {
            NP::err("write error: " + string(buffer));
        }
    }
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
    struct sigaction sigchld_action = {
        .sa_handler = SIG_DFL,
        .sa_flags = SA_NOCLDWAIT
    };
    sigaction(SIGCHLD, &sigchld_action, NULL);
    
#ifdef DEBUG
	NP::log("Starting server using port: " + to_string(portnum));
#endif
	
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		NP::err("Cannot open socket!");
	}
	
	/************
	*	Bind	*
	************/
	bzero((char*)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(portnum);
	
	if (::bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
		NP::err("Bind error");
	}
	
	/************
	 *	Listen	*
	 ************/
	listen(sockfd, 5);
	
	
	/************
	 *	Accept	*
	 ************/
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

void NP::processRequest(int sockfd) {
    
    int n;
    string str;
    
    // Welcome msg
    const char buffer[] = "\
**************************************** \n\
** Welcome to the information server. ** \n\
****************************************";
    NP::writeWrapper(sockfd, buffer, sizeof(buffer));
    
}
