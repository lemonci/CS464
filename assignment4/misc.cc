/*
 * Part of the solution for Assignment 3, by Stefan Bruda.
 *
 * This files contains some common code for the two servers, the main
 * functions of the two listening threads, and the main function of
 * the program.
 */
#include "shfd.h"

/*
* the number of peers involved in replication
*/
const int MAX_PEER = 10;  //up to 10 server
struct peers pserv[MAX_PEER];
int replica;                    //real number of replicas


/**
 * preallocated threads
 */
int max_threads;
int incr_threads;
int curr_threads = 0;
int act_threads = 0;

bool tdie = false;
int to_die;


/*
 * Log file
 */
const char* logfile = "shfd.log";
const char* pidfile = "shfd.pid";

/*
 * true iff the threads in file server is alive (and kicking).
 */
bool talive;
/*
 * true iff the file server is alive (and kicking).
 */
bool falive;

pthread_mutex_t thread_mutex;
pthread_mutex_t logger_mutex;

extern char **environ;

/*
 * What to debug (nothing by default):
 */
bool debugs[3] = {false, false, false};

void logger(const char * msg) {
    pthread_mutex_lock(&logger_mutex);
    time_t tt = time(0);
    char* ts = ctime(&tt);
    ts[strlen(ts) - 1] = '\0';
    printf("%s: %s", ts, msg);
    fflush(stdout);
    pthread_mutex_unlock(&logger_mutex);
}

/*
 * Simple conversion of IP addresses from unsigned int to dotted
 * notation.
 */
void ip_to_dotted(unsigned int ip, char* buffer) {
    char* ipc = (char*)(&ip);
    sprintf(buffer, "%d.%d.%d.%d", ipc[0], ipc[1], ipc[2], ipc[3]);
}

int next_arg(const char* line, char delim) {
    int arg_index = 0;
    char msg[MAX_LEN];  // logger string

    // look for delimiter (or for the end of line, whichever happens first):
    while ( line[arg_index] != '\0' && line[arg_index] != delim)
        arg_index++;
    // if at the end of line, return -1 (no argument):
    if (line[arg_index] == '\0') {
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): no argument\n", __FILE__, line ,delim);
            logger(msg);
        } /* DEBUG_COMM */
        return -1;
    }
    // we have the index of the delimiter, we need the index of the next
    // character:
    arg_index++;
    // empty argument = no argument...
    if (line[arg_index] == '\0') {
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): no argument\n", __FILE__, line ,delim);
            logger(msg);
        } /* DEBUG_COMM */    
        return -1;
    }
    if (debugs[DEBUG_COMM]) {
        snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): split at %d\n", __FILE__, line ,delim, arg_index);
        logger(msg);
    } /* DEBUG_COMM */
    return arg_index;
}


/*
*Create preallocated thread in master socket
* 0 = threads are create
* 1= threads cannot be create
* if reach the max_threads just go back....
*/
int set_threads(long int msock) {
     // Setting up the thread creation:
    pthread_t tt;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);
    
    char msg[MAX_LEN];

    for (int i=0; i< incr_threads; i++){
        if (curr_threads <max_threads){
            pthread_mutex_lock(&thread_mutex);
            curr_threads++;
            snprintf(msg, MAX_LEN, "%s: new thread [%d] create...current thread = %d\n", __FILE__, gettid(), curr_threads);
            logger(msg);
            pthread_mutex_unlock(&thread_mutex);
            if(pthread_create(&tt, &ta, (void* (*) (void*))file_client, (void*) msock) != 0){
                snprintf(msg, MAX_LEN, "%s: set threads cannot pthread_create: %s\n", __FILE__, strerror(errno));
                logger(msg);
                snprintf(msg, MAX_LEN, "%s: the file server died.\n", __FILE__);
                logger(msg);
                pthread_mutex_lock(&thread_mutex);
                curr_threads--;
                pthread_mutex_unlock(&thread_mutex);
                talive = false;         //something is wrong with the client thread we should end procedure
                return 1;
            }            
        }
        else{
            break;     // reach max_threads limit
        }
    }
    return 0;                 // meaning the threads have succeed
}

/**
 * handle threads whether to die or not
 * 
 */
void handle_threads(){
        char msg[MAX_LEN];

        pthread_mutex_lock(&thread_mutex);
        if (tdie){
            to_die --;
            curr_threads --;
            if (to_die == 0)
                tdie = false;            
        }
        else{
            // tdie is back to false
            pthread_mutex_unlock(&thread_mutex);
            return;
        }
        pthread_mutex_unlock(&thread_mutex);
        snprintf(msg, MAX_LEN, "%s: idle thread [%d] has died\n", __FILE__, gettid());
        logger(msg);
        pthread_exit(NULL);
    
}

void* file_server (int msock) {
    char msg[MAX_LEN];

    if (set_threads(msock) != 0){              //set the initial threads
        snprintf(msg, MAX_LEN, "%s: file server failed to make new threads from set_threads\n", __FILE__);
        logger(msg);
        snprintf(msg, MAX_LEN, "%s: the file server died.\n", __FILE__);
        logger(msg);
        falive = false;             //we don't have client threads so file server is useless...
        return 0;
    }
    talive = true;              //we have our initial threads

    while (talive){              //keep the initial threads alive 
        sleep(70);              //verify every 1 min if thread are alive?
    }
    falive = false;             //we should also terminate the file server thread too

    return 0;   //it will reach if something bad happen....


/****************************old code*************************************/
    // int ssock;                      // slave sockets
    // struct sockaddr_in client_addr; // the address of the client...
    // socklen_t client_addr_len = sizeof(client_addr); // ... and its length
    // // Setting up the thread creation:
    // pthread_t tt;
    // pthread_attr_t ta;
    // pthread_attr_init(&ta);
    // pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    // char msg[MAX_LEN];  // logger string

    // while (1) {
    //     // Accept connection:
    //     ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
       
    //     //additonal t_incr threads

    //     if (ssock < 0) {
    //         if (errno == EINTR) continue;
    //         snprintf(msg, MAX_LEN, "%s: file server accept: %s\n", __FILE__, strerror(errno));
    //         logger(msg);
    //         snprintf(msg, MAX_LEN, "%s: the file server died.\n", __FILE__);
    //         logger(msg);
    //         falive = false;
    //         return 0;
    //     }

    //     // assemble client coordinates (communication socket + IP)
    //     client_t* clnt = new client_t;
    //     clnt -> sd = ssock;
    //     ip_to_dotted(client_addr.sin_addr.s_addr, clnt -> ip);

    //     // create a new thread for the incoming client:
    //     if ( pthread_create(&tt, &ta, (void* (*) (void*))file_client, (void*)clnt) != 0 ) {
    //         snprintf(msg, MAX_LEN, "%s: file server pthread_create: %s\n", __FILE__, strerror(errno));
    //         logger(msg);
    //         snprintf(msg, MAX_LEN, "%s: the file server died.\n", __FILE__);
    //         logger(msg);
    //         falive = false;
    //         return 0;
    //     }
    //     // go back and block on accept.
    // }
}


void* shell_server (int msock) {
    int ssock;                      // slave sockets
    struct sockaddr_in client_addr; // the address of the client...
    socklen_t client_addr_len = sizeof(client_addr); // ... and its length
    // // Setting up the thread creation:
    // pthread_t tt;
    // pthread_attr_t ta;
    // pthread_attr_init(&ta);
    // pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);               //only accept one connection at a time

    char msg[MAX_LEN];  // logger string

    while (1) {
        // Accept connection:
        ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (ssock < 0) {
            if (errno == EINTR) continue;
            snprintf(msg, MAX_LEN, "%s: shell server accept: %s\n", __FILE__, strerror(errno));
            logger(msg);
            return 0;
        }

        // assemble client coordinates (communication socket + IP)   --> should be local
        client_t* clnt = new client_t;
        clnt -> sd = ssock;
        ip_to_dotted(client_addr.sin_addr.s_addr, clnt -> ip);

        // // create a new thread for the incoming client:
        // if ( pthread_create(&tt, &ta, (void* (*) (void*))shell_client, (void*)clnt) != 0 ) {
        //     snprintf(msg, MAX_LEN, "%s: shell server pthread_create: %s\n", __FILE__, strerror(errno));
        //     logger(msg);
        //     return 0;
        // }

        //only one connection at a time from incoming client:
        shell_client(clnt);

        // go back and block on accept.
    }
    return 0;   // will never reach this anyway...
}

/**
 * Extract the peer from the arg[optind]
 */
void extractPeer(char* address){
    struct peers newPeer;
    size_t len_addr = strlen(address);

    char * token; 
    token = strtok(address, ":");
    if (len_addr == strlen(token)){
        printf("%s: wrong peer address\n", address);
        return;
    }
    else{
        newPeer.phost = token;
        token = strtok(NULL,":");
        if(token == NULL){
            printf("No port number in peer address\n");
            return;
        } 
        else{
            
            if ((newPeer.pport = atoi(token)) == 0){
                printf("Port number invalid\n");
                return;
            }
            pserv[replica] = newPeer;
            replica++;
        }
    }
}

/*
 * Initializes the access control structures, fires up a thread that
 * handles the file server, and then does the standard job of the main
 * function in a multithreaded shell server.
 */
int main (int argc, char** argv, char** envp) {
    int shport = 9001;              // ports to listen to
    int fport = 9002;
    int pport = 10000;
    long int shsock, fsock;              // master sockets
    const int qlen = 32;            // queue length for incoming connections
    char* progname = basename(argv[0]);  // informational use only.
    max_threads = 12;
    incr_threads = 3;               //default preallocated threads



    char msg[MAX_LEN];  // logger string

    pthread_mutex_init(&logger_mutex, 0);
    pthread_mutex_init(&thread_mutex, 0);

    // parse command line
    extern char *optarg;
    int copt;
    bool detach = true;  // Detach by default
    while ((copt = getopt (argc,argv,"s:f:v:t:T:p:dD")) != -1) {
        switch ((char)copt) {
        case 'd':
            detach = false;
            break;
        case 'D':
            debugs[DEBUG_DELAY] = 1;
            printf("will delay file\n");
            break;
        case 'v':
            if (strcmp(optarg,"all") == 0)
                debugs[DEBUG_COMM] = debugs[DEBUG_FILE] = 1;
            else if (strcmp(optarg,"comm") == 0)
                debugs[DEBUG_COMM] = 1;
            else if (strcmp(optarg,"file") == 0)
                debugs[DEBUG_FILE] = 1;
            break;
        case 's':
            shport = atoi(optarg);
            break;
        case 'f':
            fport = atoi(optarg);
            break;
        case 't':
            incr_threads = atoi(optarg);
            break;
        case 'T':
            max_threads = atoi(optarg);
            break;
        case 'p':
            pport = atoi(optarg);
            break;
        }
    }
    //the extra arguments we extract and put them in the struct Peer
    optind --;
    for(; optind < argc && *argv[optind] != '-'; optind++){
        extractPeer(argv[optind]);
    }          

    if (shport <= 0 || fport <= 0 || pport <= 0 || incr_threads <= 0 || max_threads <= 0) {
        printf("Usage: %s  [-d] [-D] [-s port] [-f port] [-p port host:port host:port] [-t preallocate] [-T max_thread] [-v all|file|comm].\n", progname);
        return 1;
    }

    if(incr_threads > max_threads){
        printf("Cannot create threads because t_incr > t_max\n");
        return 1;
    }
    printf("File server creates %d threads with a maximum threads of %d\n", incr_threads, max_threads);

    if (replica == 0){
        printf("no peer for replication. Synchronization off at port %d\n", pport);
    }
    else{
        printf("peers detected...Synchronization on at port %d\n", pport);
        for (int i=0; i< replica; i++){ 
            printf("peer %d: host-> %s  port-> %d\n", i, pserv[i].phost, pserv[i].pport);
            // some flag for synchronization
        }
    }

    // The pid file does not make sense as a lock file since our
    // server never goes down willlingly.  So we do not lock the file,
    // we just store the pid therein.  In other words, we hint to the
    // existence of a pid file but we are not really using it.
    int pfd = open(pidfile, O_RDWR| O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (pfd < 0) {
        perror("pid file");
        printf("Will not write the PID.\n");
    }
    snprintf(msg, MAX_LEN, "%d\n", getpid());
    write(pfd, msg, strlen(msg));
    close(pfd);

    // Initialize the file locking structure:
    flocks_size = getdtablesize();
    flocks = new rwexcl_t*[flocks_size];
    for (size_t i = 0; i < flocks_size; i++)
        flocks[i] = 0;
    
    // Open the master sockets (this is the startup code, since we
    // might not have permissions to open this socket for some reason
    // or another, case in which the startup fails):
    shsock = controlsocket(shport,qlen);
    if (shsock < 0) {
        perror("shell server controlsocket");                                              // now only listen fro local connections i.e. 127.0.0.1
        return 1;
    }
    printf("Shell server up and listening clients on local machine at port %d\n", shport);

    fsock = passivesocket(fport,qlen);
    if (fsock < 0) {
        perror("file server passivesocket");
        return 1;
    }
    printf("File server up and listening on port %d\n", fport);

    // ... and we detach!
    if (detach) {
        // umask:
        umask(0177);

        // ignore SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGALRM, SIGSTOP:
        // (we do not need to do anything about SIGTSTP, SIGTTIN, SIGTTOU)
        signal(SIGHUP,  SIG_IGN);
        signal(SIGINT,  SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        signal(SIGALRM, SIG_IGN);
        signal(SIGSTOP, SIG_IGN);

        // private group:
        setpgid(getpid(),0);

        // close everything (except the master socket) and then reopen what we need:
        for (int i = getdtablesize() - 1; i >= 0 ; i--)
            if (i != shsock && i != fsock)
                close(i);
        // stdin:
        int fd = open("/dev/null", O_RDONLY);
        // stdout:
        fd = open(logfile, O_WRONLY|O_CREAT|O_APPEND,S_IRUSR|S_IWUSR);
        // stderr:
        dup(fd);

        // we detach:
        fd = open("/dev/tty",O_RDWR);
        ioctl(fd,TIOCNOTTY,0);
        close(fd);

        // become daemon:
        int pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid > 0) return 0;  // parent dies peacefully
        // and now we are a real server.
    }

    // Setting up the thread creation:
    pthread_t tt;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    // Launch the thread that becomes a file server:
    if ( pthread_create(&tt, &ta, (void* (*) (void*))file_server, (void*)fsock) != 0 ) {
        snprintf(msg, MAX_LEN, "%s: pthread_create: %s\n", __FILE__, strerror(errno));
        logger(msg);
        return 1;
    }
    falive = true;

    // Continue and become the shell server:
    shell_server(shsock);

    // If we get this far the shell server has died 
    snprintf(msg, MAX_LEN, "%s: the shell server died.\n", __FILE__);
    logger(msg);
    // keep this thread alive for the file server
    while (falive) {
        sleep(30);
    }

    snprintf(msg, MAX_LEN, "%s: all the servers died, exiting.\n", __FILE__);
    logger(msg);
    
    return 1;
}
