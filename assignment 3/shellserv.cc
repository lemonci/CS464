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

// use to write data on file history
struct monitor_t {
    pthread_mutex_t mutex;
    char* line_ptr;
}

//monitor is there to collect data
monitor_t mon;

void* monitor (void* ignored){
    // open file 'prev_command' upon CPRINT
    //collect the  result from the client command (output)
        // how to collect the RESULT of execve and copy it through a file?
        // how to copy/paste the same result the status code message back to file??
            //how to printf to client? it it using readline?
    //close the file 'prev_command'
}

void* do_client (int sd){
    const int ALEN = 256;
    char req[ALEN];
    int line;

    //the message line
    char status[5];
    int code;
    char code_num[50];
    char mess[ALEN];

    printf("Incoming client... \n");

    while ( (line = readline(sd, req, ALEN-1)) != 0){
        memset(status, 0, 5);
        memset(code_num, 0, sizeof(code_num));
        memset(mess, 0, sizeof(mess));

        if (strcmp(req, "quit") == 0){
            printf("quit command: sending EOF.\n");
    
            status = "OK ";
            code = 0;
            sprintf(code_num, "%d", code);
            send(sd,status, strlen(status), 0);
            send(sd,code_num, strlen(code_num), 0);
            shutdown(sd,1);
            close(sd);
            printf("Outgoing client...\n");
            return NULL;
        }
    }
}

int main (int argc, char** argv, char** envp){
    const int port = 28638;             //our designated port number
    const int qlen =32;

    long int msock, ssock;                              //master & slave socket
    struct sockaddr_in client_addr;                     // address of client
    unsigned int client_addr_len = sizeof(cient_addr);  // and client addr length

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

    //create monitor thread:
    pthread_create(&tt, &ta, monitor, NULL);

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

    }

    return 0; //it will never reach
}
