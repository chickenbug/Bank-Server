#include "server.h"

// open file with error checks
int safe_open(char* file){
    int fd;
    if((fd = open(file, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH)) == -1){
        perror("file");
        exit(1);
    }
    return fd;
}

// cleans up children on SIG_CHLD signal
void sigchld_handler(int s){
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
    printf("Client disconnected\n");
    printf("Cleaned up child <%d>\n", s);
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

//sets up the program to listen to the port provided and returns the discriptor interger
int bind_and_listen(char* port){
    int sockfd;
    struct addrinfo hints, *servinfo, *p;  
    int yes = 1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
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

    freeaddrinfo(servinfo);
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

/*sends menu to client for bank session*/
void print_startmenu(int fd){
    char* start_menu = "\nEnter a command listed below\n1. open (followed by accountname)\n2. start (followed by accountname)\n3. exit (to close session)\n";
    send(fd, start_menu, strlen(start_menu), 0);
}

/*sends menu to client for account session*/
void print_account_menu(int fd){
    char* account_menu = "\nEnter a command listed below\n1. credit (followed by amount)\n2. debit (followed by amount)\n3. balance (displays current balance)\n4. finish (when finished with this account)\n";
    send(fd, account_menu, strlen(account_menu), 0);
}

/* Locks the bank and prints all accounts with their balances or "IN SESSION" as appropriate*/
void print_bank(bank* the_bank){
    int i;
    sem_wait(&the_bank->lock);
    
    printf("Printing contents of bank...\n");
    for(i = 0; i < 20; i++){
        if(the_bank->vault[i].valid){
            if(!the_bank->vault[i].session_flag)
                printf("%s\tBalance: %.2f\n", the_bank->vault[i].name, the_bank->vault[i].balance);
            else
                printf("%s\tIN SESSION\n", the_bank->vault[i].name);
        }
    }
    printf("\n");
    sem_post(&the_bank->lock);
}

/*Handles in session accounts. Sends and processes possible commands for an account session*/
void account_controller(int current, int fd, bank* the_bank){
    char buff[200];
    int numbytes;
    int sval;
    char in_session[300], success[200];
    sprintf(in_session, "\nAccount %s in session. Waiting for current customer to exit...\n", the_bank->vault[current].name);
    while(1){
        sem_getvalue(&the_bank->vault[current].lock, &sval);
        if(sval == 0){
            send(fd, in_session, strlen(in_session), 0);
            sleep(2);
            continue;
        }
        break;
    }
    sem_wait(&the_bank->vault[current].lock);
    sprintf(success, "\nSession for account %s successfully started\n", the_bank->vault[current].name);
    send(fd, success, strlen(success), 0);
    the_bank->vault[current].session_flag = 1;
    print_account_menu(fd);
    the_bank->vault[current].session_flag = 1;
    while((numbytes = recv(fd, buff, MAXDATASIZE-1, 0)) > 0){
        buff[numbytes-1] = '\0';
        if(process_account_command(buff, fd, the_bank, current)) break;
        print_account_menu(fd);
    }
    the_bank->vault[current].session_flag = 0;
    sem_post(&the_bank->vault[current].lock);
}

/*Opens a new account in the bank*/
void open_account(char* command, int fd, bank* the_bank){
    char name[101];
    char extra[10];
    char* bank_full = "\nThe bank is full.  Cannot open a new account\n";
    char* exists = "\nAccount already exists\n";
    char created[200];
    int i;

    sem_wait(&the_bank->lock);
    if(the_bank->size == 20){
        send(fd, bank_full, strlen(bank_full), 0);
        sem_post(&the_bank->lock);
        return;
    }
    sscanf(command, "%s %s", extra, name);
    for(i = 0; i < 20; i++){
        if(strcmp(the_bank->vault[i].name,name)==0){
            send(fd, exists, strlen(exists), 0);
            sem_post(&the_bank->lock);
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
    sprintf(created, "\nAccount %s opened\n", the_bank->vault[i].name);
    send(fd, created, strlen(created), 0);
    return;
}

/*Starts an account session given the account name using the acconunt_controller() function.
  Fails if no valid accounts of that name exist*/
void start(char* command, int fd, bank* the_bank){
    char extra[10];
    char name[101];
    char* dne = "\nAccount named does not exist\n";
    int i;
    sscanf(command, "%s %s", extra, name);

    sem_wait(&the_bank->lock);
    for(i = 0; i < 20; i++){
        if(strcmp(the_bank->vault[i].name, name) == 0 && the_bank->vault[i].valid){
            sem_post(&the_bank->lock);
            account_controller(i, fd, the_bank);
            break;
        }
    }
    if(i == 20){
      send(fd, dne, strlen(dne), 0);
      sem_post(&the_bank->lock);
    } 
}
/*exits bank session and closes process*/
void exit_session(int fd, bank* the_bank){
    char* term = "\nSession terminated\n";
    char* msg = "\nServer disconnected from client. Shutting down...\n";
    send(fd, term, strlen(term), 0);
    send(fd, msg, strlen(msg), 0);
    munmap(the_bank, sizeof(bank));
    printf("Unmapping bank from shared memory\n");
    close(fd);
    exit(0);
}

/*determines the command integer id*/
int get_command_id(char* command){
    char **command_list = (char*[]){"open", "start", "credit", "debit", "balance", "finish", "exit"};
    int i;
    for(i = 0; i < 7; i++){
        if(strcmp(command_list[i], command) == 0) 
            return i;
    }
    return 7;
}

/*processes account session commands: credits, debits, balance, finish */ 
int process_account_command(char* command, int fd, bank* the_bank, int current){
    char command_head[50], extra[10];
    float money;
    char credit[200], debit[200], balance[200], closed[300];
    char* insufficient = "\nInsufficient funds\n";
    char* def = "\nCommand invalid, please enter again\n";

    sprintf(closed, "\nSession for account %s closed\n", the_bank->vault[current].name);
    sscanf(command, "%s", command_head);
    switch(get_command_id(command_head)) {
        case 2: //credit
            sscanf(command, "%s %f", extra, &money);
            (the_bank->vault[current].balance) += money;
            sprintf(credit, "\nCredit of $%.2f applied to current account\n", money);
            send(fd, credit, strlen(credit), 0);
            return 0;
        case 3: //debit
            sscanf(command, "%s %f", extra, &money);
            if(money > the_bank->vault[current].balance){
                send(fd, insufficient, strlen(insufficient), 0);
                return 0;
            }
            (the_bank->vault[current].balance) -= money;
            sprintf(debit, "\nDebit of $%.2f applied to current account\n", money);
            send(fd, debit, strlen(debit), 0);            
            return 0;
        case 4: //balance
            sprintf(balance, "\nBalance of current account is $%.2f\n", the_bank->vault[current].balance);
            send(fd, balance, strlen(balance), 0);            
            return 0;
        case 5: //finish
            send(fd, closed, strlen(closed), 0);
            return 1;
        default:
            send(fd, def, strlen(def), 0);
            return 0;         
    }
}
/*Processes Bank session commands: open, start, exit*/
void process_bank_command(char* command, int fd, bank* the_bank){
    char command_head[50];
    char* def = "\nCommand invalid, please enter again\n";
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
            send(fd, def, strlen(def), 0);
            break;         
    }       
}

//Mmaps a bank account given an open file descriptor to the file. Always clears the file as you cannot close accounts for goood 
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
    else
        printf("Successfully maps bank to file\n");

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
    int print_pid, connection_pid;

    sockfd = bind_and_listen(PORT);
    setup_child_killer();
    printf("server: waiting for connections...\n");

    bank* the_bank;
    shared_file = safe_open("vault");
    the_bank = bank_setup(shared_file);
    print_pid = fork();
    if(print_pid!=0) printf("Created child service process <%d>\n", print_pid);
    if(print_pid == 0){
        while(1){
            print_bank(the_bank);
            sleep(20);
        }
    }
    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        printf("Client connected\n");
        connection_pid = fork();
        if(connection_pid) printf("Created child service process <%d>\n", connection_pid);

        if (!connection_pid) { // this is the child process
            close(sockfd); // child doesn't need the listener
            print_startmenu(new_fd);
            while((numbytes = recv(new_fd, buff, MAXDATASIZE-1, 0)) > 0) { 
                buff[numbytes-1] = '\0';
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
