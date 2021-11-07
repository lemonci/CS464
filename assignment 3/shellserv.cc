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


void* do_client (int sd, char** envp){
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





int main (int argc, char** argv, char** envp){
    struct cmdArgs cmd;

    //int port = 28638;             //our designated port number
    const int qlen =32;
    const char* logfile = "shfd.log";

    long int msock, ssock;                              //master & slave socket
    struct sockaddr_in client_addr;                     // address of client
    unsigned int client_addr_len = sizeof(client_addr);  // and client addr length

    //parse the command line
    parse_arguments(argc, argv, &cmd);

    //fork (from shell server to file server)
    //
    //
    //

    //binding the socking passively
    msock = passivesocket(cmd.port_num_s, qlen);
    if (msock < 0){
        perror("passivesocket");
        return 1;
    }
    printf("Server up and listening on port %d.\n", cmd.port_num_s);
    printf("detached mode: %d.\n", cmd.nodetach);
    


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
            if (i != msock)
                close(i); 
        }

        int x_fd = open("/dev/null", O_RDWR);   //for fd(0)

        int x_fd2 = open(logfile, O_WRONLY | O_CREAT | O_APPEND); 
        
        int file_outErr = dup(x_fd2);  //for fd(1 &2)
       
        if ( x_fd< 0 | x_fd2 < 0| file_outErr < 0)
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
        if ( pthread_create(&tt, &ta, (void* (*) (void*)) do_client, (void*) ssock) != 0){
            perror("pthread_create");
            return 1;
        }
        //we start accepting other clients (main thread continues with the loop)
    }

    return 0; //it will never reach
}
