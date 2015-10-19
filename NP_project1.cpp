//  NCTU CS. Network Programming Assignment 1
//  by Denny Chien-Yu Huang.

#include <string>
#include <iostream>
#include <sstream>
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
	void log(string str, bool error = false, bool newline = true, bool prefix = true) {
		
		if (error && prefix) {
			cout << "ERROR: ";
		} else if (prefix) {
			cout << "LOG: ";
		}
		
		cout << str;
        if (newline) {
            cout << endl;
        }
        
		cout.flush();
	}
	
	void err(string str) {

		log(str, true);
		perror(str.c_str());
		exit(1);
	}
    
    bool processRequest(int);
	
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
		// stdout/err to next N row; -1 indicates no output; 0 means next cmd;
        int stdoutToRow = -1;
        int stderrToRow = -1;
        
        Command(const char* name, const char* arg, int stdoutRow, int stderrRow): name(name), arg(arg), stdoutToRow(stdoutRow), stderrToRow(stderrRow){}
        
        Command(const char* name, const char* arg, int stdoutRow) {
            Command(name, arg, stdoutRow, -1);
        }
        
        Command(const char* name, const char* arg) {
            Command(name, arg, -1, -1);
        }
        
        static bool isCommandValid(const char* cmd) {
            
            // dont block setenv
            if (strcmp(cmd, "setenv") == 0) {
#ifdef DEBUG
                NP::log("isCommandValid: setenv -> pass");
#endif
                return true;
            }
            
            const char* kPath = getenv("PATH");
            
            // to get SAME path, we need to make a copy of this.
            char* path = new char[strlen(kPath)+1];
            strcpy(path, kPath);
#ifdef DEBUG
            NP::log("isCommandValid: " + string(cmd) + " [PATH: " + string(path) + "]");
#endif

            char* token = strtok(path, ":");
            while (token != NULL) {

                char* file = new char[strlen(token) + 1 + strlen(cmd) +1];
                strcpy(file, token);
                strcat(file, "/");
                strcat(file, cmd);
#ifdef DEBUG
                NP::log("trying " + string(file), 0, 0);
#endif
                if (!access(file, X_OK)) {
#ifdef DEBUG
                    NP::log("... true.", 0, 1, 0);
#endif
                    return true;
                }
#ifdef DEBUG
                NP::log("... false.", 0, 1, 0);
#endif
                token = strtok(NULL, ":");
            }
#ifdef DEBUG
            NP::log("No matching file. -> Invalid command.");
#endif
            return false;
        }
	};
	
	/// The whole command line
	class CommandLine {
	public:
		vector<vector<Command>> cmds;	// not suppose to change the order (i.e. append only) to prevent error
        int inputLinesLeft = 1; // the number of lines to input
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
            
            // Welcome msg
            char welcomeMsg[] = "\
**************************************** \n\
** Welcome to the information server. ** \n\
****************************************\n";
            NP::writeWrapper(newsockfd, welcomeMsg, sizeof(welcomeMsg));
            
            while (!NP::processRequest(newsockfd)) {
                
            }
            
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
 *
 *  @return boolean value that indicates if we should exit
 */
bool NP::processRequest(int sockfd) {
    
    // create a cmd line for this input
    NP::CommandLine cl = CommandLine();
    string str;
    bool needExit = false;
    
	// read cmd
	while (cl.inputLinesLeft--) {
        
        str = NP::readWrapper(sockfd);

//        if ((str = NP::readWrapper(sockfd)).find("exit") == string::npos) {
//#ifdef DEBUG
//            NP::log("exit detected.");
//#endif
//            needExit = true;
//            break;
//        }
        
        if (str.empty()) {  // empty string
            continue;
        }
        
        /**
         *  Parse Input
         */
#ifdef DEBUG
        NP::log("Parsing input... ");
#endif
        // declare a array to store current cmds
        vector<Command> curCl;
        
        string buffer;
        stringstream ss = stringstream(str);
        while (ss >> buffer) {
            
            const char* buffer_cstr = buffer.c_str();
            
#ifdef DEBUG
            NP::log(buffer);
#endif
            if (!NP::Command::isCommandValid(buffer_cstr)) {    // invalid cmd
                
#ifdef DEBUG
                NP::log(" -> invalid cmd");
#endif
                string prefix = "Unknown command: [";
                string suffix = "].\n";
                string msg = prefix + buffer + suffix;
                NP::writeWrapper(sockfd, msg.c_str(), msg.length());
                
                break;
                
            } else {    // valid cmd
                // @TODO
//                curCl.push_back();
        
            }
        }
        
#ifdef DEBUG
        NP::log("End of parsing.");
#endif
        
        if (curCl.empty()) {
            
#ifdef DEBUG
            NP::log("curCl empty -> continue.");
#endif
            
            // if curCl is empty -> the first cmd is invalid
            // -> dont process this row
            continue;
            
        } else {
            
#ifdef DEBUG
            NP::log("curCl nonempty -> push back.");
#endif
            // otherwise, push back this row
            cl.cmds.push_back(curCl);
        }
	}
	
//    if (needExit) {
//        return;
//    }
    
    // @TODO: execute cmd
    // @TODO: need to determine `exit` cmd
    
    return false;
}
