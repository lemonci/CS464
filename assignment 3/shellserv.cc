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
}

void* monitor (void* ignored){
    
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


}