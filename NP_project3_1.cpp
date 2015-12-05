
/**
 *	Main idea:
 *      - Write may fail. Write a wrapper class to handle this.
 *      - Parse query string needs to be handled carefully.
 */

#include <iostream>
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
    
    void err(string domId, string msg);
    
    
    class Client {
        char ip[INET_ADDRSTRLEN];
        int port;
        char file[FILENAME_LENGTH];
        string domId;
        int sockfd;
        
    public:
        Client() {}
        
        Client(int id, const char* iIp, int iPort, const char* iFile);
        
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
        void execute();
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
    int port[CLIENT_MAX_NUMBER];
    char file[CLIENT_MAX_NUMBER][FILENAME_LENGTH];
    cout << data << endl;
    
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
        port[numberOfMachines] = atoi(&token[3]);

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

NP::Client::Client(int id, const char* iIp, int iPort, const char* iFile) {
    
    domId = "m" + to_string(id);
    strncpy(ip, iIp, INET_ADDRSTRLEN);
    port = iPort;
    strncpy(file, iFile, FILENAME_LENGTH);
}

void NP::executeClientPrograms() {
    for (int i = 0; i < numberOfMachines; i++) {
        clients[i].execute();
    }
}

void NP::Client::execute() {
    
    /**
     *	Get server info
     */
    int status;
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if ((status = getaddrinfo(ip, NULL, &hints, &res)) != 0) {
        err(domId, "getaddrinfo: " + string(gai_strerror(status)));
        return;
    }

    /**
     *	Get sockfd
     */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        
        err(domId, "Socket error: " + string(strerror(errno)));
        return;
        
    }
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    this->sockfd = sockfd;
    
    /**
     *	Connect
     */
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        err(domId, "Connect error: " + string(strerror(errno)));
    }
    
    
    freeaddrinfo(res);
}

/// MISC
void NP::err(string domId, string msg) {
    cout << "<script>document.all['" + domId + "'].innerHTML += \"ERROR:" + msg + "\";</script>";
}
