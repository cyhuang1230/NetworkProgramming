
/**
 *	Main idea:
 *      - Write may fail. Write a wrapper class to handle this.
 *      - Parsing query string needs to be handled carefully.
 */

#include <iostream>
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

namespace NP {

    class Client;
    
    void printHeader();
    
    void printFooter();
    
    void printBody();
    
    void executeClientPrograms();
    
    /**
     *  Misc
     */
    
    void print(string domId, string msg, bool isError = false);
    
    
    class Client {
        char ip[INET_ADDRSTRLEN];
        char port[6];
        char file[FILENAME_LENGTH];
        string domId;
        int sockfd;
        
    public:
        Client() {}
        
        Client(int id, const char* iIp, const char* iPort, const char* iFile);
        
        ~Client() {
            close(sockfd);
        }
        
        string getIp() {
            return string(ip);
        }
        
        string getDomId() {
            return domId;
        }
        
        /**
         *	Execute program and constantly produce output
         */
        bool connect();
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
    "<span id=\"error\"></span>"
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
    "</table>";
//    "<script>document.all['m1'].innerHTML += \"****************************************<br>\";</script>";
    
    cout << beforeTableHeader << tableHeader << tableData << afterTableData;
}

NP::Client::Client(int id, const char* iIp, const char* iPort, const char* iFile) {
    
    domId = "m" + to_string(id);
    strncpy(ip, iIp, INET_ADDRSTRLEN);
    strncpy(port, iPort, 6);
    strncpy(file, iFile, FILENAME_LENGTH);
}

void NP::executeClientPrograms() {
    for (int i = 0; i < numberOfMachines; i++) {
        clients[i].connect();
    }
}

bool NP::Client::connect() {
    
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
        print(domId, "getaddrinfo: " + string(gai_strerror(status)));
        return false;
    }

    /**
     *	Get sockfd
     */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        
        print(domId, "Socket error: " + string(strerror(errno)));
        return false;
    }
    // set to nonblocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    this->sockfd = sockfd;
    
    /**
     *	Connect
     */
    if (::connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        print(domId, "Connect error: " + string(strerror(errno)));
    }
    
    freeaddrinfo(res);
    return true;
}

/// MISC
void NP::print(string domId, string msg, bool isError) {
    
    cout << "<script>document.all['" + domId + "'].innerHTML += \"";
    
    if (isError) {
        cout << "ERROR:";
    }
    
    cout << msg + "\";</script>";
}
