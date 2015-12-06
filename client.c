#include "client.h"

/*gets address of connection for a nice print*/
void *get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*Thread function to handle all command sends*/
void *send_handler(void * argp){
    char obuf[MAXDATASIZE];
    int numbytes;
    int sockfd;
    sockfd = *(int*)argp;
    while(1){
        numbytes = read(STDIN_FILENO, obuf, 200);
        send(sockfd, obuf, numbytes, 0);
        sleep(2);
    }
}

/* Attempts to connect to PORT at the given hostname. Returns socket descriptor. Retry on failure.*/ 
int connector(char* hostname){
    char s[INET6_ADDRSTRLEN];
    struct addrinfo hints, *servinfo, *p;
    int rv, sockfd;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    while(1){
        if ((rv = getaddrinfo(hostname, PORT, &hints, &servinfo)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
            return 1;
        }

        // loop through all the results and connect to the first we can
        for(p = servinfo; p != NULL; p = p->ai_next) {
            if ((sockfd = socket(p->ai_family, p->ai_socktype,
                    p->ai_protocol)) == -1) {
                perror("client: socket");
                continue;
            }
            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                close(sockfd);
                // perror("client: connect");
                continue;
            }
            break;
        }

        if (p == NULL) {
            fprintf(stderr, "Failed to connect trying again in 3 seconds\n");
            sleep(3);
            continue;
        }

        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
        break;
    }
    freeaddrinfo(servinfo); // all done with this structure

    printf("client: connecting to %s\n", s);
    return sockfd;
}

int main(int argc, char** argv)
{
    int sockfd, numbytes;  
    char ibuf[MAXDATASIZE];
    pthread_t tid;

    sockfd = connector(argv[1]);
    
    numbytes = recv(sockfd, ibuf, MAXDATASIZE-1, 0);
    ibuf[numbytes] = '\0';
    printf("%s", ibuf);
    pthread_create(&tid, NULL, send_handler, (void*)&sockfd);
    while(numbytes > 0){
        numbytes = recv(sockfd, ibuf, MAXDATASIZE-1, 0);
        ibuf[numbytes] = '\0';
        printf("%s", ibuf);
    }

    pthread_kill(tid, SIGKILL);
    pthread_join(tid, NULL);
    close(sockfd);
    return 0;
}
