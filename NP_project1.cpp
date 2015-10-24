//  NCTU CS. Network Programming Assignment 1
//  by Denny Chien-Yu Huang.

//    A Command Line (a.k.a. cl)
//     ---------------------
//    |    --               | <- a line
//    |   |ls| <- a command |
//    |    --               |
//    |                     |
//    |                     |
//    |                     |
//     ---------------------

#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include <list>
#include <utility>
using namespace std;

#define DEBUG

#define MAX_SIZE 15001
#define DEFAULT_PORT 4412

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
    
    void prepareChildHead(int, const int, vector<int*>&, vector<Command>&);
    
    void deamonPreparation(vector<vector<Command>>& cl);
    
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
        while (it != l.begin()) {
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
//		const char* name;	// command name
//		const char* arg;	// arguments
        vector<string> arg;
		// stdout/err to next N row; -1 indicates sockfd; 0 means next cmd;
        int stdoutToRow = -1;
        int stderrToRow = -1;
        int sockfd;
        
//        Command(const char* name, const char* arg, int stdoutRow, int stderrRow): name(name), arg(arg), stdoutToRow(stdoutRow), stderrToRow(stderrRow){}
//        
//        Command(const char* name, const char* arg, int stdoutRow) {
//            Command(name, arg, stdoutRow, -1);
//        }
//        
//        Command(const char* name, const char* arg) {
//            Command(name, arg, -1, -1);
//        }
        
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

//#ifdef DEBUG
//            NP::log("whereis(" + string(cmd) +")");
//#endif
            // to get SAME path, we need to make a copy of this.
            int kPathLen = strlen(kPath);
            char* path = new char[kPathLen+1];
            strcpy(path, kPath);
            
//#ifdef DEBUG
//            //            NP::log("isCommandValid: " + string(cmd) + " [PATH: " + string(path) + "]");
//            NP::log("kPathLen = " + std::to_string(kPathLen));
//            NP::log_ch(path);
//#endif
            
            char* token = strtok(path, ":");
            while (token != NULL) {
                
                int len = strlen(token) + 1 + strlen(cmd);
                char* file = new char[len+1];
                strcpy(file, token);
                strcat(file, "/");
                strcat(file, cmd);
                *remove(file, file+len, '\r') = '\0';
                
//#ifdef DEBUG
//                NP::log("Processing token: (len = " + std::to_string(strlen(token)) + "+1+" + std::to_string(strlen(cmd)) + "=" + std::to_string(len) + ")", 0, 1, 0);
//                NP::log_ch(token, 0, 1);
//                NP::log("trying ", 0, 0);
//                NP::log_ch(file, 0, 0, 0);
//                NP::log("...", 0, 0, 0);
//#endif
                if (access(file, X_OK) == 0) {
//#ifdef DEBUG
//                    NP::log("true.", 0, 1, 0);
//#endif
                    delete[] path;
                    
                    return file;
                }
//#ifdef DEBUG
//                NP::log("false, due to " + std::to_string(errno), 0, 1, 0);
//#endif
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
            char welcomeMsg[] = "\
**************************************** \n\
** Welcome to the information server. ** \n\
****************************************\n";
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
    
	// read cmd
	while (cl.inputLinesLeft--) {
        
        strLine = NP::readWrapper(sockfd);

//        if ((str = NP::readWrapper(sockfd)).find("exit") == string::npos) {
//#ifdef DEBUG
//            NP::log("exit detected.");
//#endif
//            needExit = true;
//            break;
//        }
        
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
        bool shouldCurStrBeCmd = true;  // indicates if the current string is a cmd (else is a arg)
        
        if (strLine.find("setenv") != string::npos && strLine.find("PATH", 7) != string::npos) {
#ifdef LOCAL
            string dir = "/Users/ChienyuHuang/ras";
            string path = dir + string("/") + strLine.substr(12);
#else
            string path = get_current_dir_name() + string("/") + strLine.substr(12);
#endif
            path = path.substr(0, path.length()-1);
//            @TODO: trim '\n'
#ifdef DEBUG
            NP::log("setenv detected! with path: " + string(path.c_str()));
#endif
            if(setenv("PATH", path.c_str(), 1) == -1) {
                NP::err("setenv error, with path: " + string(path.c_str()));
            }
            
            needExecute = false;
            break;
        }
        
        if (strLine.find("exit") != string::npos) {
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
                    cl.inputLinesLeft += extraLine;
                }
                
                shouldCurStrBeCmd = true;
                
            } else if (curStr[0] == '!') {
            
                int extraLine = atoi(curStr.substr(1).c_str());
                curCl.back().stderrToRow = extraLine;
                
                if (cl.inputLinesLeft < extraLine) {
                    cl.inputLinesLeft = extraLine;
                }
                
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
            continue;
            
        } else {
            
#ifdef DEBUG
            NP::log("curCl nonempty -> push back.");
#endif
            // otherwise, push back this row
            cl.cmds.push_back(curCl);
        }
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
    
    
#ifdef DEBUG
    NP::log("cl: ");
    for (int i = 0; i < cl.cmds.size(); i++) {
        for (int j = 0; j < cl.cmds[i].size(); j++) {
            for (int k = 0; k < cl.cmds[i][j].arg.size(); k++) {
                NP::log(cl.cmds[i][j].arg[k], 0, 0, 0);
                NP::log(" ", 0, 0, 0);
            }
            NP::log("\n", 0, 0, 0);
        }
    }
    NP::log("cl end.");
    NP::log("Preparing to execute cmd...");
#endif
    
    /**
     *  Execute Command
     *
     */
    NP::deamonPreparation(cl.cmds);
    
//    pid_t daemon = fork();
//    switch (daemon) {
//        case -1:    // failure
//            NP::err("fork deamon error");
//            
//        case 0: // daemon
//            NP::deamonPreparation(cl.cmds);
//            break;
//        
//        default:    // parent
//            waitpid(daemon, NULL, 0);
//            break;
//    }
    
//    
//    vector<pid_t> pids;
//    
//    for (int i = 0; i < totalLine; i++) {
//        
//        // fork child
//        pid_t cur = fork();
//        pids.push_back(cur);
//        switch (cur) {
//            case -1:    // error
//                NP::err("fork error");
//                
//            case 0:     // child
//            {
//                // Create pipe
//#ifdef DEBUG
//                NP::log("Creating pipes.....", 0, 0 ,1);
//#endif
//                vector<int*> pipes = vector<int*>(totalLine, NULL);
//                for (int i = 0; i < totalLine; i++) {
//                    pipes[i] = new int[2];
//                    if (pipe(pipes[i]) == -1) {
//                        NP::err("pipe no. " + to_string(i) + "creation error");
//                    }
//                }
//#ifdef DEBUG
//                NP::log("done", 0, 1, 0);
//                NP::log("Start forking child... with totoalLine = " + to_string(totalLine));
//#endif
//                NP::prepareChildHead(i, totalLine, pipes, cl.cmds[i]);
//                break;
//            }
//                
//            default:
//                // parent has to wait
//#ifdef DEBUG
//                NP::log("parent waiting for pid " + to_string(cur));
//#endif
//                waitpid(cur, NULL, 0);
//#ifdef DEBUG
//                NP::log("parent waiting for pid " + to_string(cur) + " over");
//#endif
//                
//                break;
//        }
//    }
//    
//#ifdef DEBUG
//    NP::log("closing parent's pipe...", 0, 0);
//#endif
//    // close parent's pipe (all)
//    for (int i = 0; i < totalLine; i++) {
//        if (close(pipes[i][0]) || close(pipes[i][1])) {
//            NP::err("parent close pipe error.");
//        }
//    }
//#ifdef DEBUG
//    NP::log(" done", 0, 1, 0);
//#endif
    
//    // parent has to wait
//    for (int i = 0; i < totalLine; i++) {
//#ifdef DEBUG
//        NP::log("parent waiting for pid " + to_string(pids[i]));
//#endif
//        waitpid(pids[i], NULL, 0);
//#ifdef DEBUG
//        NP::log("parent waiting for pid " + to_string(pids[i]) + " over");
//#endif
//    }

//    // free pipe
//    for (int i = 0; i < totalLine; i++) {
//        delete[] pipes[i];
//    }
    
    return false;
}

/**
 *	for daemon to process cmds
 *
 *	@param cl	all commands
 */
void NP::deamonPreparation(vector<vector<Command>>& cl){
    
    const int totalLines = cl.size();
    list<pair<pair<int, int>, string>> listStdout;    // store stdout temporarily
    list<pair<pair<int, int>, string>> listStderr;    // store stderr temporarily
    
    for (int curClNow = 0; curClNow < totalLines; curClNow++) {
        
        int totalCmd = cl[curClNow].size();
        
        for (int curCmd = 0; curCmd < totalCmd; curCmd++) {

#ifdef DEBUG
            NP::log("this is cl[" + to_string(curClNow) + "][" + to_string(curCmd) + "] = " + cl[curClNow][curCmd].arg[0]);
#endif
            
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
                    
//#ifdef DEBUG
//                    NP::log("Ready to execute cmd: " + cl[curClNow][curCmd].arg[0]);
//#endif
                    
                    // exec
                    if(execv(NP::Command::whereis(cl[curClNow][curCmd].arg[0].c_str()), cl[curClNow][curCmd].toArgArray()) == -1) {
                        string env = string(getenv("PATH"));
                        *remove(env.begin(), env.end(), '\r') = '\0';
                        NP::err("execvp error: " + cl[curClNow][curCmd].arg[0] + ", with PATH: " + env);
                    }

                default:    // parent
                    
                    // if list has input to child, write it now
                    list<pair<pair<int, int>, string>>::iterator itStdout = NP::findTemp(listStdout, make_pair(curClNow, curCmd));
                    list<pair<pair<int, int>, string>>::iterator itStderr = NP::findTemp(listStderr, make_pair(curClNow, curCmd));
                    
                    // stdout found
                    if (itStdout != listStdout.end()) {
                        NP::writeWrapper(input[1], (itStdout->second).c_str(), (itStdout->second).length());
#ifdef DEBUG
                        NP::log("[parent] found stdout for (" + to_string(curClNow) + "," + to_string(curCmd) + "): (via fd " + to_string(input[1]) + ")\n" + itStdout->second);
#endif
                        listStdout.remove(*itStdout);
                        
                    } else {
#ifdef DEBUG
                        NP::log("[parent] nothing found stdout for (" + to_string(curClNow) + "," + to_string(curCmd) + ")");
#endif
                    }
                    
                    // stderr found
                    if (itStderr != listStderr.end()) {
                        NP::writeWrapper(input[1], (itStderr->second).c_str(), (itStderr->second).length());
#ifdef DEBUG
                        NP::log("[parent] found stderr for (" + to_string(curClNow) + "," + to_string(curCmd) + "): (via fd " + to_string(input[1]) + ")\n" + itStderr->second);
#endif
                        listStderr.remove(*itStderr);
                        
                    } else {
#ifdef DEBUG
                        NP::log("[parent] nothing found stderr for (" + to_string(curClNow) + "," + to_string(curCmd) + ")");
#endif
                    }
                    
                    // close write end of 3 pipes
                    string closeMsg = "[parent] close write pipe(" + to_string(curClNow) + "," + to_string(curCmd) + ") ";
                    NP::close(input[1], -1, -1, closeMsg + "[input] error", closeMsg + "[input] done");
                    NP::close(stdoutOutput[1], -1, -1, closeMsg + "[stdoutOutput] error", closeMsg + "[stdoutOutput] done");
                    NP::close(stderrOutput[1], -1, -1, closeMsg + "[stderrOutput] error", closeMsg + "[stderrOutput] done");
                    
                    // wait
                    waitpid(child, NULL, 0);
                    
                    // read from child output
                    string strChildStdoutOutput = NP::readWrapper(stdoutOutput[0], false);
                    string strChildStderrOutput = NP::readWrapper(stderrOutput[0], false);
                    
                    // close read end of 3 pipes
                    closeMsg = "[parent] close read pipe(" + to_string(curClNow) + "," + to_string(curCmd) + ") ";
                    NP::close(input[0], -1, -1, closeMsg + "[input] error", closeMsg + "[input] done");
                    NP::close(stdoutOutput[0], -1, -1, closeMsg + "[stdoutOutput] error", closeMsg + "[stdoutOutput] done");
                    NP::close(stderrOutput[0], -1, -1, closeMsg + "[stderrOutput] error", closeMsg + "[stderrOutput] done");
                    
                    // if child has output, we may
                    //  1) store to list
                    //  2) output to sockfd
                    if (strChildStdoutOutput.empty()) {
#ifdef DEBUG
                        NP::log("[parent] child(" + to_string(curClNow) + "," + to_string(curCmd) + ") no stdout");
#endif
                        
                    } else {
#ifdef DEBUG
                        NP::log("[parent] child(" + to_string(curClNow) + "," + to_string(curCmd) + ") has stdout[" + to_string(cl[curClNow][curCmd].stdoutToRow) + "]:\n" + strChildStdoutOutput);
#endif
                        switch (cl[curClNow][curCmd].stdoutToRow) {
                                
                            case -1:    // to sockfd
                                NP::writeWrapper(cl[curClNow][curCmd].sockfd, strChildStdoutOutput.c_str(), strChildStdoutOutput.length());
                                break;
                                
                            case 0: // to next cmd
                                listStdout.push_back(make_pair(make_pair(curClNow, curCmd+1), strChildStdoutOutput));
                                break;
                                
                            default:    // to next N row
                                listStdout.push_back(make_pair(make_pair(curClNow+cl[curClNow][curCmd].stdoutToRow, 0), strChildStdoutOutput));
                        }
                        
                    }

            }
            
        }
    }
}


/**
 *  prepare child head
 *
 *  @param curClNum     the cl number this child belongs to
 *  @param totalLine total cl num
 *  @param pipes     all pipes
 *  @param cmds      commands that belong to this cl
 */
void NP::prepareChildHead(const int curClNum, const int totalLine, vector<int*>& pipes, vector<Command>& cmds) {

    /**
     *  Prepare pipe: close unused pipe
     */
#ifdef DEBUG
    NP::log("in prepareChildHead(curClNum = "+ to_string(curClNum) +"): Closing unused pipes.....");
#endif
    int stdoutToRow = cmds.back().stdoutToRow > 0 ? curClNum + cmds.back().stdoutToRow : -1;
    int stderrToRow = cmds.back().stderrToRow > 0 ? curClNum + cmds.back().stderrToRow : -1;
    for (int i = 0; i < totalLine; i++) {
        
        if (stdoutToRow == stderrToRow && stdoutToRow != -1) {
            
            // check if stdoutToRow and stderrToRow are the same
            NP::err("in afterForkChild(): stdoutPipe == stderrPipe, with i = " + to_string(i));

        } else if (i == stdoutToRow || i == stderrToRow) {
            
            // close read, left write only
            NP::close(pipes[i][0], i, 0, "in afterForkChild(): close stdoutToRow(" + to_string(stdoutToRow) + ") or stderrToRow(" + to_string(stderrToRow) + ") error(" + to_string(errno) + "), with i = " + to_string(i) + ", pipes[i][0] = " + to_string(pipes[i][0]), "close pipes[" + to_string(i) + "][" + to_string(0) + "]");
            
        } else if (i == curClNum) {
            
            // intentionally left blank
            // Dont do anything to clNum
            
        } else {

            // close other pipes
            NP::close(pipes[i][0], i, 0, "parent close pipe["+ to_string(i) +"][0] error.");
            NP::close(pipes[i][1], i, 1, "parent close pipe["+ to_string(i) +"][1] error.");
        }
    }
#ifdef DEBUG
    NP::log("in prepareChildHead: Closing unused pipes..... done");
    NP::log("in afterForkChild(): starting executing " + to_string(cmds.size()) + " cmds....");
#endif
    
    /**
     *  Fork: Start executing cmd
     */
    int cmdCount = cmds.size();
    int pid;
    for (int i = 0; i < cmdCount; i++) {

        switch (pid = fork()) {
            case -1: // error
                NP::err("fork error");
                
            case 0: // child
#ifdef DEBUG
                NP::log("this is child. preparing pipe for " + cmds[i].arg[0] + "(" + to_string(cmds[i].stdoutToRow) + "," + to_string(cmds[i].stderrToRow) + ") with curClNum = " + to_string(curClNum));
#endif
                /**
                 *  Prepare Pipe
                 */
                // stdout
                if (cmds[i].stdoutToRow == -1) {    // stdout to sockfd
                    
                    NP::dup2(cmds[i].sockfd, STDOUT_FILENO);
                    
                } else if (cmds[i].stdoutToRow == 0) {  // stdout to next cmd
                    
                    NP::dup2(pipes[curClNum][1], STDOUT_FILENO);
                    NP::close(pipes[curClNum][1], curClNum, 1);
                    
                } else {    // stdout to next N row
                    
                    int ii = curClNum+cmds[i].stdoutToRow;
                    NP::dup2(pipes[ii][1], STDOUT_FILENO);
                    NP::close(pipes[ii][1], ii, 1);
                }
                
                // stderr
                if (cmds[i].stderrToRow == -1) {    // stderr to sockfd
                    
                    NP::dup2(cmds[i].sockfd, STDERR_FILENO);
                    
                } else if (cmds[i].stderrToRow == 0) {  // stderr to next cmd
                    
                    NP::dup2(pipes[curClNum][1], STDERR_FILENO);
                    NP::close(pipes[curClNum][1], curClNum, 1);
                    
                } else {    // stderr to next N row
                    
                    int ii = curClNum+cmds[i].stderrToRow;
                    NP::dup2(pipes[ii][1], STDERR_FILENO);
                    NP::close(pipes[ii][1], ii , 1);
                }

                // stdin
                NP::dup2(pipes[curClNum][0], STDIN_FILENO);
                NP::close(pipes[curClNum][0], curClNum, 0);
                
//#ifdef DEBUG
//                NP::log("this is child. Pipe ready. Executing...");
//                NP::log("Executing " + cmds[i].to_string());
//                NP::log("end of cmd list");
//#endif
                
                if(execv(NP::Command::whereis(cmds[i].arg[0].c_str()), cmds[i].toArgArray()) == -1) {
                    string env = string(getenv("PATH"));
                    *remove(env.begin(), env.end(), '\r') = '\0';
                    NP::err("execvp error: " + cmds[i].arg[0] + ", with PATH: " + env);
                }
                
                NP::err("in prepareChildHead: fork: case 0 error");
                
//            default:    // parent
//
//                NP::log("[parent] parent started.");
//                
////                if (close(pipes[curClNum][0])) {
////                    NP::log("! close(pipes[" + to_string(curClNum) +"][0]) errer");
////                }
//                
//                NP::close(pipes[curClNum][1], curClNum, 1, "! [parent] close(pipes[" + to_string(curClNum) +"][1]) error");
//                
////                NP::log("[parent] close pipe[" + to_string(curClNum) + "][" + to_string(0) + "]");
//                
//                if (cmds[i].stdoutToRow > 0) {
//                    
//                    int ii = curClNum+cmds[i].stdoutToRow;
//                    NP::close(pipes[ii][1], ii, 1, "! [parent] close(pipes[curClNum+cmds[i].stdoutToRow][1])", "[parent] close pipes[" + to_string(ii) + "][1]");
//                }
//                
//                if (cmds[i].stderrToRow > 0) {
//                    
//                    int ii = curClNum+cmds[i].stderrToRow;
//                    NP::close(pipes[ii][1], ii, 1, "! [parent] close(pipes[curClNum+cmds[i].stderrToRow][1])", "[parent] close pipes[" + to_string(ii) + "][1]");
//                }
//#ifdef DEBUG
//                NP::log("[parent] in prepareChildHead(): waitpid(" + to_string(pid) + ")");
//#endif
//                waitpid(pid, NULL, 0);
//#ifdef DEBUG
//                NP::log("[parent] in prepareChildHead(): waitpid(" + to_string(pid) +  ") over");
//#endif
        }
    
    }
    
    // parent
    
    NP::log("[parent] parent started.");
    
    //                if (close(pipes[curClNum][0])) {
    //                    NP::log("! close(pipes[" + to_string(curClNum) +"][0]) errer");
    //                }
    
    NP::close(pipes[curClNum][1], curClNum, 1, "! [parent] close(pipes[" + to_string(curClNum) +"][1]) error", "[parent] close(pipes[" + to_string(curClNum) +"][1])");
    
    //                NP::log("[parent] close pipe[" + to_string(curClNum) + "][" + to_string(0) + "]");
    for (int i = 0; i < cmdCount; i++) {
        
        if (cmds[i].stdoutToRow > 0) {
            
            int ii = curClNum+cmds[i].stdoutToRow;
            if(::close(pipes[ii][1]) == -1) {
                
                NP::log("! [parent] close(pipes[curClNum+cmds[i].stdoutToRow][1])");

            } else {
            
                NP::log("[parent] close pipes[" + to_string(ii) + "][1]");
            }
        }
        
        if (cmds[i].stderrToRow > 0) {
            
            int ii = curClNum+cmds[i].stderrToRow;
            if(::close(pipes[ii][1]) == -1) {
                
                NP::log("! [parent] close(pipes[curClNum+cmds[i].stderrToRow][1])");
                
            } else {
                
                NP::log("[parent] close pipes[" + to_string(ii) + "][1]");
            }
            
        }
    }

    
    for (int i = 0; i < cmdCount; i++) {

#ifdef DEBUG
        NP::log("[parent] in prepareChildHead(): wait()");
#endif
        
        wait(NULL);

#ifdef DEBUG
        NP::log("[parent] in prepareChildHead(): wait() over");
#endif
    }
    
    NP::log("[parent] exit()");
    exit(EXIT_SUCCESS);
}