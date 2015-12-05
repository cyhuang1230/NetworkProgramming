#include <iostream>
#include <cstring>
using namespace std;

namespace NP {

    void printHeader();
    
    void printFooter();
    
    void printBody();
}

using namespace NP;

int main() {
    
    printHeader();
    printBody();
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
    "</html>";

    cout << footer;
}

void NP::printBody() {
    
    char* data = getenv("QUERY_STRING");
    char ip[5][16];
    int port[5];
    char file[5][100];
    cout << data << endl;
    int numberOfMachines = 0;
    
    char* token = strtok(data, "&");
    while (token != NULL && numberOfMachines < 5) {
        cout << "DOING " << numberOfMachines << endl;
        // ip
        strncpy(ip[numberOfMachines], &token[3], 16);
        
        // port
        token = strtok(NULL, "&");
        port[numberOfMachines] = atoi(&token[3]);

        // file
        token = strtok(NULL, "&");
        strncpy(file[numberOfMachines], &token[3], 100);
        cout << ip[numberOfMachines] << ":" <<  port[numberOfMachines] << " @ " <<  file[numberOfMachines] << endl;

        token = strtok(NULL, "&");
        numberOfMachines++;
    }

    cout << "numberOfMachines" << numberOfMachines << endl;
    for (int i = 0; i < 5; i++) {
        cout << ip[i] << ":" <<  port[i] << " @ " <<  file[i] << endl;
    }
    
    char content[] =
    "<body bgcolor=#336699>"
    "<font face=\"Courier New\" size=2 color=#FFFF99>"
    "<table width=\"800\" border=\"1\">"
    "<tr>"
    "<td>140.113.210.145</td><td>140.113.210.145</td><td>140.113.210.145</td></tr>"
    "<tr>"
    "<td valign=\"top\" id=\"m0\"></td><td valign=\"top\" id=\"m1\"></td><td valign=\"top\" id=\"m2\"></td></tr>"
    "</table>"
    "<script>document.all['m0'].innerHTML += \"****************************************<br>\";</script>"
    "<script>document.all['m0'].innerHTML += \"** Welcome to the information server. **<br>\";</script>"
    "<script>document.all['m0'].innerHTML += \"****************************************<br>\";</script>"
    "<script>document.all['m1'].innerHTML += \"****************************************<br>\";</script>"
    "</font>"
    "</body>";
    
//    cout << content << endl;;
    
    
}