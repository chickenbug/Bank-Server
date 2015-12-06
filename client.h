#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <arpa/inet.h>

#ifndef CLIENT_H
#define CLIENT_H

#define PORT "3490" // the port client will be connecting to 
#define MAXDATASIZE 10000 // max number of bytes we can get at once

void *get_in_addr(struct sockaddr *sa);

void *send_handler(void * argp);

int connector(char* hostname);

#endif