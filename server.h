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
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef SERVER_H
#define SERVER_H

#define PORT "3490"  // the port users will be connecting to
#define BACKLOG 10     // how many pending connections queue will hold
#define MAXDATASIZE 100

typedef struct bank_account{
    char name[101];
    float balance;
    unsigned int session_flag;
    unsigned int valid;
    sem_t lock;
}account;

typedef struct my_bank {
    account vault[20];
    int size;
    sem_t lock;
}bank;

int safe_open(char* file);

void sigchld_handler(int s);

void setup_child_killer();

int bind_and_listen(char* port);

void print_startmenu(int fd);

void print_account_menu(int fd);

void print_bank(bank* the_bank);

void account_controller(int current, int fd, bank* the_bank);

void open_account(char* command, int fd, bank* the_bank);

void start(char* command, int fd, bank* the_bank);

void exit_session(int fd, bank* the_bank);

int get_command_id(char* command);

int process_account_command(char* command, int fd, bank* the_bank, int current);

void process_bank_command(char* command, int fd, bank* the_bank);

bank* bank_setup(int fd);

#endif