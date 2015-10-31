//  NCTU CS. Network Programming Assignment 1
//  : A pipe-enabled shell. Please refer to hw1Spec.txt for more details.

//  Code by Denny Chien-Yu Huang, 10/25/15.
//  Github: http://cyhuang1230.github.io/

//    A Command Line (a.k.a. cl)
//     ---------------------
//    |    --               | <- a line
//    |   |ls| <- a command |
//    |    --               |
//    |                     |
//    |                     |
//    |                     |
//     ---------------------

// Program structure:
//  Socket process
//   |
//    - Daemon
//       |
//        - Child process to fork and exec cmds

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
using namespace std;

#define MAX_SIZE 15001
#define DEFAULT_PORT 4411

//#define DEBUG 1

char buffer[MAX_SIZE];

namespace NP {
    
    class Command;
    class CommandLine;
    
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
    
    void deamonPreparation(vector<vector<Command>>& cl);
    
    void processCommand(const int n, vector<Command>&, list<pair<pair<int, int>, string>>&, list<pair<pair<int, int>, string>>&);
    
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
        
        if (needOutput) {
            sucmsg.empty() ? NP::log("close pipes[" + to_string(i) + "][" + to_string(j) + "]") : NP::log(sucmsg);
        }
    }
    
    list<pair<pair<int, int>, string>>::iterator findTemp(list<pair<pair<int, int>, string>>& l, const pair<int, int>& target) {
        
        list<pair<pair<int, int>, string>>::iterator it = l.begin();
        while (it != l.end()) {
            if (it->first == target) {
                return it;
            }
            ++it;
        }
        
        return it;
    }
    
	/// Individual command
	class Command {
	public:

        vector<string> arg;
        string toFile;
        
		// N: stdout/err to next N row; -1: sockfd; 0: next cmd, -2: to file (stdout only);
        int stdoutToRow = -1;
        int stderrToRow = -1;
        int sockfd;
        
        Command(const char* name, int sockfd): sockfd(sockfd) {
            appendArg(name);
        }
        
        void appendArg(const char* ch) {
            arg.push_back(ch);
        }
        
        void appendArg(string str) {
            arg.push_back(str.c_str());
        }
        
        string to_string() {
            
            string str = "";
            for (int i = 0; i < arg.size(); i++) {
                str.append(arg[i] + " ");
            }
            return str;
        }
        
        static char* whereis(const char* cmd) {
            
            const char* kPath = getenv("PATH");

            // to get SAME path, we need to make a copy of this.
            int kPathLen = strlen(kPath);
            char* path = new char[kPathLen+1];
            strcpy(path, kPath);
            
            char* token = strtok(path, ":");
            while (token != NULL) {
                
                int len = strlen(token) + 1 + strlen(cmd);
                char* file = new char[len+1];
                strcpy(file, token);
                strcat(file, "/");
                strcat(file, cmd);
                *remove(file, file+len, '\r') = '\0';
     
                if (access(file, X_OK) == 0) {

                    delete[] path;
                    
                    return file;
                }

                delete[] file;
                token = strtok(NULL, ":");
            }
            
            delete[] path;
            NP::log("whereis: " + string(cmd) + " -> not found");
            
            return NULL;
        }
        
        static bool isCommandValid(const char* cmd) {

            // dont block setenv
            if (strncmp(cmd, "setenv", 6) == 0) {
#ifdef DEBUG
                NP::log("isCommandValid: setenv -> pass");
#endif
                return true;
                
            } else if (strncmp(cmd, "printenv", 6) == 0) {
#ifdef DEBUG
                NP::log("isCommandValid: printenv -> pass");
#endif
                return true;
            }
            
            return Command::whereis(cmd) == NULL ? false : true;
        }
        
        char* const* toArgArray() {
            
            int len = arg.size();
            char** arr = new char*[len+1];
            
            for (int i = 0; i < len; i++) {
                arr[i] = new char[arg[i].size()+1];
                strcpy(arr[i], arg[i].c_str());
            }
            arr[len] = (char *)0;
            
            return arr;
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
		
		portnum = DEFAULT_PORT;
		
	} else {
		
		// read port from input
		portnum = atoi(argv[1]);
	}
#ifndef LOCAL
    // set PATH to `bin:.` to satisfy requirement
    setenv("PATH", "bin:.", 1);
    
    // change directory to ras to satisfy requirement
    if (chdir("ras") == -1) {
        NP::err("chdir error");
    }
#endif
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
        NP::log("Connection accepted");
#endif
		if (newsockfd < 0) {
			NP::err("accept error");
		}

        if ((childpid = fork()) < 0) {
        
            NP::err("fork error");

        } else if (childpid == 0) { // child process
            
            close(sockfd);
            
            // Welcome msg
            char welcomeMsg[] =
            "****************************************\n"
            "** Welcome to the information server. **\n"
            "****************************************\n";
            NP::writeWrapper(newsockfd, welcomeMsg, sizeof(welcomeMsg));
            
            do {
                
#ifdef DEBUG
                NP::log("Waiting for new cmd input");
#endif
                
            } while (!NP::processRequest(newsockfd));
            
#ifdef DEBUG
            NP::log("processRequest return true, leaving...");
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
 *
 *  @return boolean value that indicates if we should exit
 */
bool NP::processRequest(int sockfd) {
    
    // create a cmd line for this input
    NP::CommandLine cl = CommandLine();
    string strLine;
    bool needExit = false;
    bool needExecute = true;
    list<pair<pair<int, int>, string>> listStdout;    // store stdout temporarily
    list<pair<pair<int, int>, string>> listStderr;    // store stderr temporarily
    int counter = 0;
    
	// read cmd
	while (cl.inputLinesLeft--) {
#ifdef DEBUG
        NP::log("cl.inputLinesLeft = " + to_string(cl.inputLinesLeft));
#endif
        strLine = NP::readWrapper(sockfd);
        
        if (strLine.empty()) {  // empty string
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
        bool shouldCurStrBeCmd = true;  // indicates if the current string is a cmd (else is an arg)
        
        // setenv
        if (strLine.find("setenv") != string::npos && strLine.find("PATH", 7) != string::npos) {
#ifdef LOCAL
            string dir = "/Users/ChienyuHuang/ras";
            string path = dir + string("/") + strLine.substr(12);
#else
//            string path = get_current_dir_name() + string("/") + strLine.substr(12);
            string path = strLine.substr(12);
#endif
            *remove(path.begin(), path.end(), '\n') = '\0'; // trim '\n'
            
#ifdef DEBUG
            NP::log("setenv detected! with path: " + string(path.c_str()));
#endif
            if(setenv("PATH", path.c_str(), 1) == -1) {
                NP::err("setenv error, with path: " + string(path.c_str()));
            }
            
            needExecute = false;
            break;
            
        } else if (strLine.find("printenv") != string::npos && strLine.find("PATH", 9) != string::npos) {
            
#ifdef DEBUG
            NP::log("printenv detected!");
#endif
            string path = getenv("PATH");
            *remove(path.begin(), path.end(), '\r') = '\0';
            path += "\n";
            path = "PATH=" + path;
            
            NP::writeWrapper(sockfd, path.c_str(), path.length());
            
            needExecute = false;
            break;
        }
        
        // exit
        if (strLine.find("exit") != string::npos) {
#ifdef DEBUG
            NP::log("exit found!");
#endif
            needExit = true;
            break;
        }
        
        string curStr;
        stringstream ss = stringstream(strLine);
        while (ss >> curStr) {
            
            const char* curStr_cstr = curStr.c_str();
            
#ifdef DEBUG
            NP::log(curStr);
#endif
            if (curStr[0] == '|') { // pipe
                
                if (curCl.empty()) {    // no cmd to pipe
                    break;
                }
                
                // simple `|`
                // @WARNING not implemented for `!`
                if (curStr.length() == 1) {
                    
                    curCl.back().stdoutToRow = 0;
                    
                } else {
                    
                    int extraLine = atoi(curStr.substr(1).c_str());
                    curCl.back().stdoutToRow = extraLine;
                    if (cl.inputLinesLeft < extraLine) {
                        cl.inputLinesLeft = extraLine;
                    }
                }
                
                shouldCurStrBeCmd = true;
                
            } else if (curStr[0] == '!') {
            
                int extraLine = atoi(curStr.substr(1).c_str());
                curCl.back().stderrToRow = extraLine;
                
                if (cl.inputLinesLeft < extraLine) {
                    cl.inputLinesLeft = extraLine;
                }
                
            } else if (curStr[0] == '>') {
                
                string filename;
                ss >> filename;
                
                curCl.back().stdoutToRow = -2;
                curCl.back().toFile = filename;
                
            } else if (shouldCurStrBeCmd && !NP::Command::isCommandValid(curStr_cstr)) {    // invalid cmd
                
#ifdef DEBUG
                NP::log(" -> invalid cmd");
#endif
                string prefix = "Unknown command: [";
                string suffix = "].\n";
                string msg = prefix + curStr + suffix;
                NP::writeWrapper(sockfd, msg.c_str(), msg.length());
                
                // no need to read futher cmds
                break;
                
            } else {    // valid cmd
                
                if (shouldCurStrBeCmd) {    // curStr is cmd
                    
                    Command curCmd = Command(curStr.c_str(), sockfd);
                    curCl.push_back(curCmd);
                    shouldCurStrBeCmd = false;
#ifdef DEBUG
                    NP::log(" -> valid cmd.... pushing back");
#endif
                } else {
#ifdef DEBUG
                    NP::log(" -> shouldCurStrBeCmd == false.... appending arg");
#endif
                    curCl.back().appendArg(curStr);
                }
        
            }
        }
        
#ifdef DEBUG
        NP::log("--------- End of parsing. ---------");
        
#endif
        
        if (curCl.empty()) {
            
#ifdef DEBUG
            NP::log("curCl empty -> continue.");
#endif
            // if curCl is empty -> the first cmd is invalid
            // -> dont process this row
            cl.inputLinesLeft++;
            continue;
        }
        
        // process this line
        NP::processCommand(counter++, curCl, listStdout, listStderr);
    }

    if (needExit) {
#ifdef DEBUG
        NP::log("needExit -> return true");
#endif
        return true;
        
    } else if (!needExecute) {
#ifdef DEBUG
        NP::log("!needExecute -> return false");
#endif
        return false;
    }

    return false;
}

/**
 *	Process command
 *
 *	@param cl	a vector of Command
 */
void NP::processCommand(const int no, vector<Command>& line, list<pair<pair<int, int>, string>>& listStdout, list<pair<pair<int, int>, string>>& listStderr) {

#ifdef DEBUG
    NP::log("processCommand: no. " + to_string(no));
#endif
    
    const int totalCmd = line.size();
    
    for (int curCmd = 0; curCmd < totalCmd; curCmd++) {
        
        // Create pipe for this children
        int input[2];   // write data to child
        int stdoutOutput[2];    // child's stdout output
        int stderrOutput[2];    // child's stderr otuput
        
        pipe(input);
        pipe(stdoutOutput);
        pipe(stderrOutput);
        
        pid_t child = fork();
        switch (child) {
            case -1:    // error
                NP::err("fork error");
                
            case 0: // child
            {
                /**
                 *	Prepare pipe
                 */
                // stdout
                NP::dup2(stdoutOutput[1], STDOUT_FILENO, false);
                close(stdoutOutput[1], -1, stdoutOutput[1], "", "", false);
                close(stdoutOutput[0], -1, stdoutOutput[0], "", "", false);
                
                // stderr
                NP::dup2(stderrOutput[1], STDERR_FILENO, false);
                close(stderrOutput[1], -1, stderrOutput[1], "", "", false);
                close(stderrOutput[0], -1, stderrOutput[0], "", "", false);
                
                // stdin
                NP::dup2(input[0], STDIN_FILENO, false);
                close(input[1], -1, stderrOutput[1], "", "", false);
                close(input[0], -1, stderrOutput[0], "", "", false);
                
                // exec
                char* file = NP::Command::whereis(line[curCmd].arg[0].c_str());
                if(execvp(file, line[curCmd].toArgArray()) == -1) {
                    
                    string env = string(getenv("PATH"));
                    *remove(env.begin(), env.end(), '\r') = ' ';
                    NP::err("execvp error: " + string(file) + ", with PATH: " + env);
//                        NP::err("execvp error: " + string(file) + ", with PATH: " + env + ", pwd = " + get_current_dir_name());
                }
            }
                
            default:    // parent
                
                // if list has input to child, write it now
                list<pair<pair<int, int>, string>>::iterator itStdout = NP::findTemp(listStdout, make_pair(no, curCmd));
                list<pair<pair<int, int>, string>>::iterator itStderr = NP::findTemp(listStderr, make_pair(no, curCmd));
                
                // stdout found
                if (itStdout != listStdout.end()) {
                    NP::writeWrapper(input[1], (itStdout->second).c_str(), (itStdout->second).length());
#ifdef DEBUG
                    NP::log("[parent] (listStdout) found stdout for (" + to_string(no) + "," + to_string(curCmd) + "): (via fd " + to_string(input[1]) + ")\n" + itStdout->second);
#endif
                    listStdout.remove(*itStdout);
                    
                } else {
#ifdef DEBUG
                    NP::log("[parent] (listStdout) nothing found stdout for (" + to_string(no) + "," + to_string(curCmd) + ")");
#endif
                }
                
                // stderr found
                if (itStderr != listStderr.end()) {
                    NP::writeWrapper(input[1], (itStderr->second).c_str(), (itStderr->second).length());
#ifdef DEBUG
                    NP::log("[parent] (listStderr) found stderr for (" + to_string(no) + "," + to_string(curCmd) + "): (via fd " + to_string(input[1]) + ")\n" + itStderr->second);
#endif
                    listStderr.remove(*itStderr);
                    
                } else {
#ifdef DEBUG
                    NP::log("[parent] (listStderr) nothing found stderr for (" + to_string(no) + "," + to_string(curCmd) + ")");
#endif
                }
                
                // close write end of 3 pipes
                string closeMsg = "[parent] close write pipe(" + to_string(no) + "," + to_string(curCmd) + ") ";
                NP::close(input[1], -1, -1, closeMsg + "[input] error", closeMsg + "[input] done");
                NP::close(stdoutOutput[1], -1, -1, closeMsg + "[stdoutOutput] error", closeMsg + "[stdoutOutput] done");
                NP::close(stderrOutput[1], -1, -1, closeMsg + "[stderrOutput] error", closeMsg + "[stderrOutput] done");
                
                // wait
                waitpid(child, NULL, 0);
                
                // read from child output
                string strChildStdoutOutput = NP::readWrapper(stdoutOutput[0], false);
                string strChildStderrOutput = NP::readWrapper(stderrOutput[0], false);
                
                // close read end of 3 pipes
                closeMsg = "[parent] close read pipe(" + to_string(no) + "," + to_string(curCmd) + ") ";
                NP::close(input[0], -1, -1, closeMsg + "[input] error", closeMsg + "[input] done");
                NP::close(stdoutOutput[0], -1, -1, closeMsg + "[stdoutOutput] error", closeMsg + "[stdoutOutput] done");
                NP::close(stderrOutput[0], -1, -1, closeMsg + "[stderrOutput] error", closeMsg + "[stderrOutput] done");
                
                /**
                 *	if child has output, we may
                 *      1) store to list ( >= 0)
                 *      2) output to sockfd ( == -1)
                 *      3) output to file ( == -2)
                 */
                // stdout
                if (strChildStdoutOutput.empty()) {
#ifdef DEBUG
                    NP::log("[parent] child(" + to_string(no) + "," + to_string(curCmd) + ") no stdout");
#endif
                } else {
#ifdef DEBUG
                    NP::log("[parent] child(" + to_string(no) + "," + to_string(curCmd) + ") has stdout to row " + to_string(no) + " + " + to_string(line[curCmd].stdoutToRow) + ":\n" + strChildStdoutOutput);
#endif
                    switch (line[curCmd].stdoutToRow) {
                            
                        case -2:    // to file
                        {
                            ofstream fileOutput;
                            fileOutput.open(line[curCmd].toFile, ios::out | ios::trunc);
                            fileOutput << strChildStdoutOutput;
                            fileOutput.close();
                            break;
                        }
                            
                        case -1:    // to sockfd
                            NP::writeWrapper(line[curCmd].sockfd, strChildStdoutOutput.c_str(), strChildStdoutOutput.length());
                            break;
                            
                        case 0: // to next cmd
                            listStdout.push_back(make_pair(make_pair(no, curCmd+1), strChildStdoutOutput));
                            break;
                            
                        default:    // to next N row
                        {
                            // have to check if there's already something for that row
                            list<pair<pair<int, int>, string>>::iterator itStdout = NP::findTemp(listStdout, make_pair(no + line[curCmd].stdoutToRow, curCmd));
                            
                            if (itStdout != listStdout.end()) { // something found
#ifdef DEBUG
                                NP::log("for row " + to_string(no+line[curCmd].stdoutToRow) + " already existed output:\n" + itStdout->second);
#endif
                                itStdout->second += strChildStdoutOutput;
                                
                            } else {    // nothing found
#ifdef DEBUG
                                NP::log("nothing found for row " + to_string(no+line[curCmd].stdoutToRow));
#endif
                                
                                listStdout.push_back(make_pair(make_pair(no+line[curCmd].stdoutToRow, 0), strChildStdoutOutput));
                            }
                        }
                    }
                    
                }
                
                // stderr
                if (strChildStderrOutput.empty()) {
#ifdef DEBUG
                    NP::log("[parent] child(" + to_string(no) + "," + to_string(curCmd) + ") no stderr");
#endif
                } else {
#ifdef DEBUG
                    NP::log("[parent] child(" + to_string(no) + "," + to_string(curCmd) + ") has stderr to row " + to_string((no)) + " + " + to_string(line[curCmd].stderrToRow) + ":\n" + strChildStderrOutput);
#endif
                    switch (line[curCmd].stderrToRow) {
                            
                        case -1:    // to sockfd
                            NP::writeWrapper(line[curCmd].sockfd, strChildStderrOutput.c_str(), strChildStderrOutput.length());
                            break;
                            
                        case 0: // to next cmd
                            listStderr.push_back(make_pair(make_pair(no, curCmd+1), strChildStderrOutput));
                            break;
                            
                        default:    // to next N row
                        {
                            // have to check if there's already something for that row
                            list<pair<pair<int, int>, string>>::iterator itStderr = NP::findTemp(listStderr, make_pair(no + line[curCmd].stderrToRow, curCmd));
                            
                            if (itStderr != listStderr.end()) { // something found
#ifdef DEBUG
                                NP::log("for row " + to_string(no+line[curCmd].stderrToRow) + " already existed output:\n" + itStderr->second);
#endif
                                itStderr->second += strChildStderrOutput;
                                
                            } else {    // nothing found
#ifdef DEBUG
                                NP::log("nothing found for row " + to_string(no+line[curCmd].stderrToRow));
#endif
                                
                                listStderr.push_back(make_pair(make_pair(no+line[curCmd].stderrToRow, 0), strChildStderrOutput));
                            }
                        }
                    }
                    
                }
//@TODO: stdout & stderr wrong order.
        }
        
    }

}