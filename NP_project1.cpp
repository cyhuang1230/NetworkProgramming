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
		// @TODO: Double fork / SIGCHLD to prevent zombie process.
		if ((childpid = fork()) < 0) {
			
		}
	}
	
	return 0;
}
