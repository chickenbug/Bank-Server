/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#define PORT "3490"  // the port users will be connecting to
#define BACKLOG 10     // how many pending connections queue will hold
#define MAXDATASIZE 100

typedef struct bank_account{
    char name[101];
    float balance;
    unsigned int session_flag;
    unsigned int valid;
}account;

const char** command_list = {
    "open",
    "start",
    "credit",
    "debit",
    "balance",
    "finish"
    "exit"
}

int get_command_id(char* command){
    int i;
    for(i = 0; i < 7; i++){
        if(strcmp(command_list[i], command) = 0) 
            return i;
    }
    return 7;
}



void print_startmenu(int fd){
    send(fd, "Enter a command listed below\n1. open (followed by accountname)\n2. start (followed by accountname)\n3. exit (to close session)\n",124, 0);
}

void print_account_menu(int fd){
    send(fd, "Enter a command listed below\n1. credit (followed by amount)\n2. debit (followed by amount)\n3. finish (when finished with this account)\n",134, 0);
}

void sigchld_handler(int s){
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

void setup_child_killer(){
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}

//setups the program to listen to the port provided and returns the discriptor interger
int bind_and_listen(char* port){
    int sockfd;
    struct addrinfo hints, *servinfo, *p;  
    int yes = 1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    return sockfd;
}

void process_command(char* command){
    char command_head[50];
    sscans(buff, "%s", command_head);
    switch(get_command_id(command_head)) {
        case  1:
            open(command);
            break;
        case 2:
            start(command); 
        case 6:
            exit_session();
        default:         
    }
}

int main(void){
    char buff[100];
    struct sockaddr_storage their_addr; // connector's address information
    int sockfd, new_fd, numbytes;  // listen on sock_fd, new connection on new_fd
    socklen_t sin_size;

    sockfd = bind_and_listen(PORT);
    setup_child_killer();
    printf("server: waiting for connections...\n");

    account* bank;
    memset(&bank, 0, sizeof(bank));

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener

            while(1){
                print_startmenu(new_fd);
                numbytes = recv(new_fd, buff, MAXDATASIZE-1, 0);
                buff[numbytes] = '\0';
                printf("Command recv '%s' \n",buff);
                process_command(buff);
            }

            close(new_fd);
            exit(0);
        }
        close(new_fd);  // parent doesn't need this
    }

    return 0;
}