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

char **command_list = (char*[]){"open", "start", "credit", "debit", "balance", "finish", "exit"};

int get_command_id(char* command){
    int i;
    for(i = 0; i < 7; i++){
        if(strcmp(command_list[i], command) == 0) 
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

//Sets up the signal handler that cleans up the dead children. Need to add some print statements to this
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

void account_controller(int current, int fd, bank* the_bank){
    char buff[200];
    int numbytes;
    int sval;

    sem_getvalue(&the_bank->vault[current].lock, &sval);
    if(sval == 0){
        printf("Account in session. Waiting for current customer to exit...\n");
    }
    sem_wait(&the_bank->vault[current].lock);


    print_account_menu(fd);
    the_bank->vault[current].session_flag = 1;
    while((numbytes = recv(fd, buff, MAXDATASIZE-1, 0)) > 0){
        buff[numbytes-1] = '\0';
        printf("Command recv '%s' \n",buff);
        if(process_account_command(buff, fd, the_bank, current)) break;
        print_account_menu(fd);
    }
    the_bank->vault[current].session_flag = 0;
    sem_post(&the_bank->vault[current].lock);
}

//TODO change the prints to sends to the client
void open_account(char* command, int fd, bank* the_bank){
    char name[101];
    char extra[10];
    int i;

    sem_wait(&the_bank->lock);
    if(the_bank->size == 20){
        printf("The bank is full.  Cannot open a new account");
        return;
    }
    sscanf(command, "%s %s", extra, name);
    for(i = 0; i < 20; i++){
        if(strcmp(the_bank->vault[i].name,name)==0){
            printf("Account already exitsts\n");
            return;
        }
    }
    for (i = 0; i<20; i++){
        if(!(the_bank->vault[i].valid)) break;
    }
    strcpy(the_bank->vault[i].name, name);
    the_bank->vault[i].valid = 1;
    the_bank->size++;
    sem_post(&the_bank->lock);
    return;
}

//Starts an account session for the given account name if it exists
void start(char* command, int fd, bank* the_bank){
    char extra[10], name[101];
    int i;
    sscanf(command, "%s %s", extra, name);

    sem_wait(&the_bank->lock);
    for(i = 0; i < 20; i++){
        if(strcmp(the_bank->vault[i].name, name) == 0){
            sem_post(&the_bank->lock);
            account_controller(i, fd, the_bank);
            break;
        }
    }
    if(i == 20){
      printf("Account named does not exist\n");
      sem_post(&the_bank->lock);
    } 
}

void exit_session(int fd, bank* the_bank){
    printf("Session terminated.\n");
    munmap(the_bank, sizeof(bank));
    close(fd);
    exit(0);
}

int process_account_command(char* command, int fd, bank* the_bank, int current){
    char command_head[50], extra[10];
    float money;
    sscanf(command, "%s", command_head);
    switch(get_command_id(command_head)) {
        case 2: //credit
            sscanf(command, "%s %f", extra, &money);
            (the_bank->vault[current].balance) += money;
            printf("Credit of $%f applied to current account\n", money);
            return 0;
        case 3: //debit
            sscanf(command, "%s %f", extra, &money);
            if(money > the_bank->vault[current].balance){
                printf("Insufficient funds\n");
                return 0;
            }
            (the_bank->vault[current].balance) -= money;
            printf("Debit of $%f applied to current account\n", money);            
            return 0;
        case 4: //balance
            printf("Balance of current account is $%f\n", the_bank->vault[current].balance);            
            return 0;
        case 5: //finish
            printf("Account closed.\n");;
            return 1;
        default:
            printf("Command invalid, please enter again\n");         
    }
}

void process_bank_command(char* command, int fd, bank* the_bank){
    char command_head[50];
    sscanf(command, "%s", command_head);
    switch(get_command_id(command_head)) {
        case  0:
            open_account(command, fd, the_bank);
            break;
        case 1:
            start(command, fd, the_bank);
            break;
        case 6:
            exit_session(fd, the_bank);
            break;
        default:
            printf("Command invalid, please enter again\n");
            break;         
    }       
}

int safe_open(char* file){
    int fd;
    if((fd = open(file, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH)) == -1){
        perror("file");
        exit(1);
    }
    return fd;
}

//Mmaps a bank account given an open file descriptor to the file.
/*Currently clears the file every time. Need to find a way to determine if the file exists to avoid the strech and clear  */ 
bank* bank_setup(int fd){
    bank* the_bank;
    account acct;
    int i, result;

    result = lseek(fd, sizeof(bank)-1, SEEK_SET);
    if (result == -1) {
        close(fd);
        perror("Error calling lseek() to 'stretch' the file");
        exit(EXIT_FAILURE);
    }

    result = write(fd, "", 1);
    if (result != 1) {
        close(fd);
        perror("Error writing last byte of the file");
        exit(EXIT_FAILURE);
    }
    if((the_bank = (bank*)mmap(0, sizeof(bank), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED){
        printf("Mmap to shared file failed. EXITING...\n");
        exit(1);
    }
    memset(the_bank, 0, sizeof(bank));
    the_bank->size = 0;
    if(sem_init(&the_bank->lock, 1, 1) == -1){
        printf("bank lock is broken money unsafe. EXITING\n");
        exit(1);
    }
    for(i = 0; i <20; i++){
        if(sem_init(&(the_bank->vault[i]).lock, 1, 1) == -1){
            printf("account %d lock comprimised. Money unsafe\n", i);
            exit(1);
        } 
    }
    return the_bank;
}

int main(void){
    char buff[100];
    struct sockaddr_storage their_addr; // connector's address information
    int sockfd, new_fd, numbytes;  // listen on sock_fd, new connection on new_fd
    socklen_t sin_size;
    int shared_file;

    sockfd = bind_and_listen(PORT);
    setup_child_killer();
    printf("server: waiting for connections...\n");

    bank* the_bank;

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        printf("Client connected\n");
        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener
            shared_file = safe_open("vault");
            the_bank = bank_setup(shared_file);
            print_startmenu(new_fd);
            while((numbytes = recv(new_fd, buff, MAXDATASIZE-1, 0)) > 0) { 
                buff[numbytes-1] = '\0';
                printf("Command recv '%s' \n",buff);
                process_bank_command(buff, new_fd, the_bank);
                print_startmenu(new_fd);
            }

            close(new_fd);
            exit(0);
        }
        close(new_fd);  // parent doesn't need this
    }

    return 0;
}