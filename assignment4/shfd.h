/*
 * Part of the solution for Assignment 3, by Stefan Bruda.
 *
 * Common header for all the server functions and data.
 */

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <libgen.h>
#include <fcntl.h>
#include <unistd.h>

#include "tcp-utils.h"

/*** Global stuff: ***/

/*
* Structure to collect each peer server IP and port number
*/
struct peers{
    int pport;
    char* phost;
};


/*
 * Structure for parameters to the client handling function.  the IP
 * address is used for logging.
 */
struct client_t {
    int sd;    // the communication socket
    char ip[20];   // the (dotted) IP address
};

/* 
 * Log file (both stderr and stdout):
 */
extern const char* logfile;

/*
 * Buffer size for various command and data buffers.
 */
const size_t MAX_LEN = 1024;

/*
 * nextarg(line, delim) looks for the first occurrence of `delim' in
 * `line' and returns the index of the character just after this
 * occurrence.  If no occurrence of `delim' exists in `line', or if
 * the first occurrence of `delim' is the last character in the
 * string, returns -1.
 *
 * This function is used to parse the client request.  If req is such
 * a request, then &req[next_arg(req,' ')] is a string that contains
 * whatever was sent by the client sans the name of the command, and
 * so on.  The function is non destructive.
 */
int next_arg(const char*, char);

/*
 * Debug constants and variables:
 */
const size_t DEBUG_COMM = 0;
const size_t DEBUG_FILE = 1;
const size_t DEBUG_DELAY = 2;
extern bool debugs[3]; // What to debug

/*
 * Log functions, just does a cout on the argument (prefixed by the
 * current time) at this time, but a separate function is provided for
 * flexibility (it is thus easy to switch to system logger).
 */
void logger(const char *);

/*
 * Mutex for the logger function (needed because the logger uses the
 * function cdate() which is not thread safe).
 */
extern pthread_mutex_t logger_mutex;


/*** File server stuff: ***/

/*
 * The structure implementing the access restrictions for a file.
 * Also contains the file descriptor for the file (for easy access)
 * and the name of the thing.
 *
 * The access control to files is implemented using a condition
 * variable (basically, one can access the file iff nobody writes to
 * it).
 */
struct rwexcl_t {    
    pthread_mutex_t mutex;      // mutex for the whole structure
    pthread_cond_t can_write;   // condition variable, name says it all
    unsigned int reads;         // number of simultaneous reads (a write
                                // process should wait until this number is 0)
    unsigned int owners;        // how many clients have the file opened
    int fd;                     // the file descriptor (also used as
                                // file id for the clients)
    ino_t inode;                // inode (used for identifying identical files)
    char* name;                 // the (absolute) name of the file (for debugging purposes)
};

/*
 * The access control structure for the opened files (initialized in
 * the main function), and its size.
 */
extern rwexcl_t** flocks;
extern size_t flocks_size;

/*
 * Invalid descriptor error value.
 */
const int err_nofile = -2;

/*
 * Client handler for the file server.  Keeps reading requests from
 * the socket given as argument and responds to them accordingly.
 * Terminates when receives the command QUIT or when the cliens closes
 * the connection.  The names of the commands are case insensitive.
 */
void* file_client (client_t*);



/*** Shell server stuff: ***/

/*
 * The child process executing an external command returns this on
 * exec* or file errors.
 */
const int err_exec = 0xFF;

/*
 * Client handler for the shell server.  Keeps reading requests from
 * the socket given as argument and responds to them accordingly.
 * Terminates upon an end of file from the client.
 */
void* shell_client(client_t*);

