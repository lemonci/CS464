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

#include "tokenize.h"
#include "tcp-utils.h"

/*
 * Global configuration variables.
 */
const char* path[] = {"/bin","/usr/bin",0}; // path, null terminated (static)
char output[4096];

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

int run_it (const char* command, char* const argv[], char* const envp[], const char** path, char* mess) {

    // we really want to wait for children so we inhibit the normal
    // handling of SIGCHLD
    signal(SIGCHLD, block_zombie_reaper);

    int childp = fork();
    int status = 0;

    if (childp == 0) { // child does execve

    //redirect output
    // int file_no  = open("output.txt", O_WRONLY | O_CREAT, S_IRUSR|S_IWUSR);
    // if (file_no == -1){
    //     return -2;
    // }
    // int output_no = dup2(file, STDOUT_FILENO);
    // close(file_no);

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
        char* message = new char [strlen(command)+10];
        sprintf(message, "exec %s", command);
        perror(message);
        delete [] message;
        exit(1);   // crucial to exit so that the function does not
                       // return twice!
    }

    else { // parent just waits for child completion
        
        if (waitpid(childp, &status, 0) != -1){
            if (WIFEXITED(status)){
                int status_code = WEXITSTATUS(status);
                if (status_code == 0){
                    //execve is executed
                    sprintf(mess, "OK %d execve complete\n", status_code);
                    printf("%s", mess);
                }
                else if (status_code == 1){
                    //execve has an error
                    sprintf(mess, "FAIL %d execve failed: %s\n", status_code, command);
                    printf("%s", mess);
                }
                else {
                    sprintf(mess, "ERR %d execve terminated abnormally\n", status_code);
                    printf("%s", mess);
                }
                
            }
        }else {
            perror("waitpid() failed");
        }
        
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
    char status[5];
    int code;
    char err_mess[ALEN];
    char mess[ALEN];

    printf("Incoming client... \n");

    signal(SIGCHLD, zombie_reaper);

    while ( (line = readline(sd, req, ALEN-1)) != recv_nodata){
        memset(status, 0, 5);
        memset(mess, 0, sizeof(mess));
        memset(err_mess, 0, sizeof(err_mess));

        if (strcmp(req, "quit") == 0){
            snprintf(status, 3, "OK");
            code = 0;
            sprintf(mess, "%s %d close tcp connection\n", status, code);
            
            //for the output.txt
            printf("quit command: sending EOF.\n");
            printf("%s", mess);

            send(sd, ack, strlen(ack), 0);
            send(sd, mess, strlen(mess), 0);
            shutdown(sd,1);
            close(sd);
            printf("Outgoing client...\n");
            return NULL;
        }
        //just to be safe, even though readline already done so...
        if (strlen(req) > 0 && req[strlen(req)-1] =='\r')
            req[strlen(req) -1] = '\0';
        
        //tokenize input:
        num_tok = str_tokenize(req, com_tok, strlen(req));
        com_tok[num_tok] = 0;   //null termination from execve
        
        //printf("%s\n", com_tok[0]);

        send(sd,ack, strlen(ack),0);
        run_it(com_tok[0], com_tok, envp, path, mess);
        send(sd,mess, strlen(mess),0);
        
    }

    //read 0 bytes = EOF
    shutdown(sd,1);
    close(sd);
    printf("Outgoing client...\n");
    return NULL;
}

int main (int argc, char** argv, char** envp){
    const int port = 28638;             //our designated port number
    const int qlen =32;

    long int msock, ssock;                              //master & slave socket
    struct sockaddr_in client_addr;                     // address of client
    unsigned int client_addr_len = sizeof(client_addr);  // and client addr length

    msock = passivesocket(port, qlen);
    if (msock < 0){
        perror("passivesocket");
        return 1;
    }
    printf("Server up and listening on port %d.\n", port);


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
