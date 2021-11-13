#include <stdio.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h> 

#include "tokenize.h"
#include "tcp-utils.h"
#include "fserv.h"
//#define DEBUG

/***
 * Structure to absorb command line
 **/
struct cmdArgs{
    int port_num_f = 9001;
    int port_num_s = 9002;
    bool nodetach = false;
    bool delay = false;
};

/*
 * Global configuration variables.
 */
const char* path[] = {"/bin","/usr/bin",0}; // path, null terminated (static)

/*
 * Typical reaper.
 */
void zombie_reaper (int signal) {
    int ignore;
    while (waitpid(-1, &ignore, WNOHANG) >= 0)
        /* NOP */;
}

/*
 * Dummy reaper, set as signal handler in those cases when we want
 * really to wait for children.  Used by run_it().
 *
 * Note: we do need a function (that does nothing) for all of this, we
 * cannot just use SIG_IGN since this is not guaranteed to work
 * according to the POSIX standard on the matter.
 */
void block_zombie_reaper (int signal) {
    /* NOP */
}

int run_it (const char* command, char* const argv[], char* const envp[], const char** path, const char* filename, const int sd) {

    // we really want to wait for children so we inhibit the normal
    // handling of SIGCHLD
    signal(SIGCHLD, block_zombie_reaper);

    const int ALEN = 256;
    char mess[ALEN];

    int childp = fork();
    int status = 0;

    if (childp == -1)
        perror("fork");
    
    if (childp == 0) { // child does execve

    //redirect output
    close(1);
    close(2);
    int file_no  = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR|S_IWUSR);
    if (file_no == -1){
        perror("redirect: ");
        return -2;
    }
    int file_out = dup2(file_no, STDOUT_FILENO);
    int file_err = dup2(file_no, STDERR_FILENO);
    
    if (file_out <0 || file_err < 0){
        printf("Unable to duplicate file descriptor.\n");
        exit(EXIT_FAILURE);
    }

#ifdef DEBUG
        fprintf(stderr, "%s: attempting to run (%s)", __FILE__, command);
        for (int i = 1; argv[i] != 0; i++)
            fprintf(stderr, " (%s)", argv[i]);
        fprintf(stderr, "\n");
#endif
        execve(command, argv, envp);     // attempt to execute with no path prefix...
        for (size_t i = 0; path[i] != 0; i++) { // ... then try the path prefixes
            char* cp = new char [strlen(path[i]) + strlen(command) + 2];
            sprintf(cp, "%s/%s", path[i], command);
#ifdef DEBUG
            fprintf(stderr, "%s: attempting to run (%s)", __FILE__, cp);
            for (int i = 1; argv[i] != 0; i++)
                fprintf(stderr, " (%s)", argv[i]);
            fprintf(stderr, "\n");
#endif
            execve(cp, argv, envp);
            delete [] cp;
        }

        // If we get here then all execve calls failed and errno is set
        // char* message = new char [strlen(command)+10];
        // sprintf(message, "exec %s", command);
        // perror(message);

        sprintf(mess, "\nFAIL %d execve %s\n", errno, command);
        printf("%s", mess);
        send(sd,mess, strlen(mess),0);
        // delete [] message;
        exit(255);   // crucial to exit so that the function does not
                       // return twice!
    }

    else { // parent just waits for child completion
        
        if (waitpid(childp, &status, 0) != -1){
            int fileno = open(filename, O_WRONLY|O_APPEND, S_IWUSR|S_IRUSR);
            if (WIFEXITED(status)){
                int status_code = WEXITSTATUS(status);
                if (status_code == 0){
                    //execve is executed
                    sprintf(mess, "OK %d execve complete\n", status_code);
                    printf("%s", mess);
                    
                }else if (status_code == 255){
                    //to identify the failure
                    //do nothing except flag it  to server
                    sprintf(mess, "FAIL at execve\n");
                    printf("%s", mess);
                    goto endrun;
                }
                else {
                    //execve has an error
                    sprintf(mess, "ERR %d execve terminated abnormally\n", status_code);
                    printf("%s", mess);
                }
                    write(fileno, mess, strlen(mess));
                    close(fileno);
                    send(sd,mess, strlen(mess),0);
            }
        }else {
            perror("waitpid() failed");
            exit(EXIT_FAILURE);
        }

        endrun:
        // we restore the signal handler now that our baby answered
        signal(SIGCHLD, zombie_reaper);
        return status;
    }
}


void* do_client_s (int sd, char** envp){
    const int ALEN = 256;
    char req[ALEN];
    const char* ack = "ACK: ";
    int line;           // to receive the command
    
    //to receive the client command
    req[255] = '\0';
    char* com_tok[129];  // buffer for the tokenized commands
    size_t num_tok;      // number of tokens

    //the message line
    char mess[ALEN];
    char output[ALEN];

    int pid= getpid();
    sprintf(output, "temp_%d_%d",sd, pid);
    int lastfd;       //fd of last command

    signal(SIGCHLD, zombie_reaper);

    while ( (line = readline(sd, req, ALEN-1)) != recv_nodata){
        printf("Incoming client %d... \n",sd);

        memset(mess, 0, sizeof(mess));
        

        if (strcmp(req, "quit") == 0){
            sprintf(mess, "OK 0 close tcp connection\n");
            
            //server-side
            printf("quit command: sending EOF.\n");

            send(sd, ack, strlen(ack), 0);
            send(sd, mess, strlen(mess), 0);
            shutdown(sd,1);
            close(sd);
            if(unlink(output) != 0)
                perror("unlink fail: ");
            printf("Outgoing client %d...\n", sd);
            return NULL;
        }
        //CPRINT
        else if (strcmp(req, "CPRINT") == 0){
            lastfd = open(output, O_RDONLY);
            if (lastfd < 0){
                //cannot open the file, problably no command is issued
                sprintf(mess, "\nFAIL %d EIO: Input/output error\n", lastfd);
                //server-side
                printf("%s", mess);

                send(sd, ack, strlen(ack), 0);
                send(sd, mess, strlen(mess), 0);

                //now we create the file
                lastfd = open (output, O_WRONLY|O_CREAT |O_TRUNC,S_IRUSR|S_IWUSR);
                write(lastfd, mess, strlen(mess));
                close(lastfd);
            }
            else{
                //the file is written
                while ( int n = readline(lastfd, mess, ALEN-1) != recv_nodata){
                    if (n < 0){
                        sprintf(mess, "output: file problem\n");
                        perror(mess);
                        break;
                    }
                    
                    send(sd, mess, strlen(mess), 0);
                    send(sd, "\n", 1, 0);
                }
                close(lastfd);
            }
            
            continue;
        }

        //just to be safe, even though readline already done so...
        if (strlen(req) > 0 && req[strlen(req)-1] =='\r')
            req[strlen(req) -1] = '\0';
        
        //tokenize input:
        num_tok = str_tokenize(req, com_tok, strlen(req));
        com_tok[num_tok] = 0;   //null termination from execve
        
        //printf("%s\n", com_tok[0]);

        send(sd,ack, strlen(ack),0);
        run_it(com_tok[0], com_tok, envp, path, output, sd);
        
        
    }

    //read 0 bytes = EOF
    shutdown(sd,1);
    close(sd);
    if(unlink(output) != 0)
        perror("unlink fail: ");
    printf("Outgoing client %d...\n", sd);
    return NULL;
}

//parse the arguments for the code
void parse_arguments(int argc, char** argv, struct cmdArgs* cmdargs){
	if (argc <= 1){
		printf("Nothing to parse\nSetting up default features\n");
		return;
	}
	for(int i=1; i<argc ;i++){
		// -f specifies the file server port
		if(strcmp(argv[i], "-f") == 0 && i+1 < argc){
		cmdargs->port_num_f = atoi(argv[i+1]);
		i++;
#ifdef DEBUG
		printf("The file server port is now : %d\n",cmdargs->file_server_port);
#endif 	
		}
		// -s specifies the shell server prot 
		else if(strcmp(argv[i], "-s") == 0 && i+1 < argc){
		cmdargs->port_num_s = atoi(argv[i+1]);
		i++;
#ifdef DEBUG
		printf("The shell server port is now : %d\n",cmdargs->shell_server_port);
#endif 
		}
		// -d does not detach, debugging mode
		// TODO case sensetive check
		else if(strcmp(argv[i], "-d") == 0){
		cmdargs->nodetach = true;
#ifdef DEBUG
		printf("debugging mode is on\n");
#endif
		}

		// -D delays read and write operations
		// TODO case sensetive check
		else if(strcmp(argv[i], "-D") == 0){
		cmdargs->delay = true;
#ifdef DEBUG
		printf("Delay mode is on\n");
#endif		
		}

		// -v
		else if(strcmp(argv[i], "-v") == 0){
        #define DEBUG
#ifdef DEBUG
		printf("verbose toggle now on\n");
#endif
		}
		else {
			printf("invalid argument: %s, ignored\n",argv[i]);
		}
	}
	return;
}


#define PORT_NUMBER 28648
#define QLENGTH 32
#define FILE_QUANTITY 10
pthread_mutex_t lock;

struct fileRecord {
    pthread_mutex_t mutex; //mutex for the whole structure
    pthread_cond_t can_write; //condition variable, name says it all
    unsigned int readers; //number of simultaneous rads ( a write process should wait until this number is 0)
    unsigned int owners; //how many clients have the file opened
    FILE* fp; //the file descriptor (also used as file id for the clients)

    char* name; // the (absolue) name of the file
};

struct fileRecord fileArray[FILE_QUANTITY];

void* do_client_f (int sd)
{
    const int ALEN = 256;
    char req[ALEN];
    const char* ack = "ACK : ";
    const char* ackOK = "OK";
    const char* ackERR = "ERR";
    const char* ackFAIL = "FAIL";
    char ack1[1000];
    int n;
    FILE* fp = NULL;
    char* com_tok[129];
    size_t num_tok;

    pid_t x = syscall(__NR_gettid);
    //std::cout<<"Thread id: "<<x;

    char str[10];
    sprintf(str, "%d", x);

    printf("Incoming client...\n");

    // Loop while the client has something to say...
    while ((n = readline(sd,req,ALEN-1)) != recv_nodata) //change to while 1, change the condition to an operation
    {

        num_tok = str_tokenize(req, com_tok, strlen(req));
        //str_tokenize(req, com_tok, strlen(req));

        if (strcmp(com_tok[0],"quit") == 0)
        {
            printf("Received quit, sending EOF.\n");
            shutdown(sd,1);
            close(sd);
            printf("Outgoing client...\n");
            return NULL;
        }
        else if(strcmp(com_tok[0],"FOPEN")==0)
        {
          if(num_tok!=2)
          {
            snprintf(ack1, sizeof ack1,"%s %d Improper FOPEN.\nFOPEN format -> FOPEN filename \n", ackFAIL,errno);
            send(sd,ack1,strlen(ack1),0);
          }
          else
          {
                int aclck;
                aclck=pthread_mutex_trylock(&lock); //lock not initialized
                if(aclck==0) //file can be locked
                {
                  int identifier = check_descriptor(com_tok[1]); //check if the file is open per file_table. If zero, not open.
                  if(identifier==-1) //file doesn't appear in file_table
                  {
                    fp = fopen(com_tok[1], "r+");
                    //If the file does not exist in the file system, create a file
                    if (fp == NULL)
                    {
                        fp = fopen(com_tok[1], "w+");
                        if (fp == NULL)
                        {
                            snprintf(ack1, sizeof ack1,"%s %d\n", ackFAIL,errno);
                            send(sd,ack1,strlen(ack1),0);
                            continue;
                        }
                        else send(sd,ack1,strlen(ack1),0);
                    }
                    identifier = write_descriptor(getpid(),com_tok[1],fp, 0); //write file information to file_table
                    if(identifier !=-1)
                    {
                      snprintf(ack1, sizeof ack1,"%s %d\n", ackOK, identifier);
                      send(sd,ack1,strlen(ack1),0);
                    }
                    else
                    {
                      snprintf(ack1, sizeof ack1,"%s %d\n", ackFAIL,errno);
                      send(sd,ack1,strlen(ack1),0);
                    }
                  }
                  else
                  { //lock successful, file has been openedn per file_table record
                    fileArray[identifier].owners ++;
                    snprintf(ack1, sizeof ack1,"%s %d\n", ackERR, identifier);
                    send(sd,ack1,strlen(ack1),0);
                  }
                  pthread_mutex_unlock(&lock);

                }
                else  //file not locked
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackFAIL,errno);
                  send(sd,ack1,strlen(ack1),0);
                }
                //pthread_mutex_unlock(&lock);
            }
        }
        else if(strcmp(com_tok[0],"FSEEK")==0) //com_tok[0] == "FSEEK", com_tok[1] == identifier, com_tok[2] == offset
        {
          if(num_tok!=3)
          { //if FSEEK syntax is not correct
            snprintf(ack1, sizeof ack1,"%s %d Improper FSEEK.\nFSEEK format -> FSEEK identifier offset \n", ackFAIL,errno);
            send(sd,ack1,strlen(ack1),0);
          }
          else
          { //if FSEEK syntax is correct
                int skrt = fseek(fileArray[atoi(com_tok[1])].fp, atoi(com_tok[2]), SEEK_CUR);
                if(skrt==0)
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackOK,0);
                  send(sd,ack1,strlen(ack1),0);
                }
                else
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackERR,ENOENT);
                  send(sd,ack1,strlen(ack1),0);
                }
          }
        }
        else if(strcmp(com_tok[0],"FREAD")==0)
        {
          if(num_tok!=3) //FREAD syntax not correct
          {
            snprintf(ack1, sizeof ack1,"%s %d Improper FREAD.\nFREAD format -> FREAD identifier length \n", ackFAIL,errno);
            send(sd,ack1,strlen(ack1),0);
          }
          else
          { //ADD a condition to judge if the file is being written by other user
                int identifier = atoi(com_tok[1]);
                int length = atoi(com_tok[2]);
                if (identifier >= FILE_QUANTITY || identifier < 0)
                {
                    send(sd,"The identifier is not valid.\n",strlen("The identifier is not valid.\n"),0);
                    continue;
                }
                else
                {
                    if (fileArray[identifier].fp == NULL)
                    {
                        send(sd,"The identifier doesn't represent an opened file.\n",strlen("The identifier doesn't represent an opened file.\n"),0);
                        continue;
                    }
                    else
                    {
                        pthread_mutex_lock(&fileArray[identifier].mutex);
                        fileArray[identifier].readers++;
                        if (delay == true) sleep(5);
                        snprintf(ack1, sizeof ack1,"%s %d\n", ackOK,0);
                        send(sd,ack1,strlen(ack1),0);
                        pthread_mutex_unlock(&fileArray[identifier].mutex);
                        char * buffer = (char *) malloc(length);;
                        fread(buffer, length, 1, fileArray[identifier].fp);
                        printf("%s\n", buffer);
                        pthread_mutex_lock(&fileArray[identifier].mutex);
                        fileArray[identifier].readers--;
                        pthread_mutex_unlock(&fileArray[identifier].mutex);
                        
                        if (fileArray[identifier].readers == 0) pthread_cond_broadcast(&fileArray[identifier].can_write);
                        
                        if (delay == true) sleep(30);
                        snprintf(ack1, sizeof ack1,"%s %d\n", ackOK,0);
                        send(sd,ack1,strlen(ack1),0);
                    }
                }
            }
        }
        else if(strcmp(com_tok[0],"FWRITE")==0)
        {
          if(num_tok!=3)
          {
            snprintf(ack1, sizeof ack1,"%s %d Improper FWRITE.\nFWRITE format -> FWRITE identifier bytes \n", ackFAIL,errno);
            send(sd,ack1,strlen(ack1),0);
          }
          else
          {
            int identifier = atoi(com_tok[1]);
            char bytes[1000];
            strncpy(bytes, com_tok[2], strlen(com_tok[2]));
            if (identifier >= FILE_QUANTITY || identifier < 0)
            {
                send(sd,"The identifier is not valid.",strlen("The identifier is not valid."),0);
                continue;
            }
            else
            {
                if (fileArray[identifier].fp == NULL)
                {
                    send(sd,"The identifier doesn't represent an opened file.",strlen("The identifier doesn't represent an opened file."),0);
                    continue;
                }
                else
                {
                    //printf("%s", bytes);
                    pthread_mutex_lock(&fileArray[identifier].mutex);
                    if (delay == true) sleep(30);
                    snprintf(ack1, sizeof ack1,"%s %d\n", ackOK,0);
                    send(sd,ack1,strlen(ack1),0);
                    while (fileArray[identifier].readers != 0) { pthread_cond_wait(&fileArray[identifier].can_write, &fileArray[identifier].mutex); }
                    fileArray[identifier].readers ++;
                    pthread_mutex_unlock(&fileArray[identifier].mutex);
                    pthread_mutex_lock(&fileArray[identifier].mutex);
                    fwrite(bytes , 1 , strlen(com_tok[2]) , fileArray[identifier].fp ); //write the file
                    fileArray[identifier].readers --;
                    if (fileArray[identifier].readers == 0) pthread_cond_broadcast(&fileArray[identifier].can_write);
                    pthread_mutex_unlock(&fileArray[identifier].mutex);
                }
            }
          }
        }
        
        else if(strcmp(com_tok[0],"FCLOSE")==0)
        {
          if(num_tok!=2)
          {
            snprintf(ack1, sizeof ack1,"%s %d Improper FCLOSE.\nFCLOSE format -> FCLOSE identifier \n", ackFAIL,errno);
            send(sd,ack1,strlen(ack1),0);
          }
          else
          { // check if someone else is reading/writing the file 
            int identifier = atoi(com_tok[1]); //if com_tok[1] is not a string??
                if (identifier < FILE_QUANTITY && identifier >= 0)
                {
                    if(fileArray[identifier].fp != NULL)
                    {
                      fclose(fileArray[identifier].fp);
                      fileArray[identifier].fp = NULL;
                      send(sd,"The file has been closed.\n",strlen("The file has been closed.\n"),0);
                    }
                    else send(sd,"The identifier doesn't represent an opened file.\n",strlen("The identifier doesn't represent an opened file.\n"),0);
                }
                else send(sd,"The identifier is not valid.\n",strlen("The identifier is not valid.\n"),0);
          }

        } //Add compare if QUIT
        else
        {
          send(sd,ack,strlen(ack),0);
          send(sd,"I hope that's not a valid command for me.\n",strlen("I hope that's not a valid command for me.\n"),0);
          send(sd,req,strlen(req),0);
          send(sd,"\n",1,0);
        }

    }
    // read 0 bytes = EOF:
    printf("Connection closed by client.\n");
    shutdown(sd,1);
    close(sd);
    printf("Outgoing client...\n");
    return NULL;
}


FILE* create_file(char* file_name)
{
    //This function handles user-program-file-descriptor-table.
    FILE* fp;

    printf("-----> File %s created for client",file_name);

    //creating the file
    fp = fopen(file_name, "w+");  //Actually, we don't need the path. We can just create a file with filename.
    return fp;
}

void add_trailing_spaces(char *dest, int size, int num_of_spaces)
{
    int len = strlen(dest);

    if( len + num_of_spaces >= size )
    {
        num_of_spaces = size - len - 1;
    }

    memset( dest+len, ' ', num_of_spaces );
    dest[len + num_of_spaces] = '\0';
}

int initiate_descriptor() //Can be replaced with an array of struct
{
    for (int i = 0; i < FILE_QUANTITY; i++) {
        fileArray[i].name = (char *) malloc(80);
    fileArray[i].fp = NULL;
    }
    return 0;
}   
    
int write_descriptor(int pid, char file_name[80],FILE* file_desc,int deldes=0) //Rewrite
{
    int writeAddr = -1;
    for (int i = 0; i < FILE_QUANTITY; i++) {
    if (fileArray[i].fp == NULL)
    {
    writeAddr = i;
    break;
    }
    }
    if (writeAddr == -1) return -1;
    //else  
    pthread_mutex_init(&fileArray[writeAddr].mutex, NULL);//mutex for the whole structure
    pthread_cond_init(&fileArray[writeAddr].can_write, NULL);
    fileArray[writeAddr].readers = 0; //number of simultaneous rads ( a write process should wait until this number is 0)
    fileArray[writeAddr].owners = 1; //how many clients have the file opened
    fileArray[writeAddr].fp = file_desc; //the file descriptor (also used as file id for the clients)
    strncpy(fileArray[writeAddr].name, file_name, 200); // the (absolue) name of the file
    
    return writeAddr;
}

int check_descriptor(char file_name[80])
{
    for (int i = 0; i < FILE_QUANTITY; i++) {
    if (fileArray[i].fp == NULL) continue;
    if (strcmp(fileArray[i].name, file_name) == 0)
    {
        return i;
    }
    }
    return -1;
}


int main (int argc, char** argv, char** envp){
    struct cmdArgs cmd;

    //int port = 28638;             //our designated port number
    const int qlen =32;
    const char* logfile = "shfd.log";
    int x_fd2;

    long int msock, ssock;                              //master & slave socket
    struct sockaddr_in client_addr;                     // address of client
    unsigned int client_addr_len = sizeof(client_addr);  // and client addr length

    //parse the command line
    parse_arguments(argc, argv, &cmd);
    if (cmd.nodetach == false){
        x_fd2 = open(logfile, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR|S_IWUSR); 
    }

    if (cmd.delay == true)
        detectDelay(&cmd.delay);

    //fork (from shell server to file server)
    pid_t serv_pid;
    serv_pid = fork();
    if (serv_pid < 0){
        perror("file serv did not fork:");
        exit(EXIT_FAILURE);
    }
    if (serv_pid == 0){
        //file server will bind its socket
        //binding the socking passively
        msock = passivesocket(cmd.port_num_f, qlen);
        if (msock < 0){
            perror("passivesocket");
            return 1;
        }
        printf("Server up and listening on port %d.\n", cmd.port_num_f);
        printf("detached mode: %d.\n", cmd.nodetach);
        
    }
    else{
        //shell server will bind its socket
        //binding the socking passively
        msock = passivesocket(cmd.port_num_s, qlen);
        if (msock < 0){
            perror("passivesocket");
            return 1;
        }
        printf("Server up and listening on port %d.\n", cmd.port_num_s);
        printf("detached mode: %d.\n", cmd.nodetach);
    }


    //creating the daemon
    pid_t pid;


    // no detach (no -d) goes into the background
    if (cmd.nodetach == false){   
        pid = fork();
        if (pid< 0){
            perror("startup fork");
            exit(EXIT_FAILURE);
        }
        if (pid > 0)
            exit(EXIT_SUCCESS);   //BYE THE FOREGROUND
        
        //we are in the child now as server in a new session -TTY purpose
        if (setsid() < 0)
            exit(EXIT_FAILURE);
        
        //ignore signals
        signal(SIGCHLD, SIG_IGN);
        signal(SIGHUP, SIG_IGN);

        //redirect standard output & error into the file descriptor shfd.log
        //close all descriptors except standard output & standard error
        
        //then we close all the descriptor
        for ( int i = getdtablesize() - 1; i >= 0; i --){
            if (i != msock && i != x_fd2)
                close(i); 
        }
        
        int x_fd = open("/dev/null", O_RDWR);   //for fd(0)

        //int x_fd2 = open(logfile, O_WRONLY | O_CREAT | O_APPEND); 
        
        int file_outErr = dup(x_fd2);  //for fd(1 &2)
       
        if ( x_fd< 0 || x_fd2 < 0 || file_outErr < 0)
            printf("Error at the detach");

        //set new file permisssion created by daemon
        umask(077);
        
    }
    else{       
        // -d is present (detached = debug mode)
        
        //remove from the controlling tty
        int fd = open("/dev/tty", O_RDWR);
        ioctl(fd, TIOCNOTTY, 0);
        close(fd);
    }

    //Set the  threads
    pthread_t tt;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);

    while (1){
        //Accept connection:
        ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (ssock < 0){
            if (errno == EINTR)
                continue;
            perror ("accept");
            return 1;
        }

        //create thread + execute in do_client:
        if (serv_pid == 0){
            if ( pthread_create(&tt, &ta, (void* (*) (void*)) do_client_f, (void*) ssock) != 0){
            perror("pthread_create_f");
            return 1;
            }
        }
        else{
            if ( pthread_create(&tt, &ta, (void* (*) (void*)) do_client_s, (void*) ssock) != 0){
                perror("pthread_create_s");
                return 1;
            }
        }
        //we start accepting other clients (main thread continues with the loop)
    }

    return 0; //it will never reach
}