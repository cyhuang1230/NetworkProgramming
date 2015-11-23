//  NCTU CS. Network Programming Assignment 2
//  : Remote Working Ground (RWG)
//  : Ver.I - Single-process concurrent paradigm
//  :: Please refer to `hw2spec.txt` for more details.

//  Code by Denny Chien-Yu Huang, 11/12/15.
//  Github: http://cyhuang1230.github.io/

/*  For HW1 */
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

/*  For HW2 */
/**
 *  Main idea:
 *      - Basically same structure as ver. II, 
 *        but as a single process program, we dont need any shm or semaphore, which makes it much easier, yayyy!.
 *      - Only involves one huge adjustment:
 *        We can no longer use a while loop to get whole input from `numbered-pipe`.
 *        If we continue to do so, program will stuck in the following input:
 *          [Client 1] % ls |1
 *          [Client 2] % ls |1
 *          [Client 1] % cat
 *          [Client 2] ________ <- program stucks, prompt wont display
 *       => Instead, we have to check input ***line by line***, and keep track of input-related information.
 */

// @TODO: Need public pipe translator to prevent pipe_id > 100

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
#include <sys/select.h>
using namespace std;

#define MAX_SIZE 15001
#define DEFAULT_PORT 4411
#define MAX_USER 30 // 1~30
#define MAX_PUBLIC_PIPE 101 // 1~100
#define PIPE_SIZE (1025*sizeof(char))
#define PIPE_SIZE_TOTAL ((PIPE_SIZE)*(MAX_PUBLIC_PIPE))
#define USER_MSG_BUFFER (1025*sizeof(char))
#define USER_MSG_BUFFER_TOTAL (USER_MSG_BUFFER*(MAX_USER+1))
#define USER_NAME_SIZE 20
//#define DEBUG 1

char buffer[MAX_SIZE];

namespace NP {
    
    class Command;
    class CommandLine;
    
    void log(string str, bool error = false, bool newline = true, bool prefix = true);
    
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
    
    void processCommand(const int n, vector<Command>&, list<pair<pair<int, int>, string>>&);
    
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
        
		// N: stdout/err to next N row; -1: sockfd; 0: next cmd, -2: to file (stdout only), <=-3: to public pipe -N-3 [-4: public pipe 1];
        int stdoutToRow = -1;
        int stderrToRow = -1;
        int stdinFromPipe = -1;
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
        
        static char* whereis(const char* cmd);
        
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
//        int inputLinesLeft = 1; // the number of lines to input
    };
    
    /* For HW2 */
    
     /// Required info of each client
    class Client {
    public:
        int id = 0;
        char userName[USER_NAME_SIZE+1] = "(no name)";
        int pid;
        int sockfd;
        char ip[INET_ADDRSTRLEN];
        int port;
        char  msg[USER_MSG_BUFFER];
        string PATH = "bin:.";
        // for input commands
        list<pair<pair<int, int>, string>> listOutput;    // store output temporarily
        int inputLinesLeft = 0;
        int lineCounter = 0;

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
        
    public:
        char curCmd[10000]; // to store command that needs to broadcast
        int curPublicPipe = 0;
        bool isJustReadPublicPipe = 0;  // true: just read; false: just wrote
        friend void debug(int);
        
        /**
         *	Get client by id
         *
         *	@param id
         *
         *	@return ptr to client; NULL if invalid id
         */
        Client* getClientById(int id) {
            
            if (id > MAX_USER || !isUserIdValid(clients[id].id)) {
                return NULL;
            }
            
            return &clients[id];
        }
        
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
        
        /**
         *	Send message to a specific user
         *  Raw message means that we won't change any message content
         *
         *	@return true if operation succeeded; false, otherwise
         */
        bool sendRawMsgToClient(int senderId, int receiverId, string msg);
        
        /**
         *	Send message to every client
         */
        void broadcastRawMsg(int senderId, string msg);
        
        /**
         *	Function `tell`: send msg to spcific user
         *
         *	@return true if operation succeeded; false, otherwise.
         */
        bool tell(int senderId, int receiverId, string msg);
        
        /**
         *	Function `yell`: broadcast message
         */
        void yell(int senderId, string msg);
        
        /**
         *	Function `who`: show client list
         *
         *	@return string for caller to write
         */
        string who(int callerId);
        
        /**
         *	Function `name`: set new name
         *
         *	@return true if operation succeded; false, otherwise.
         */
        bool name(int senderId, string newName);
        
        /**
         *	Return a flag indicating whether a pipe is used
         *
         *	@return true if used; false, otherwise
         */
        bool isPipeUsed(int pipeNum);
        
        /**
         *	Write msg to public pipe #interalId
         *
         *	@param interalId	input id should be translated to interal id before using
         *	@param msg			msg to write
         */
        void writeToPublicPipe(int interalId, string msg);
        
        /**
         *	Read msg from public pipe #interId
         *
         *	@param interalId	input id should be translated to interal id before using
         *
         *	@return msg in that public pipe
         */
        string readFromPublicPipe(int interalId);

        /**
         *	Public pipe id to interal id translator
         *
         *	@param id	input id
         *
         *	@return interal id, ranging from 1 to 100 (inclusive)
         */
        int publicPipeIdToInteralId(int id);
    };
    
    ClientHandler clientHandler;
    Client* iAm;
    string public_pipe[MAX_PUBLIC_PIPE];
}

int main(int argc, const char * argv[]) {
	
	int listenfd, newsockfd, childpid, portnum;
	struct sockaddr_in client_addr, serv_addr;
    char client_addr_str[INET_ADDRSTRLEN]; // client ip addr
    int client_port;   // client port
	socklen_t client_addr_len;
    fd_set fdset, dupset;
    int toCheck = 0, maxfd;
    
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
    
    // Initialize client handler
    NP::clientHandler = NP::ClientHandler();
    
#ifdef DEBUG
	NP::log("Starting server using port: " + to_string(portnum) + " [HW2]");
#endif
	
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		NP::err("Cannot open socket!");
	}

	/**
	 *  Bind
	 */
	bzero((char*)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(portnum);
	
	if (::bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
		NP::err("Bind error");
	}
	
	/**
	 *  Listen
	 */
	listen(listenfd, 30);
    FD_ZERO(&fdset);
    FD_SET(listenfd, &fdset);
    maxfd = listenfd;
    
    /**
	 *  Accept
	 */
	while (1) {

        dupset = fdset;
        toCheck = select(maxfd+1, &dupset, NULL, NULL, NULL);
#ifdef DEBUG
        NP::log("select with toCheck = " + to_string(toCheck));
#endif
        // if it's a new connection
        if (FD_ISSET(listenfd, &dupset)) {

            client_addr_len = sizeof(client_addr);
            newsockfd = ::accept(listenfd, (struct sockaddr *) &client_addr, &client_addr_len);
            if (newsockfd < 0) {
                NP::err("accept error");
            }
            
            // get client addr and port
            inet_ntop(AF_INET, &(client_addr.sin_addr), client_addr_str, INET_ADDRSTRLEN);
            client_port = client_addr.sin_port;

#ifdef DEBUG
            NP::log("Connection from " + string(client_addr_str) + ":" + to_string(client_port) + " accepted.");
#endif
            
            // insert new client
            NP::iAm = NP::clientHandler.insertClient(getpid(), newsockfd, client_addr_str, client_port);
            if (NP::iAm == NULL) {
                NP::err("insert client error");
            }
            
            // Welcome msg
            char welcomeMsg[] =
            "****************************************\n"
            "** Welcome to the information server. **\n"
            "****************************************\n";
            NP::writeWrapper(newsockfd, welcomeMsg, strlen(welcomeMsg));
            
            // broadcast incoming user
            NP::clientHandler.broadcastRawMsg(NP::iAm->id, "*** User '(no name)' entered from " + NP::iAm->getIpRepresentation() + ". ***\n");
            
            NP::writeWrapper(newsockfd, "% ", 2);
            
            FD_SET(newsockfd, &fdset);
            if (newsockfd > maxfd) {
                maxfd = newsockfd;
            }
            
            if (--toCheck <= 0) {
                continue;
            }
        }
        
        for (int i = 1; i <= MAX_USER; i++) {
            
            NP::iAm = NP::clientHandler.getClientById(i);
            if (NP::iAm == NULL || !FD_ISSET(NP::iAm->sockfd, &dupset)) {
                continue;
            }
#ifdef DEBUG
            NP::log("Selected id #" + to_string(NP::iAm->id) + ", with toCheck = " + to_string(toCheck));
#endif
            if (NP::processRequest(NP::iAm->sockfd)) {
                
                // if need to exit
                FD_CLR(NP::iAm->sockfd, &fdset);
                close(NP::iAm->sockfd);
                NP::clientHandler.removeClient(NP::iAm->id);
            }
            
            if (--toCheck > 0) {
                continue;
            }
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
    
    if (NP::iAm->inputLinesLeft == 0) { // first time
        NP::iAm->listOutput = list<pair<pair<int, int>, string>>();
        NP::iAm->lineCounter = 0;
    }
    
    // create a cmd line for this input
    NP::CommandLine cl = CommandLine();
    string strLine;
    bool needExit = false;
    bool needExecute = true;
    bool isFirstInput = true;
    char charLine[10000];
    
    // due to TCP segment(1448 bytes), may cause read line error
    // we need to manually check if the input line is completed
    bool isSameLine = false;
    
    // for `batch`
    bool isBatch = false;
    ifstream file;
    
	// read cmd
	do {
#ifdef DEBUG
        NP::log("NP::iAm->inputLinesLeft = " + to_string(NP::iAm->inputLinesLeft));
#endif
        if (isBatch) {
            
            strLine = string(); // reset strLine
            string curStr;
            
            file.getline(buffer, MAX_SIZE);
            strLine = buffer;
            strLine += "\n";
            
#ifdef DEBUG
            NP::log("cur cmd from batch:\n" + strLine);
#endif
            
        } else if (isSameLine) {
#ifdef DEBUG
            NP::log("last wasnt a complete line, catenating...");
#endif
            
            strLine += NP::readWrapper(sockfd, false);

        } else {
        
            strLine = NP::readWrapper(sockfd, !isFirstInput);
            isFirstInput = false;
        }
        
        // if the line is a complete line
        if (strLine[strLine.length()-1] == '\n') {
#ifdef DEBUG
            NP::log("this is a complete line");
#endif
            isSameLine = false;

        } else {
#ifdef DEBUG
            NP::log("this is NOT a complete line");
#endif
            isSameLine = true;
        }
        
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

            char* chPath = new char[path.length()+1];
            strcpy(chPath, path.c_str());
            chPath = strtok(chPath, "\n\r");
            
#ifdef DEBUG
            NP::log("setenv detected! with path: " + string(chPath));
#endif
            NP::iAm->PATH = string(chPath);
            delete [] chPath;
            
            needExecute = false;
            break;
            
        } else if (strLine.find("printenv") != string::npos && strLine.find("PATH", 9) != string::npos) {
            
#ifdef DEBUG
            NP::log("printenv detected!");
#endif
            string path = "PATH=" + NP::iAm->PATH + "\n";
            NP::writeWrapper(sockfd, path.c_str(), path.length());
            
            needExecute = false;
            break;

        }  else if (strLine.find("batch") == 0) {   // batch

            string name = strLine.substr(6);
            *remove(name.begin(), name.end(), '\n') = '\0'; // trim '\n'
            *remove(name.begin(), name.end(), '\r') = '\0'; // trim '\r'
            NP::iAm->inputLinesLeft++;

#ifdef DEBUG
            NP::log("batch with file: `" + name + "`");
#endif
            
            file.open(name, ifstream::in);
            
            if (!file.good()) {
                NP::log("bad file");
                continue;
            }
            
            isBatch = true;
            continue;

        } else if (strLine.find("who") == 0) {  // who
            
            string whoMsg = NP::clientHandler.who(NP::iAm->id);
            NP::writeWrapper(sockfd, whoMsg.c_str(), whoMsg.length());

            needExecute = false;
            // @WARNING: should change to continue; to allow pipe?
            break;
            
        } else if (strLine.find("name") == 0) {  // name
            
            string newName = strLine.substr(5);
            char* charName = new char[newName.length()+1];
            strcpy(charName, newName.c_str());
            charName = strtok(charName, "\n\r");
            newName = string(charName);

            if (NP::clientHandler.name(NP::iAm->id, newName)) {
            
                // name successfully
                string ret = "*** User from " + NP::iAm->getIpRepresentation() + " is named '" + newName +"'. ***\n";
                NP::clientHandler.broadcastRawMsg(NP::iAm->id, ret);
                
            } else {
                
                // failed to name
                string ret = "*** User '" + newName + "' already exists. ***\n";
                NP::writeWrapper(sockfd, ret.c_str(), ret.length());
            }
            
            delete [] charName;
            needExecute = false;
            // @WARNING: should change to continue; to allow pipe?
            break;
            
        } else if (strLine.find("yell") == 0) {  // yell
            
            string msg = strLine.substr(5, strLine.find('\r')-5);
            
            NP::clientHandler.yell(NP::iAm->id, msg);
            
            needExecute = false;
            // @WARNING: should change to continue; to allow pipe?
            break;
            
        } else if (strLine.find("tell") == 0) {  // tell
            
            char* cmd = new char[strLine.length()-5+1];
            strcpy(cmd, strLine.substr(5).c_str());
            char* chId = strtok(cmd, " ");
            int id = atoi(chId);
            char* msg = strtok(NULL, "\n\r");
            
            if (!NP::clientHandler.tell(NP::iAm->id, id, msg)) {

                string msg = "*** Error: user #" + to_string(id) + " does not exist yet. ***\n";
                NP::writeWrapper(sockfd, msg.c_str(), msg.length());
            }
            
            needExecute = false;
            delete[] cmd;
            
            // @WARNING: should change to continue; to allow pipe?
            break;
        }
        
        // exit
        if (strLine.find("exit") != string::npos) {
#ifdef DEBUG
            NP::log("exit found!");
#endif
            NP::clientHandler.broadcastRawMsg(NP::iAm->id, "*** User '" + NP::iAm->getName() + "' left. ***\n");
            
            needExit = true;
            break;
        }
        
        if (isSameLine) {
            continue;
        }
        
        bool hasReadPublicPipeSymbol = false;
        bool existPipeError = false;
        
        string curStr;
        stringstream ss = stringstream(strLine);
        memset(charLine, 0, sizeof(charLine));
        strcpy(charLine, strLine.c_str());
        char* afterLine = strtok(charLine, "\n\r");
        strcpy(charLine, afterLine);
        
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
                    if (NP::iAm->inputLinesLeft < extraLine) {
                        NP::iAm->inputLinesLeft = extraLine;
                    }
                }
                
                shouldCurStrBeCmd = true;
                
            } else if (curStr[0] == '!') {
            
                int extraLine = atoi(curStr.substr(1).c_str());
                curCl.back().stderrToRow = extraLine;
                
                if (NP::iAm->inputLinesLeft < extraLine) {
                    NP::iAm->inputLinesLeft = extraLine;
                }
                
            } else if (curStr[0] == '>') {
                
                if (curStr.length() == 1) { // to file
                    
                    string filename;
                    ss >> filename;
                    
                    curCl.back().stdoutToRow = -2;
                    curCl.back().toFile = filename;

                } else {    // to public pipe
                    
                    int toPipe = stoi(curStr.substr(1));
                    int toPipeInteral = NP::clientHandler.publicPipeIdToInteralId(toPipe);
#ifdef DEBUG
                    NP::log("pipe result to public pipe #" + to_string(toPipe) + ", interal = " + to_string(toPipeInteral));
#endif
                    // check pipe availability
                    if (NP::clientHandler.isPipeUsed(toPipeInteral)) {
#ifdef DEBUG
                        NP::log("public pipe with internal id #" + to_string(toPipeInteral) + " is used");
#endif
                        string msg = "*** Error: the pipe #" + to_string(toPipe) + " already exists. ***\n";
                        NP::writeWrapper(NP::iAm->sockfd, msg.c_str(), msg.length());
                        existPipeError = true;
                        
                        if (hasReadPublicPipeSymbol) {
                            break;  // we read public pipe symbol before, no need to read next cmd
                        }

                    } else {

                        curCl.back().stdoutToRow = -3 - toPipeInteral;

                        NP::clientHandler.broadcastRawMsg(NP::iAm->id,
                                          "*** " + NP::iAm->getName() + " (#" + to_string(NP::iAm->id) + ") just piped '" + string(charLine) + "' ***\n");
                    }
                    
                    hasReadPublicPipeSymbol = true;
                }
                
            } else if (curStr[0] == '<') {  // read from public pipe
                
                int toPipe = stoi(curStr.substr(1));
                int toPipeInteral = NP::clientHandler.publicPipeIdToInteralId(toPipe);
                
                // check pipe has content or not
                if (!NP::clientHandler.isPipeUsed(toPipeInteral)) {
#ifdef DEBUG
                    NP::log("public pipe with internal id #" + to_string(toPipeInteral) + " is not used");
#endif
                    string msg = "*** Error: the pipe #" + to_string(toPipe) + " does not exist yet. ***\n";
                    NP::writeWrapper(NP::iAm->sockfd, msg.c_str(), msg.length());
                    existPipeError = true;
                    
                    if (hasReadPublicPipeSymbol) {
                        break;  // we read public pipe before, no need to read next cmd
                    }
                } else {

                    curCl.back().stdinFromPipe = toPipeInteral;
                    
                    NP::clientHandler.broadcastRawMsg(NP::iAm->id,
                                      "*** " + NP::iAm->getName() + " (#" + to_string(NP::iAm->id) + ") just received via '" + string(charLine) + "' ***\n");
                }
                
                hasReadPublicPipeSymbol = true;
                
            } else if (existPipeError) {
              
                break;
                
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
            NP::iAm->inputLinesLeft++;
            continue;
        }
        
        // process this line
        NP::processCommand(NP::iAm->lineCounter++, curCl, NP::iAm->listOutput);
    
    } while (isSameLine);

    if (needExit) {
#ifdef DEBUG
        NP::log("needExit -> return true");
#endif
        return true;
    }

    NP::writeWrapper(sockfd, "% ", 2);

    return false;
}

/**
 *	Process command
 *
 *	@param cl	a vector of Command
 */
void NP::processCommand(const int no, vector<Command>& line, list<pair<pair<int, int>, string>>& listOutput) {

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
                    
                    string env = NP::iAm->PATH;
                    
                    NP::err("execvp error: " + string(file) + ", with PATH: " + env);
//                        NP::err("execvp error: " + string(file) + ", with PATH: " + env + ", pwd = " + get_current_dir_name());
                }
            }
                
            default:    // parent
                
                // if list has input to child, write it now
                if (line[curCmd].stdinFromPipe != -1) { // if input is from public pipe
                    
                    string msg = NP::clientHandler.readFromPublicPipe(line[curCmd].stdinFromPipe);
                    NP::writeWrapper(input[1], msg.c_str(), msg.length());
#ifdef DEBUG
                    NP::log("[parent] public pipe #" + to_string(line[curCmd].stdinFromPipe) + " for (" + to_string(no) + "," + to_string(curCmd) + "): (via fd " + to_string(input[1]) + ")\n" + msg);
#endif

                } else {    // if input from list
                    
                    list<pair<pair<int, int>, string>>::iterator itStdout = NP::findTemp(listOutput, make_pair(no, curCmd));
                    
                    // found output
                    if (itStdout != listOutput.end()) {
                        NP::writeWrapper(input[1], (itStdout->second).c_str(), (itStdout->second).length());
#ifdef DEBUG
                        NP::log("[parent] found output for (" + to_string(no) + "," + to_string(curCmd) + "): (via fd " + to_string(input[1]) + ")\n" + itStdout->second);
#endif
                        listOutput.remove(*itStdout);
                        
                    } else {
#ifdef DEBUG
                        NP::log("[parent] nothing found for (" + to_string(no) + "," + to_string(curCmd) + ")");
#endif
                    }
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
                 *      4) output to public pipe N ( == -3-N)
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
                        case -3-MAX_PUBLIC_PIPE ... -4:   // to public pipe (-3 shouldnt happen)
                            NP::clientHandler.writeToPublicPipe(-3-line[curCmd].stdoutToRow, strChildStdoutOutput);
                            break;
                            
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
                            listOutput.push_back(make_pair(make_pair(no, curCmd+1), strChildStdoutOutput));
                            break;
                            
                        default:    // to next N row
                        {
                            // have to check if there's already something for that row
                            list<pair<pair<int, int>, string>>::iterator itStdout = NP::findTemp(listOutput, make_pair(no + line[curCmd].stdoutToRow, curCmd));
                            
                            if (itStdout != listOutput.end()) { // something found
#ifdef DEBUG
                                NP::log("for row " + to_string(no+line[curCmd].stdoutToRow) + " already existed output:\n" + itStdout->second);
#endif
                                itStdout->second += strChildStdoutOutput;
                                
                            } else {    // nothing found
#ifdef DEBUG
                                NP::log("nothing found for row " + to_string(no+line[curCmd].stdoutToRow));
#endif
                                
                                listOutput.push_back(make_pair(make_pair(no+line[curCmd].stdoutToRow, 0), strChildStdoutOutput));
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
                        case -3-MAX_PUBLIC_PIPE ... -4:   // to public pipe (-3 shouldnt happen)
                            NP::clientHandler.writeToPublicPipe(-3-line[curCmd].stdoutToRow, strChildStdoutOutput);
                            break;
                            
                        case -1:    // to sockfd
                            NP::writeWrapper(line[curCmd].sockfd, strChildStderrOutput.c_str(), strChildStderrOutput.length());
                            break;
                            
                        case 0: // to next cmd
                            listOutput.push_back(make_pair(make_pair(no, curCmd+1), strChildStderrOutput));
                            break;
                            
                        default:    // to next N row
                        {
                            // have to check if there's already something for that row
                            list<pair<pair<int, int>, string>>::iterator itStderr = NP::findTemp(listOutput, make_pair(no + line[curCmd].stderrToRow, curCmd));
                            
                            if (itStderr != listOutput.end()) { // something found
#ifdef DEBUG
                                NP::log("for row " + to_string(no+line[curCmd].stderrToRow) + " already existed output:\n" + itStderr->second);
#endif
                                itStderr->second += strChildStderrOutput;
                                
                            } else {    // nothing found
#ifdef DEBUG
                                NP::log("nothing found for row " + to_string(no+line[curCmd].stderrToRow));
#endif
                                
                                listOutput.push_back(make_pair(make_pair(no+line[curCmd].stderrToRow, 0), strChildStderrOutput));
                            }
                        }
                    }
                    
                }

        }
        
    }

}

char* NP::Command::whereis(const char* cmd) {
    
    const char* kPath = NP::iAm->PATH.c_str();
    
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
        file = strtok(file, "\n\r");
        
        if (access(file, X_OK) == 0) {
            
            delete[] path;
            
            return file;
        }
        
        delete[] file;
        token = strtok(NULL, ":");
    }
    
    delete[] path;
    
    return NULL;
}

void NP::log(string str, bool error, bool newline, bool prefix) {
    
    if (error && prefix) {
        cout << "[" << getpid() << ", " + to_string(NP::iAm == NULL ? -1 : NP::iAm->id) + "] ERROR: ";
    } else if (prefix) {
        cout << "[" << getpid() << ", " + to_string(NP::iAm == NULL ? -1 : NP::iAm->id) + "] LOG: ";
    }
    
    cout << str;
    if (newline) {
        cout << endl;
    }
    
    cout.flush();
}

/* For HW2 */

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

bool NP::ClientHandler::sendRawMsgToClient(int senderId, int receiverId, string msg) {
#ifdef DEBUG
    NP::log("sendMsgToClient: (" + to_string(senderId) + " -> " + to_string(receiverId) + ", size = " + to_string(msg.length()) + "):\n" + msg);
#endif

    // check sender & recervier id validity
    if (!isUserIdValid(receiverId) || !isUserIdValid(senderId)) {
        return false;
    }
    
    // write
    NP::writeWrapper(clients[receiverId].sockfd, msg.c_str(), msg.length());
    
    return true;
}

void NP::ClientHandler::broadcastRawMsg(int senderId, string msg) {
#ifdef DEBUG
    NP::log("broadcastRawMsg from " + to_string(senderId) + ":\n" + msg);
#endif
    
    for (int i = 1; i <= MAX_USER; i++) {
        
        if (!isUserIdValid(i)) {
            // there may be invalid id between users,
            // so we cannot simply break if id is invalid
            continue;
        }
        
        if (!sendRawMsgToClient(senderId, i, msg)) {
            NP::err("error when broadcasting from " + to_string(senderId) + " to " + to_string(i) + ": `" + msg + "`");
        }
    }
}

bool NP::ClientHandler::tell(int senderId, int receiverId, string msg) {
#ifdef DEBUG
    NP::log("telling: (" + to_string(senderId) + " -> " + to_string(receiverId) + "):\n" + msg);
#endif
    
    msg = "*** " + clients[senderId].getName() + " told you ***: " + msg + "\n";
    
    return sendRawMsgToClient(senderId, receiverId, msg);
}

void NP::ClientHandler::yell(int senderId, string msg) {
#ifdef DEBUG
    NP::log("yelling from " + to_string(senderId) + ":\n" + msg);
#endif
    
    msg = "*** " + clients[senderId].getName() + " yelled ***: " + msg + "\n";
    
    broadcastRawMsg(senderId, msg);
}

string NP::ClientHandler::who(int callerId) {
    
    string msg = "<ID>\t<nickname>\t<IP/port>\t<indicate me>\n";
    for (int i = 1; i <= MAX_USER; i++) {
        // there may be invalid id between users,
        // so we cannot simply break if id is invalid
        if (!isUserIdValid(i)) {
            continue;
        }
#ifdef DEBUG
        NP::log("who -> this is " + clients[i].print());
#endif
        msg += to_string(i) + "\t" + clients[i].getName() + "\t" + clients[i].getIpRepresentation() + "\t";
        
        if (i == callerId) {
            
            msg += "<-me\n";
            
        } else {
            
            msg += "\n";
        }
    }
    
    return msg;
}

bool NP::ClientHandler::name(int senderId, string newName) {

    for (int i = 1; i <= MAX_USER; i++) {
        
        if (!isUserIdValid(i)) {
            continue;
        }
        
        if (strncmp(clients[i].userName, newName.c_str(), USER_NAME_SIZE) == 0) {
            return false;
        }
    }
    
    strcpy(clients[senderId].userName, newName.c_str());
    return true;
}

bool NP::ClientHandler::isPipeUsed(int pipeNum) {
    return pipeInUseFlag[pipeNum];
}

void NP::ClientHandler::writeToPublicPipe(int interalId, string msg) {
#ifdef DEBUG
    NP::log("writeToPublicPipe: #" + to_string(interalId) + ":\n" + msg);
#endif

    msg = msg.substr(0, 1024);
    public_pipe[interalId] = msg;
    pipeInUseFlag[interalId] = true;
}

string NP::ClientHandler::readFromPublicPipe(int interalId) {
#ifdef DEBUG
    NP::log("readFromPublicPipe: #" + to_string(interalId));
#endif

    pipeInUseFlag[interalId] = false;
    
    return public_pipe[interalId];
}

// @TODO
int NP::ClientHandler::publicPipeIdToInteralId(int id) {
    // shoud range from 1~100
    return id + 'A';
}

