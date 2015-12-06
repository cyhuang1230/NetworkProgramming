
/**
 *	Main idea:
 *      - Write may fail. Write a wrapper class to handle this.
 *      - Parsing query string needs to be handled carefully.
 *      - Since we're client now, doesn't matter which port to use, i.e. no need to `bind` anymore!
 *      - Try using `flag`(bitwise comparison) instead of individually specifying each property in `log` function.
 *      - Print output should be handled(e.g. `<br/>`, `<`, `>`...) since Javascript has different escape characters than C.
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
using namespace std;

#define CLIENT_MAX_NUMBER 5
#define FILENAME_LENGTH 100
#define BUFFER_SIZE 15000
// for fd management
enum fd_status {F_CONNECTING, F_READING, F_WRITING, F_DONE};

// for log flag
#define IS_LOG 1 << 0
#define IS_ERROR 1 << 1

char buffer[BUFFER_SIZE];

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
    
    void resetBuffer() {
        bzero(buffer, BUFFER_SIZE);
    }
    
    void writeWrapper(int sockfd, const char buffer[], size_t size);
    
    string readWrapper(int sockfd);
    
 
    
    class Client {
        char ip[INET_ADDRSTRLEN];
        char port[6];
        char filename[FILENAME_LENGTH];
        string domId;
        int sockfd;
        fd_status sockStatus = F_CONNECTING;
        ifstream file;
        
    public:
        Client() {}
        
        Client(int id, const char* iIp, const char* iPort, const char* iFile);
        
        // assign operator
        Client& operator=(const Client& newClient) {
            strncpy(ip, newClient.ip, INET_ADDRSTRLEN);
            strncpy(port, newClient.port, 6);
            strncpy(filename, newClient.filename, FILENAME_LENGTH);
            domId = newClient.domId;
            sockfd = newClient.sockfd;
            sockStatus = newClient.sockStatus;
            
            // we *logically* only use assign operator when bootstrap
            // therefore, we open file in ifstream
            file = ifstream(filename, ifstream::in);
            
            return *this;
        }
        
        ~Client() {
            close(sockfd);
        }
        
        string getIp() {
            return string(ip);
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
    
    char* token = strtok(data, "&");
    while (token != NULL && numberOfMachines < 5) {
        
        if (strlen(token) <= 3) {
            // first 3 letters must be sth like `h1=`,
            // so if there's info for us to read,
            // `token` must longer than 3 words
            break;
        }
        
        // ip
        strncpy(ip[numberOfMachines], &token[3], 16);
        
        // port
        token = strtok(NULL, "&");
        strncpy(port[numberOfMachines], &token[3], 6);

        // file
        token = strtok(NULL, "&");
        strncpy(file[numberOfMachines], &token[3], 100);

        token = strtok(NULL, "&");
        numberOfMachines++;
    }

    for (int i = 0; i < numberOfMachines; i++) {
        // insert clients
        clients[i] = Client(i, ip[i], port[i], file[i]);
    }
    
    char beforeTableHeader[] =
    "<body bgcolor=#336699>"
    "<font face=\"Courier New\" size=2 color=#FFFF99>"
    "<table width=\"800\" border=\"1\">"
    "<tr>";
    
    string tableHeader;
    for (int i = 0; i < numberOfMachines; i++) {
        tableHeader += "<td>" + clients[i].getIp() + "</td>";
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
            print("ALL", "Client `" + clients[i].getDomId() + "` encounters error. Stop program.", IS_LOG | IS_ERROR);
            return;
        }
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

        for (int i = 0; i < toCheck; i++) {
            
            for (int j = 0; j < numberOfMachines; j++) {
                
                if (clients[j].getSockStatus() == F_CONNECTING &&
                    (FD_ISSET(clients[j].getSockFd(), &rfds) || FD_ISSET(clients[j].getSockFd(), &wfds))) {

                    // error
                    if (getsockopt(clients[j].getSockFd(), SOL_SOCKET, SO_ERROR, &error, &error_len) != 0 ||
                        error != 0) {
                        print(clients[j].getDomId(), "Connection error: " + string(strerror(error)), IS_ERROR);
                        return;
                    }
                    
                    // we have to read prompt first
                    clients[j].setSockStatus(F_READING);
                    
                    // remove from write fd so that we can read it in next select
                    FD_CLR(clients[j].getSockFd(), &ws);
                    
                } else if (clients[j].getSockStatus() == F_READING && FD_ISSET(clients[j].getSockFd(), &rfds)) {
                    
                    string strRead = readWrapper(clients[j].getSockFd());
                    clients[j].print(strRead, 0);
                    clients[j].print(strRead, IS_LOG);
                    
                    // only write when prompt is read
                    if (strRead.find("% ") != string::npos) {

                        // finish reading, write next time.
                        clients[j].setSockStatus(F_WRITING);
                        
                        // remove from read fd
                        FD_CLR(clients[j].getSockFd(), &rs);
                        
                        // put in write fd
                        FD_SET(clients[j].getSockFd(), &ws);
                    }
                    
                } else if (clients[j].getSockStatus() == F_WRITING && FD_ISSET(clients[j].getSockFd(), &wfds)) {
                    
                    // @TODO: read input from file
                    char test[] = "ls\n";
                    writeWrapper(clients[j].getSockFd(), test, strlen(test));
                    
                    // finish writing, read next time.
                    clients[j].setSockStatus(F_READING);
                    
                    // remove from write fd
                    FD_CLR(clients[j].getSockFd(), &ws);
                    
                    // put in read fd
                    FD_SET(clients[j].getSockFd(), &rs);
                }
                
            }   // end for of checking every machine
            
        }   // end for of toCheck
    }
}

/// NP::Client
NP::Client::Client(int id, const char* iIp, const char* iPort, const char* iFile) {
    
    domId = "m" + to_string(id);
    strncpy(ip, iIp, INET_ADDRSTRLEN);
    strncpy(port, iPort, 6);
    strncpy(filename, iFile, FILENAME_LENGTH);
}

bool NP::Client::connect() {
    
    /**
     * Check if file exists
     */
    if (!file.good()) {
        print("File `" + string(filename) + "` cannot be opened.", IS_ERROR);
        return false;
    }
    print("File `" + string(filename) + "` opened! Start connecting...", IS_LOG);
    
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

/// MISC
void NP::print(string domId, string msg, int flag) {
    
    if (flag & IS_LOG) {
        cout << "<script>document.all['log'].innerHTML += \"[" + domId + "] ";
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
    
    // replace `\n` with `<br/>
    found = msg.find("\n");
    while (found != string::npos) {
        msg.replace(found, 1, "<br/>");
        found = msg.find("\n");
    }
    
    cout << msg + "<br/>\";</script>";
}

void NP::writeWrapper(int sockfd, const char buffer[], size_t size) {
    
    resetBuffer();
    int bytesWritten = 0;
    int bytesToWrite = strlen(buffer);
    string domId = "sockfd " + to_string(sockfd);
    print(domId, "write(expected size = " + to_string(size) + ", via fd " + to_string(sockfd) +"):\n" + string(buffer), IS_LOG);

    // in case write doesn't successful in one time
    while (bytesWritten < bytesToWrite) {
        
        int n = write(sockfd, buffer + bytesWritten, size - bytesWritten);
        
        print(domId, "  written(n = " + to_string(n) + ", total: " + to_string(n+bytesWritten) + "/" + to_string(bytesToWrite) +")", IS_LOG);
        
        if (n < 0) {
            print(domId, "write error: " + string(buffer), IS_LOG | IS_ERROR);
        }
        
        bytesWritten += n;
    }
}

string NP::readWrapper(int sockfd) {
    
    resetBuffer();
    
    string domId = "sockfd " + to_string(sockfd);

    int n = read(sockfd, buffer, sizeof(buffer));

    if (n < 0) {
        
        print(domId, "read error: " + string(buffer), IS_LOG | IS_ERROR);

    } else if (n == 0) {
    
        return string();
    }

    print(domId, "read(size = " + to_string(sizeof(buffer)) + ", n = " + to_string(n) + ", via fd " + to_string(sockfd) +"):\n" + string(buffer), IS_LOG);

    return string(buffer);
}