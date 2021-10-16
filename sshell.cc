/*
 * Part of the solution for Assignment 2
 */

//#define DEBUG

#include <stdio.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "tokenize.h"
#include "tcp-utils.h"  // for `readline' only

#include <sys/socket.h> // for the TCP socket connection


/*
 * Global configuration variables.
 */
const char* PATH[] = {"/bin","/usr/bin",0}; // path, null terminated (static)
const char* PROMPT = "sshell> ";            // prompt
const char* CONFIG = "shconfig";            // configuration file
const int ALEN = 256;                       // the length of each  TCP reply
int ka_flag = 0;            //keep alive flag to prevent double connection
//int ka_sd = -1;             // keep alive unique socket (set it negative)
int g_socket = -1; // a socket that survive until error or closed.
size_t hsize = 0, vsize = 0, rport = 0;  // terminal dimensions, read from and port number
char* rhost = 0;                    //machine name
char host[128];


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

/*
 * run_it(c, a, e, p) executes the command c with arguments a and
 * within environment p just like execve.  In addition, run_it also
 * awaits for the completion of c and returns a status integer (which
 * has the same structure as the integer returned by waitpid). If c
 * cannot be executed, then run_it attempts to prefix c successively
 * with the values in p (the path, null terminated) and execute the
 * result.  The function stops at the first match, and so it executes
 * at most one external command.
 */
int run_it (const char* command, char* const argv[], char* const envp[], const char** path) {

    // we really want to wait for children so we inhibit the normal
    // handling of SIGCHLD
    signal(SIGCHLD, block_zombie_reaper);

    int childp = fork();
    int status = 0;

    if (childp == 0) { // child does execve
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
        exit(errno);   // crucial to exit so that the function does not
                       // return twice!
    }

    else { // parent just waits for child completion
        waitpid(childp, &status, 0);
        // we restore the signal handler now that our baby answered
        signal(SIGCHLD, zombie_reaper);
        return status;
    }
}

/*
 * Implements the internal command `more'.  In addition to the file
 * whose content is to be displayed, the function also receives the
 * terminal dimensions.
 */
void do_more(const char* filename, const size_t hsize, const size_t vsize) {
    const size_t maxline = hsize + 256;
    char* line = new char [maxline + 1];
    line[maxline] = '\0';

    // Print some header (useful when we more more than one file)
    printf("--- more: %s ---\n", filename);

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        sprintf(line, "more: %s", filename);
        perror(line);
        delete [] line;
        return;
    }

    // main loop
    while(1) {
        for (size_t i = 0; i < vsize; i++) {
            int n = readline(fd, line, maxline);
            if (n < 0) {
                if (n != recv_nodata) {  // error condition
                    sprintf(line, "more: %s", filename);
                    perror(line);
                }
                // End of file
                close(fd);
                delete [] line;
                return;
            }
            line[hsize] = '\0';  // trim longer lines
            printf("%s\n", line);
        }
        // next page...
        printf(":");
        fflush(stdout);
        fgets(line, 10, stdin);
        if (line[0] != ' ') {
            close(fd);
            delete [] line;
            return;
        }
    }
    delete [] line;
}

int communication(char* commandline){
        //communicating with server
        char ans[ALEN];             //to catch the reply
        int ans_count = 0;

#ifdef DEBUG
        printf("Commmunication start");
        printf("Sending: %s\n",real_com[0]);
#endif   
        send(g_socket, commandline, strlen(commandline), 0);               
        send(g_socket,"\n",1,0);

        if (!ka_flag){
            shutdown(g_socket, SHUT_WR); // if not kept alive, stop sending
        }

        // printf("***inside communication ka_flag, sd:%d, %d\n", ka_flag, g_socket);
        //collect the reply
        while(1){     //5000ms timeout

            // read data. If no data, end funtion and return error code 3
            if (( ans_count = recv_nonblock(g_socket, ans, ALEN-1, 5000)) == recv_nodata){
				if (!ka_flag) { // If not kept alive, close connection.
                    shutdown(g_socket, SHUT_RDWR);   //no more sends or receive
                    close(g_socket);
                    printf("Connection is closed\n");
                    g_socket = -9;                  
                }
				return 3;}

            // Normal processing, when ans_count is positive
            if (ans_count > 0)
            {
                ans[ans_count] = '\0';                  // end of the data string 
                printf("%s", ans);
                fflush(stdout);
            }
            // Reach the end of data, exit with no-error code 0
            else if (ans_count == 0){

                if (!ka_flag) { // If not kept alive, close connection.
                    shutdown(g_socket, SHUT_RDWR);   //no more sends or receive
                    close(g_socket);
                    printf("Connection is closed\n");
                    g_socket = -9;                  
                }
                return 0;                
            }
            // When ans_count is negative, kill connexion, and exit with error code 1. 
            else {
                perror("recv_noblock: ");    //something went wrong with receiving data
                shutdown(g_socket, SHUT_RDWR);        //block all bad data
                close(g_socket);
                g_socket = -9;
                return 1;
            }
        }
}

int setupParameter(){
    char fileline[129];   // buffer for commands
    fileline[128] = '\0';
    char* configblock[129];  // buffer for the tokenized commands
//    size_t num_block;      // number of tokens

    // Config:
    int confd = open(CONFIG, O_RDONLY);
    if (confd < 0) {
        perror("config");
        fprintf(stderr, "config: cannot open the configuration file.\n");
        fprintf(stderr, "config: will now attempt to create one.\n");
        confd = open(CONFIG, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        if (confd < 0) {
            // cannot open the file, giving up.
            perror(CONFIG);
            fprintf(stderr, "config: giving up...\n");
            return 1;
        }
        close(confd);
        // re-open the file read-only
        confd = open(CONFIG, O_RDONLY);
        if (confd < 0) {
            // cannot open the file, we'll probably never reach this
            // one but who knows...
            perror(CONFIG);
            fprintf(stderr, "config: giving up...\n");
            return 1;
        }
    }

    // read terminal size
    while (hsize == 0 || vsize == 0 || rhost == 0 || rport == 0) {
        int n = readline(confd, fileline, 128);
        if (n == recv_nodata)
            break;
        if (n < 0) {
            sprintf(fileline, "config: %s", CONFIG);
            perror(fileline);
            break;
        }
        str_tokenize(fileline, configblock, strlen(fileline));
        // note: VSIZE and HSIZE can be present in any order in the
        // configuration file, so we keep reading it until the
        // dimensions are set (or there are no more lines to read)
       
        // printf("%s -->", com_tok[0]);
        // printf("rhost is now: %s\n", rhost);
        if (strcmp(configblock[0], "VSIZE") == 0 && atol(configblock[1]) > 0)
            vsize = atol(configblock[1]);
        else if (strcmp(configblock[0], "HSIZE") == 0 && atol(configblock[1]) > 0)
            hsize = atol(configblock[1]);
        else if (strcmp(configblock[0], "RHOST") == 0 && strlen(configblock[1]) > 0 ){
            // printf("host parse at RHOST: %s\n", com_tok[1]);
            // printf("port parse at RHOST: %s\n", rport);
            strncpy(host, configblock[1], 127);
            rhost = host;
            // printf("rhost is now (inside): %s\n", rhost);
        }
        else if (strcmp(configblock[0], "RPORT") == 0 && atol(configblock[1]) > 0){
            rport = atol(configblock[1]);
            
            // printf("host parse at RPORT: %s\n", rhost);
            // printf("port parse at RPORT: %s\n", rport);
    
        } 
            
        // lines that do not make sense are hereby ignored
    }
    close(confd);

    if (hsize <= 0) {
        // invalid horizontal size, will use defaults (and write them
        // in the configuration file)
        hsize = 75;
        confd = open(CONFIG, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        write(confd, "HSIZE 75\n", strlen( "HSIZE 75\n"));
        close(confd);
        fprintf(stderr, "%s: cannot obtain a valid horizontal terminal size, will use the default\n", 
                CONFIG);
    }
    if (vsize <= 0) {
        // invalid vertical size, will use defaults (and write them in
        // the configuration file)
        vsize = 40;
        confd = open(CONFIG, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        write(confd, "VSIZE 40\n", strlen( "VSIZE 40\n"));
        close(confd);
        fprintf(stderr, "%s: cannot obtain a valid vertical terminal size, will use the default\n",
                CONFIG);
    }
    if (rhost == 0) {
        // host name by degault
        rhost = (char *)"10.18.0.22";
        confd = open (CONFIG, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR| S_IWUSR);
        write(confd, "RHOST 10.18.0.22\n", strlen("RHOST 10.18.0.22\n") );
        close(confd);
        fprintf(stderr, "%s: cannot obtain a hostname server, will use the default\n",
                CONFIG);

    }
    if (rport == 0) {
        // port number by default to hostname       
        rport = 9001;
        confd = open (CONFIG, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR| S_IWUSR);
        write(confd, "RPORT 9001\n", strlen("RPORT 9001\n") );
        close(confd);
        fprintf(stderr, "%s: cannot obtain a port number to server, will use the default\n",
                CONFIG);

    }

    printf("Terminal set to %ux%u. Shell is connected to %s at port number %u.\n", (unsigned int)hsize, (unsigned int)vsize, rhost, (unsigned int) rport);
    
    // All green, return ok.
    return 0;
}

int readCommand(char *command){
    
    printf("%s",PROMPT);
    fflush(stdin);
    if (fgets(command, 128, stdin) == 0) {
        // EOF, will quit
        printf("\nBye\n");
        return 0;
    }
    // fgets includes the newline in the buffer, get rid of it
    if(strlen(command) > 0 && command[strlen(command) - 1] == '\n')
        command[strlen(command) - 1] = '\0';

    return 1;
}

void remotiseCommand(char* command, char* rcmd){

    
}

void printSocketError(int errorcode){
    switch (errorcode) {
        case -1 : printf ( "Connection failed: error in obtaining host address\n"); break; 
        case -2 : printf ( "Connection failed: error in creating socket\n"); break; 
        case -3 : printf ( "Connection failed at connect\n"); break;  
        case -4 : printf ( "Connection failed: no port found for the specified service\n"); break;  
        default : printf ( "connectbyport failure\n"); break;  
    }
}

void remoteProcessing(char * rcmd){

    // If socket is unavailable, create a new socket
    if (g_socket < 0){
		g_socket = connectbyportint(rhost, rport);	// printf("rhost:%s", rhost); printf("rport:%lu", rport); 
        
    }

    if (g_socket < 0){
        printSocketError(g_socket);
    }
    printf ("%d", g_socket);
//    else {//Connection is successful
//        printf("\nBackground connected to %s on port %u.\n", rhost, (unsigned int) rport);
//    }
    
    // Send command and print response
    int returnflag = communication(rcmd); // printf("returnflag: %d \n", returnflag);
    if ( returnflag == 0 ) {
        printf("Background connection is finished.\n");
    }
	else if ( returnflag == 3 ) {
        printf("Background connection is finished.\n");
    }
    else
        perror("Background communication error");   

    //  socket is reset to -1 by communication(rcmd); if not kept alive
}

int main (int argc, char** argv, char** envp) {

 //   char host[128];      // copy data from com_tok to host string (isolating the data)
    char command[129];   // buffer for commands
    command[128] = '\0';
    char* com_tok[129];  // buffer for the tokenized commands
 //   size_t num_tok;      // number of tokens
    char rcmd[129];      //buffer for remote cmd
    rcmd[128] = '\0';
    size_t num_tok;

    // Welcome message
    printf("Simple shell with client version v1.0.\n");

    // read configuration file and setup parameters
    setupParameter();

    // install the typical signal handler for zombie cleanup
    // (we will inhibit it later when we need a different behaviour,
    // see run_it)
    signal(SIGCHLD, zombie_reaper);

    // Command loop:
    while(1) {
        int bg = 0;
        int local = 0;

        // Read input:
        if (readCommand(command)==0) return 0;
		if (strncmp(command,"! ", 2) != 0){         // if local command, skip this step
			memset(&rcmd, 0, sizeof(rcmd));             //reset rcmd
			if (strncmp(command,"& ", 2) == 0 && keepalive = 0){             //remove & and put on remote cmd
				strncpy(rcmd, command + 2, sizeof(rcmd));
			}
			else{
				strncpy(rcmd, command, sizeof(rcmd));
			}
		} 
        // tokenize local command and decide local and background flag
        // Tokenize input:
        char** real_com = com_tok;  // may need to skip the first
                                    // token, so we use a pointer to
                                    // access the tokenized command
        num_tok = str_tokenize(command, real_com, strlen(command));
        real_com[num_tok] = 0;      // null termination for execve

        
        if (strcmp(com_tok[0], "!") == 0){ //local commands
#ifdef DEBUG
            fprintf(stderr, "%s: local command\n", __FILE__);
#endif
            local = 1;
            //discard the first token which means local access
            real_com = com_tok + 1;

        }


        if (strcmp(real_com[0], "&") == 0) { // background command coming
#ifdef DEBUG
            fprintf(stderr, "%s: background command\n", __FILE__);
#endif
            bg = 1;
            // discard the first token now that we know that it
            // specifies a background process...
            
            real_com = real_com + 1;  
        }
            
        // Process input:
        if (strlen(real_com[0]) == 0) // no command, luser just pressed return
            continue;    
        if (local){

            if (strcmp(real_com[0], "exit") == 0) {
                printf("Bye\n");
                return 0;
            }
            else if (strcmp(real_com[0], "keepalive") == 0){
                ka_flag = 1;
                printf("Keepalive mode ON\n");
            }
            else if (strcmp(real_com[0], "close") == 0){
                ka_flag = 0;
                shutdown(g_socket, SHUT_RDWR);   //no more sends or receive
                close(g_socket);
                g_socket = -9;                     // make it so that we do not open the connection unless keepalive is back
                printf("Keepalive mode OFF\n");
            }
            else if (strcmp(real_com[0], "more") == 0) {
                // note: more never goes into background (any prefixing
                // `&' is ignored)
                if (real_com[1] == 0)
                    printf("more: too few arguments\n");
                // list all the files given in the command line arguments
                for (size_t i = 1; real_com[i] != 0; i++) 
                    do_more(real_com[i], hsize, vsize);
            }
            else { // external command
                if (bg) {  // background command, we fork a process that
                        // awaits for its completion
                    int bgp = fork();
                    if (bgp == 0) { // child executing the command
                        int r = run_it(real_com[0], real_com, envp, PATH);
                        printf("& %s done (%d)\n", real_com[0], WEXITSTATUS(r));
                        if (r != 0) {
                            printf("& %s completed with a non-null exit code\n", real_com[0]);
                        }
                        return 0;
                    }
                    else  // parent goes ahead and accepts the next command
                        continue;
                }
                else {  // foreground command, we execute it and wait for completion
                    int r = run_it(real_com[0], real_com, envp, PATH);
                    if (r != 0) {
                        printf("%s completed with a non-null exit code (%d)\n", real_com[0], WEXITSTATUS(r));
                    }
                }
            }
        }
        else { // Remote command process
            // Process remote command ONLY when it's remote command
            //transfer command into the remote string (rcmd)
			// printf("rcmd: %s\n", rcmd);printf("command: %s\n", command);
            if (bg){
                int bg_localp = fork();
                if (bg_localp == -1){
                    perror("Remote background process failed:");
                }
                else if (bg_localp == 0){
                    remoteProcessing(rcmd);           
                }
                else{
                    continue;   // return to main
                }
            }
            else { // Forefront processing
                remoteProcessing(rcmd);
					if (strncmp(rcmd,"quit", 4) == 0 || strncmp(rcmd,"Quit", 4) == 0 || strncmp(rcmd,"QUIT", 4) == 0 ){         // if local command, skip this step
						g_socket = -9; //reset g_socket for new remote connection
					}
            }
        }
    }
}
