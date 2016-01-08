//  NCTU CS. Network Programming Assignment 4 CGI supprting SOCKS4 proxy
//  CGI. Please refer to hw3spec.txt for more details.

//  Code by Denny Chien-Yu Huang, 12/31/15.
//  Github: http://cyhuang1230.github.io/

/**
 *	Main idea:
 *      - Same as HW3. Just need to send SOCK4 requset and handle SOCK4 reply.
 */

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>
using namespace std;

#define CLIENT_MAX_NUMBER 5
#define FILENAME_LENGTH 100
#define BUFFER_SIZE 15000
// for fd management
enum fd_status {F_CONNECTING, F_READING, F_WRITING, F_DONE};

// for log flag
#define IS_LOG (1 << 0)
#define IS_ERROR (1 << 1)
#define NEED_NEWLINE (1 << 2)
#define NEED_BOLD (1<<3)
char buffer[BUFFER_SIZE];

// for log time
time_t start, now;

namespace NP {

    class Client;
    
    void printHeader();
    
    void printFooter();
    
    void printBody();
    
    void executeClientPrograms();
    
    /**
     *  Misc
     */
    void print(string domId, string msg, int flag = 0);
    
    void printLog(string msg) {
        print("ALL", msg, IS_LOG | NEED_NEWLINE);
    }
    
    void resetBuffer() {
        bzero(buffer, BUFFER_SIZE);
    }
    
    void writeWrapper(int sockfd, const char buffer[], size_t size);
    
    int readWrapper(int sockfd);
    
    class Client {
        char ip[INET_ADDRSTRLEN];
        char port[6];
        char filename[FILENAME_LENGTH];
        char socksip[INET_ADDRSTRLEN];
        char socksport[6];
        string domId;
        int sockfd;
        fd_status sockStatus = F_CONNECTING;
        ifstream file;
        bool sentExit = false;
        bool sentSocksReqFlag = false;
        bool handledSocksRespFlag = false;
        
    public:
        Client() {}
        
        // assign operator
        void set(int id, const char* iIp, const char* iPort, const char* iFile, const char* iSocksip, const char* iSocksport) {
            
            domId = "m" + to_string(id);
            strncpy(ip, iIp, INET_ADDRSTRLEN);
            strncpy(port, iPort, 6);
            strncpy(filename, iFile, FILENAME_LENGTH);
            strncpy(socksip, iSocksip, INET_ADDRSTRLEN);
            strncpy(socksport, iSocksport, 6);

            // we *logically* only use assign operator when bootstrap
            // therefore, we open file in ifstream
            file = ifstream(filename, ifstream::in);
        }
        
        ~Client() {
            close(sockfd);
        }
        
        string getFileName() {
            return string(filename);
        }
        
        string getIp() {
            return string(ip);
        }
        
        string getPort() {
            return string(port);
        }
        
        string getSocksIp() {
            return string(socksip);
        }
        
        string getSocksPort() {
            return string(socksport);
        }
        
        string getDomId() {
            return domId;
        }
        
        int getSockFd() {
            return sockfd;
        }
        
        fd_status getSockStatus() {
            return sockStatus;
        }
        
        void setSockStatus(fd_status newStatus) {
            sockStatus = newStatus;
        }
        
        ifstream& getFile() {
            return file;
        }
        
        bool hasSentExit() {
            return sentExit;
        }
        
        void setExitStatus(bool status) {
            sentExit = status;
        }
        
        bool hasSentSocksReq() {
            return sentSocksReqFlag;
        }
        
        void sentSockReq();
        
        bool hasHandledSocksResp() {
            return handledSocksRespFlag;
        }
        
        void setHandledSocksFlag() {
            handledSocksRespFlag = true;
        }
        
        /**
         *	Execute program and constantly produce output
         */
        bool connect();
        
        bool setFile(char name[FILENAME_LENGTH]);
        
        char* readFile();
        
        void print(string msg, int flag = 0) {
            NP::print(domId, msg, flag);
        }
    };
}

using namespace NP;

Client clients[CLIENT_MAX_NUMBER];
int numberOfMachines = 0;

int main() {
    
    time(&start);
    
    printHeader();
    
    printBody();
    
    executeClientPrograms();
    
    printFooter();
    
    return 0;
}

void NP::printHeader() {
    
    char header[] =
    "Content-Type: text/html\n\n"
    "<html>"
    "<head>"
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />"
    "<title>Network Programming Homework 3</title>"
    "</head>";
    
    cout << header;
}

void NP::printFooter() {
    
    char footer[] =
    "</font>"
    "</body>"
    "</html>";

    cout << footer;
}

void NP::printBody() {

    char* data = getenv("QUERY_STRING");
    char ip[CLIENT_MAX_NUMBER][INET_ADDRSTRLEN];
    char port[CLIENT_MAX_NUMBER][6];
    char file[CLIENT_MAX_NUMBER][FILENAME_LENGTH];
    char socksip[CLIENT_MAX_NUMBER][INET_ADDRSTRLEN];
    char socksport[CLIENT_MAX_NUMBER][6];

    char* token = strtok(data, "&");
    while (token != NULL && numberOfMachines < 5) {
        
        if (strlen(token) <= 3) {
            // first 3 letters must be sth like `h1=`,
            // so if there's info for us to read,
            // `token` must be longer than 3 words
            break;
        }
        
        // ip
        strncpy(ip[numberOfMachines], &token[3], INET_ADDRSTRLEN);
        
        // port
        token = strtok(NULL, "&");
        strncpy(port[numberOfMachines], &token[3], 6);

        // file
        token = strtok(NULL, "&");
        strncpy(file[numberOfMachines], &token[3], FILENAME_LENGTH);

        // socks host
        token = strtok(NULL, "&");
        strncpy(socksip[numberOfMachines], &token[4], INET_ADDRSTRLEN);
        
        // socks port
        token = strtok(NULL, "&");
        strncpy(socksport[numberOfMachines], &token[4], 6);
        
        token = strtok(NULL, "&");
        numberOfMachines++;
    }

    for (int i = 0; i < numberOfMachines; i++) {
        // insert clients
        clients[i].set(i, socksip[i], socksport[i], file[i], ip[i], port[i]);
    }

    char beforeTableHeader[] =
    "<body bgcolor=#336699>"
    "<font face=\"Courier New\" size=2 color=#FFFF99>"
    "<table width=\"800\" border=\"1\">"
    "<tr>";
    
    string tableHeader;
    for (int i = 0; i < numberOfMachines; i++) {
        tableHeader += "<td>" + clients[i].getFileName() + " @ " + clients[i].getSocksIp() + ":" + clients[i].getSocksPort() +
            " via " + clients[i].getIp() + ":" + clients[i].getPort() + "</td>";
    }
    
    string tableData = "</tr><tr>";
    for (int i = 0; i < numberOfMachines; i++) {
        tableData += "<td valign=\"top\" id=\"" + clients[i].getDomId() + "\"></td>";
    }
    
    char afterTableData[] =
    "</tr>"
    "</table>"
    "<span id=\"log\"style=\"color: azure;\"></span>";
    
    cout << beforeTableHeader << tableHeader << tableData << afterTableData;
}

void NP::executeClientPrograms() {
    
    /**
     *	Connect
     */
    for (int i = 0; i < numberOfMachines; i++) {
        if (!clients[i].connect()) {
            print("ALL", "Client `" + clients[i].getDomId() + "` encounters error. Stop program.", IS_LOG | IS_ERROR | NEED_NEWLINE);
            return;
        }
        // print intro
        clients[i].print("Client id #" + to_string(i) + ": sockfd: " + to_string(clients[i].getSockFd()), IS_LOG | NEED_NEWLINE);
    }
    
    // fd related
    int connections = numberOfMachines;
    int nfds = FD_SETSIZE;
    fd_set rfds;    // readable fds used in select
    fd_set wfds;    // writable fds used in select
    fd_set rs;      // active read fds
    fd_set ws;      // actvie write fds
    
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&rs);
    FD_ZERO(&ws);
    
    for (int i = 0; i < numberOfMachines; i++) {
        FD_SET(clients[i].getSockFd(), &rs);
        FD_SET(clients[i].getSockFd(), &ws);
    }
    
    /**
     *  Handling connections
     */
    int toCheck, error = 0;
    socklen_t error_len = sizeof(error);
    
    while (connections > 0) {
        
        memcpy(&rfds, &rs, sizeof(rfds));
        memcpy(&wfds, &ws, sizeof(wfds));
        
        toCheck = select(nfds, &rfds, &wfds, NULL, 0);
        
        for (int j = 0; j < numberOfMachines; j++) {
            
            if (clients[j].getSockStatus() == F_CONNECTING &&
                (FD_ISSET(clients[j].getSockFd(), &rfds) || FD_ISSET(clients[j].getSockFd(), &wfds))) {
                
                // error
                if (getsockopt(clients[j].getSockFd(), SOL_SOCKET, SO_ERROR, &error, &error_len) != 0 ||
                    error != 0) {
                    print(clients[j].getDomId(), "Connection error: " + string(strerror(error)), IS_ERROR);
                    return;
                }
                
                // we have to sent socks request first
                clients[j].setSockStatus(F_WRITING);
                
                // remove from read fd so that we can write it in next select
                FD_CLR(clients[j].getSockFd(), &rs);
                
            } else if (clients[j].getSockStatus() == F_READING && FD_ISSET(clients[j].getSockFd(), &rfds)) {
                
                string strRead;
                
                if (!clients[j].hasHandledSocksResp()) {
                    
                    int n = NP::readWrapper(clients[j].getSockFd());
                
                    if (buffer[0] != 0 || buffer[1] != 0x5A) {
                        
                        clients[j].print("[SOCKS Server] Connection refused (" +to_string(buffer[0]) + "," + to_string(buffer[1]) + ")\n");
                        
                        FD_CLR(clients[j].getSockFd(), &rs);
                        clients[j].setSockStatus(F_DONE);
                        connections--;
                    }
                    
                    clients[j].setHandledSocksFlag();
                    
                    if (n > 8) {
                        strRead = string(&buffer[8]);
                    }
                    
                } else {
                    
                    readWrapper(clients[j].getSockFd());
                    strRead = string(buffer);
                }
                
                if (strRead.empty()) {
                    continue;
                }
                
                // write to webpage
                clients[j].print(strRead);
                clients[j].print(strRead, IS_LOG | NEED_NEWLINE);  // log
                
                if (strRead.find("% ") != string::npos) {
                    // only write when prompt is read
                    
                    // finish reading, write next time.
                    clients[j].setSockStatus(F_WRITING);
                    
                    // remove from read fd
                    FD_CLR(clients[j].getSockFd(), &rs);
                    
                    // put in write fd
                    FD_SET(clients[j].getSockFd(), &ws);
                    
                } else if (clients[j].hasSentExit()) {
                    
                    // if the client is ready to exit
                    // remove from read fd and set status to F_DONE
                    FD_CLR(clients[j].getSockFd(), &rs);
                    
                    clients[j].setSockStatus(F_DONE);
                    
                    connections--;
                }
            
            } else if (clients[j].getSockStatus() == F_WRITING && FD_ISSET(clients[j].getSockFd(), &wfds)) {
                
                if (!clients[j].hasSentSocksReq()) {
                    
                    // need to send SOCKS request first
                    clients[j].sentSockReq();
                    
                } else {
                    
                    // get input from file
                    string input;
                    getline(clients[j].getFile(), input);
                    input += "\n";  // indicate EOL
                    
                    // if exit is sent, exit after next write
                    if (input.find("exit") == 0) {
                        clients[j].setExitStatus(true);
                    }
                    
                    // write to webpage
                    clients[j].print(input, NEED_BOLD);
                    clients[j].print(input, IS_LOG | NEED_NEWLINE);    // log
                    
                    // write to server
                    writeWrapper(clients[j].getSockFd(), input.c_str(), input.size());
                }
                
                // finish writing, read next time.
                clients[j].setSockStatus(F_READING);
                
                // remove from write fd
                FD_CLR(clients[j].getSockFd(), &ws);
                
                // put in read fd
                FD_SET(clients[j].getSockFd(), &rs);
            }

            if (--toCheck >= 0) {    // continue if still have availabe socket
                continue;
            }
            
        }   // end for of checking every machine
        
    }
}

/// NP::Client
bool NP::Client::connect() {
    
    /**
     * Check if file exists
     */
    if (!file.good()) {
        print("File `" + string(filename) + "` cannot be opened.", IS_ERROR);
        return false;
    }
    
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
    
    if ((status = getaddrinfo(ip, port, &hints, &res)) != 0) {
        print("getaddrinfo: " + string(gai_strerror(status)), IS_ERROR);
        return false;
    }

    /**
     *	Get sockfd
     */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        print("Socket error: " + string(strerror(errno)), IS_ERROR);
        return false;
    }
    // set to nonblocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    this->sockfd = sockfd;
    
    /**
     *	Connect
     */
    if (::connect(sockfd, res->ai_addr, res->ai_addrlen) == -1 && errno != EINPROGRESS) {
        print("Connect error: " + string(strerror(errno)), IS_ERROR);
        return false;
    }
    
    freeaddrinfo(res);
    return true;
}

void NP::Client::sentSockReq() {

    char socksreq[9];
    unsigned short ip[4];
    
    socksreq[0] = 4;
    socksreq[1] = 1;
    *((unsigned short*)&socksreq[2]) = htons(atoi(this->socksport));
    
    if(4 == sscanf(this->socksip, "%hu.%hu.%hu.%hu", &ip[0], &ip[1], &ip[2], &ip[3])) {

        socksreq[4] = (unsigned char)ip[0];
        socksreq[5] = (unsigned char)ip[1];
        socksreq[6] = (unsigned char)ip[2];
        socksreq[7] = (unsigned char)ip[3];
    }
    socksreq[8] = '\0';
    
    NP::writeWrapper(this->sockfd, socksreq, 9);
    
    this->sentSocksReqFlag = true;
}


/// MISC
void NP::print(string domId, string msg, int flag) {
    
    if (flag & IS_LOG) {
        time(&now);
        cout << "<script>document.all['log'].innerHTML += \"[" + domId + ", " + to_string(difftime(now, start)) + "s] ";
    } else {
        cout << "<script>document.all['" + domId + "'].innerHTML += \"";
    }
    
    if (flag & IS_ERROR) {
        cout << "<span style=\'color: red; font-weight:700;\'>ERROR:</span> ";
    }
    
    /**
     *	Process msg
     */
    size_t found;
    
    // replace `&` with `&amp;`
    found = msg.find("&");
    while (found != string::npos) {
        msg.replace(found, 1, "&amp;");
        found = msg.find("&");
    }
    
    // replace `"` with `&quot;`
    found = msg.find("\"");
    while (found != string::npos) {
        msg.replace(found, 1, "&quot;");
        found = msg.find("\"");
    }
    
    // replace `'` with `&apos;`
    found = msg.find("'");
    while (found != string::npos) {
        msg.replace(found, 1, "&apos;");
        found = msg.find("'");
    }
    
    // replace ` ` with `&nbsp;`
    found = msg.find(" ");
    while (found != string::npos) {
        msg.replace(found, 1, "&nbsp;");
        found = msg.find(" ");
    }
    
    // replace `<` with `&lt;`
    found = msg.find("<");
    while (found != string::npos) {
        msg.replace(found, 1, "&lt;");
        found = msg.find("<");
    }
    
    // replace `>` with `&gt;`
    found = msg.find(">");
    while (found != string::npos) {
        msg.replace(found, 1, "&gt;");
        found = msg.find(">");
    }
    
    // replace `\n` with `<br/>`
    found = msg.find("\n");
    while (found != string::npos) {
        msg.replace(found, 1, "<br/>");
        found = msg.find("\n");
    }
    
    // remove `\r`
    found = msg.find("\r");
    while (found != string::npos) {
        msg.replace(found, 1, "");
        found = msg.find("\r");
    }
    
    if (flag & NEED_BOLD) {
        msg = "<b>" + msg + "</b>";
    }
    
    cout << msg;
    
    if (flag & NEED_NEWLINE) {
        cout << "<br/>";
    }
    
    cout << "\";</script>\n";
    cout.flush();
}

void NP::writeWrapper(int sockfd, const char buffer[], size_t size) {
    
    resetBuffer();
    int bytesWritten = 0;
    int bytesToWrite = strlen(buffer);
    string domId = "sockfd " + to_string(sockfd);
    print(domId, "write(expected size = " + to_string(size) + ", via fd " + to_string(sockfd) +"):\n" + string(buffer), IS_LOG | NEED_NEWLINE);

    // in case write doesn't successful in one time
    while (bytesWritten < bytesToWrite) {
        
        int n = write(sockfd, buffer + bytesWritten, size - bytesWritten);
        
        print(domId, "  written(n = " + to_string(n) + ", total: " + to_string(n+bytesWritten) + "/" + to_string(bytesToWrite) +")", IS_LOG | NEED_NEWLINE);
        
        if (n < 0) {
            print(domId, "write error: " + string(buffer), IS_LOG | IS_ERROR | NEED_NEWLINE);
        }
        
        bytesWritten += n;
    }
}

int NP::readWrapper(int sockfd) {
    
    resetBuffer();
    
    string domId = "sockfd " + to_string(sockfd);

    int n = read(sockfd, buffer, sizeof(buffer));

    if (n < 0) {
        
        print(domId, "read error: " + string(buffer), IS_LOG | IS_ERROR | NEED_NEWLINE);
        return -1;
        
    } else if (n == 0) {
    
        return 0;
    }
    
    print(domId, "read(size = " + to_string(sizeof(buffer)) + ", n = " + to_string(n) + ", via fd " + to_string(sockfd) +"):\n" + string(buffer), IS_LOG | NEED_NEWLINE);
    
    return n;
}