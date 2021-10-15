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
const char* path[] = {"/bin","/usr/bin",0}; // path, null terminated (static)
const char* prompt = "sshell> ";            // prompt
const char* config = "shconfig";            // configuration file
const int ALEN = 256;                       // the length of each  TCP reply
int ka_flag = 0;            //keep alive flag to prevent double connection
int ka_sd = -1;             // keep alive unique socket (set it negative)

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

int communication(int socket, char* commandline){
        //communicating with server
        char ans[ALEN];             //to catch the reply
        int ans_count = 0;

        // printf("Sending: %s\n",real_com[0]);
        
        send(socket, commandline, strlen(commandline), 0);               
        send(socket,"\n",1,0);
        if (ka_flag == 0){
            shutdown(socket, SHUT_WR);
        }
        printf("***inside communication ka_flag, sd:%d, %d\n", ka_flag, socket);
        //collect the reply
        while( ( ans_count = recv_nonblock(socket, ans, ALEN-1, 5000)) != recv_nodata){     //5000ms timeout
            if (ans_count == 0){
                if (ka_flag == 0){          //for independent remote command
                    shutdown(socket, SHUT_RDWR);   //no more sends or receive
                    close(socket);
                    printf("Connection is closed\n");
                }
                return 0;
            }
            if (ans_count < 0){
                perror("recv_noblock: ");    //something went wrong with receiving data
                shutdown(socket, SHUT_WR);        //block all bad data
                close(socket);
                return 1;
            }
            ans[ans_count] = '\0';                  // end of the data string 
            printf("%s", ans);
            fflush(stdout);
        }
}



int main (int argc, char** argv, char** envp) {
    size_t hsize = 0, vsize = 0, rport = 0;  // terminal dimensions, read from and port number
    char* rhost = 0;                    //machine name
                                      // the config file
    char host[128];      // copy data from com_tok to host string (isolating the data)
    char command[129];   // buffer for commands
    command[128] = '\0';
    char* com_tok[129];  // buffer for the tokenized commands
    size_t num_tok;      // number of tokens
    char rcmd[129];      //buffer for remote cmd
    rcmd[128] = '\0';


    printf("Simple shell with client version v1.0.\n");

    // Config:
    int confd = open(config, O_RDONLY);
    if (confd < 0) {
        perror("config");
        fprintf(stderr, "config: cannot open the configuration file.\n");
        fprintf(stderr, "config: will now attempt to create one.\n");
        confd = open(config, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        if (confd < 0) {
            // cannot open the file, giving up.
            perror(config);
            fprintf(stderr, "config: giving up...\n");
            return 1;
        }
        close(confd);
        // re-open the file read-only
        confd = open(config, O_RDONLY);
        if (confd < 0) {
            // cannot open the file, we'll probably never reach this
            // one but who knows...
            perror(config);
            fprintf(stderr, "config: giving up...\n");
            return 1;
        }
    }

    // read terminal size
    while (hsize == 0 || vsize == 0 || rhost == 0 || rport == 0) {
        int n = readline(confd, command, 128);
        if (n == recv_nodata)
            break;
        if (n < 0) {
            sprintf(command, "config: %s", config);
            perror(command);
            break;
        }
        num_tok = str_tokenize(command, com_tok, strlen(command));
        // note: VSIZE and HSIZE can be present in any order in the
        // configuration file, so we keep reading it until the
        // dimensions are set (or there are no more lines to read)
       
        // printf("%s -->", com_tok[0]);
        // printf("rhost is now: %s\n", rhost);
        if (strcmp(com_tok[0], "VSIZE") == 0 && atol(com_tok[1]) > 0)
            vsize = atol(com_tok[1]);
        else if (strcmp(com_tok[0], "HSIZE") == 0 && atol(com_tok[1]) > 0)
            hsize = atol(com_tok[1]);
        else if (strcmp(com_tok[0], "RHOST") == 0 && strlen(com_tok[1]) > 0 ){
            // printf("host parse at RHOST: %s\n", com_tok[1]);
            // printf("port parse at RHOST: %s\n", rport);
            strncpy(host, com_tok[1], 127);
            rhost = host;
            // printf("rhost is now (inside): %s\n", rhost);
        }
        else if (strcmp(com_tok[0], "RPORT") == 0 && atol(com_tok[1]) > 0){
            rport = atol(com_tok[1]);
            
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
        confd = open(config, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        write(confd, "HSIZE 75\n", strlen( "HSIZE 75\n"));
        close(confd);
        fprintf(stderr, "%s: cannot obtain a valid horizontal terminal size, will use the default\n", 
                config);
    }
    if (vsize <= 0) {
        // invalid vertical size, will use defaults (and write them in
        // the configuration file)
        vsize = 40;
        confd = open(config, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        write(confd, "VSIZE 40\n", strlen( "VSIZE 40\n"));
        close(confd);
        fprintf(stderr, "%s: cannot obtain a valid vertical terminal size, will use the default\n",
                config);
    }
    if (rhost == 0) {
        // host name by degault
        rhost = (char *)"10.18.0.22";
        confd = open (config, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR| S_IWUSR);
        write(confd, "RHOST 10.18.0.22\n", strlen("RHOST 10.18.0.22\n") );
        close(confd);
        fprintf(stderr, "%s: cannot obtain a hostname server, will use the default\n",
                config);

    }
    if (rport == 0) {
        // port number by default to hostname       
        rport = 9001;
        confd = open (config, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR| S_IWUSR);
        write(confd, "RPORT 9001\n", strlen("RPORT 9001\n") );
        close(confd);
        fprintf(stderr, "%s: cannot obtain a port number to server, will use the default\n",
                config);

    }

    printf("Terminal set to %ux%u. Shell is connected to %s at port number %u.\n", (unsigned int)hsize, (unsigned int)vsize, rhost, (unsigned int) rport);

    // install the typical signal handler for zombie cleanup
    // (we will inhibit it later when we need a different behaviour,
    // see run_it)
    signal(SIGCHLD, zombie_reaper);

    // Command loop:
    while(1) {
        int bg = 0;
        int local = 0;

        // Read input:
        printf("%s",prompt);
        fflush(stdin);
        if (fgets(command, 128, stdin) == 0) {
            // EOF, will quit
            printf("\nBye\n");
            return 0;
        }
        // fgets includes the newline in the buffer, get rid of it
        if(strlen(command) > 0 && command[strlen(command) - 1] == '\n')
            command[strlen(command) - 1] = '\0';

       
        //transfer command into the remote string (rcmd)
        if (strncmp(command,"! ", 2) != 0){         // if local command, skip this step
            memset(&rcmd, 0, sizeof(rcmd));             //reset rcmd
            if (strncmp(command,"& ", 2) == 0 && ka_flag == 0){             //remove & and put on remote cmd
                strncpy(rcmd, command + 2, sizeof(rcmd));
            }
            else{                                                       // keepalive + & : do not change into background... it is the same connection
                strncpy(rcmd, command, sizeof(rcmd));
            }
        }        

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

        // ASSERT: num_tok > 0

        // Process input:
        if (strlen(real_com[0]) == 0) // no command, luser just pressed return
            continue;

        if (local == 0 ){               // TCP client stuff -- do not forget local ==0 bg =1
            
            if (bg && ka_flag == 0){
                int bg_localp = fork();
                if (bg_localp == -1){
                        perror("local background process failed:");
                }
                if (bg_localp == 0){
                        //CREATE A CONNECTION WITH A SOCKET
                    int sd = connectbyportint(rhost, rport);       
                    if (sd < 0){
                        if (sd == -1){
                            printf("Connection failed: error in obtaining host address\n");
                            continue;
                        }else if (sd == -2){
                            printf("Connection failed: error in creating socket\n");
                            continue;
                        }else if (sd ==-3){
                            printf("Connection failed at connect\n");
                            continue;
                        }else if (sd == -4){
                            printf("Connection failed: no port found for the specified service\n");
                            continue;
                        }else{
                            perror("connectbyport failure: ");          // in case it give a negative number we do not know of
                            continue;                                   // or break if connection failed from shell?????
                        }
                    }
                    printf("\nBackground connected to %s on port %u.\n", rhost, (unsigned int) rport);    //Connection is successful

                    if (communication(sd, rcmd) == 0 ) {
                        printf("Background connection is finished.\n");
                    }
                    else
                        perror("Background communication error");
                    
                    return 0;
                }
                else{
                    continue;   // return to main
                }
            }
            else{ //foreground
                printf("----ka_flag:%d, ka_sd:%d\n", ka_flag, ka_sd);
                if (ka_flag == 1 && ka_sd < 0){                 // reconnecting the ka_sd
                    ka_sd = connectbyportint(rhost, rport); 
                    if (ka_sd < 0){
                        if (ka_sd == -1){
                            printf("Connection failed: error in obtaining host address\n");
                            continue;
                        }else if (ka_sd == -2){
                            printf("Connection failed: error in creating socket\n");
                            continue;
                        }else if (ka_sd ==-3){
                            printf("Connection failed at connect\n");
                            continue;
                        }else if (ka_sd == -4){
                            printf("Connection failed: no port found for the specified service\n");
                            continue;
                        }else{
                            perror("connectbyport failure: ");          // in case it give a negative number we do not know of
                            continue;                                   // or break if connection failed from shell?????
                        }
                    }
                    printf("...Reconnecting keepalive socket\n");
                }

                if (ka_flag == 1){
                    int i = communication(ka_sd, rcmd);
                    if ( i != 0){
                        printf("%d", i);
                        printf("Communication error foreground_ka\n"); 
                    }
                }

                //CREATE A CONNECTION WITH A SOCKET
                if (ka_flag == 0){
                    int sd = connectbyportint(rhost, rport);       
                    if (sd < 0){
                        if (sd == -1){
                            printf("Connection failed: error in obtaining host address\n");
                            continue;
                        }else if (sd == -2){
                            printf("Connection failed: error in creating socket\n");
                            continue;
                        }else if (sd ==-3){
                            printf("Connection failed at connect\n");
                            continue;
                        }else if (sd == -4){
                            printf("Connection failed: no port found for the specified service\n");
                            continue;
                        }else{
                            perror("connectbyport failure: ");          // in case it give a negative number we do not know of
                            continue;                                   // or break if connection failed from shell?????
                        }
                    }
                    printf("Connected to %s on port %u.\n", rhost, (unsigned int) rport);    //Connection is successful

                    if (communication(sd, rcmd) != 0)
                        printf("Communication error foreground\n");
                }
                
            }
               
        }
        else{   //local commands
            if (strcmp(real_com[0], "exit") == 0) {
                printf("Bye\n");
                return 0;
            }
            else if (strcmp(real_com[0], "keepalive") == 0){
                ka_flag = 1;
                printf("ka_sd: %d\n", ka_sd);
                if (ka_sd >= 0){
                    printf("Keepalive failed: connection is already present\n");
                    continue;
                }
                ka_sd = connectbyportint(rhost, rport);       
                    if (ka_sd < 0){
                        if (ka_sd == -1){
                            printf("Connection failed: error in obtaining host address\n");
                            continue;
                        }else if (ka_sd == -2){
                            printf("Connection failed: error in creating socket\n");
                            continue;
                        }else if (ka_sd ==-3){
                            printf("Connection failed at connect\n");
                            continue;
                        }else if (ka_sd == -4){
                            printf("Connection failed: no port found for the specified service\n");
                            continue;
                        }else{
                            perror("connectbyport failure: ");          // in case it give a negative number we do not know of
                            continue;                                   // or break if connection failed from shell?????
                        }
                    }
                printf("Keepalive: connected to %s on port %u.\n", rhost, (unsigned int) rport);
                printf("ka_sd: %d\n", ka_sd);
            }
            else if (strcmp(real_com[0], "close") == 0){
                printf("ka_sd: %d\n", ka_sd);
                ka_flag = 0;
                close(ka_sd);
                ka_sd = -1;                     // make it so that we do not open the connection unless keepalive is back
                printf("ka_sd: %d\n", ka_sd);
                printf("Keepalive connection is closed\n");
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
                        int r = run_it(real_com[0], real_com, envp, path);
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
                    int r = run_it(real_com[0], real_com, envp, path);
                    if (r != 0) {
                        printf("%s completed with a non-null exit code (%d)\n", real_com[0], WEXITSTATUS(r));
                    }
                }
            }
        } // end of else local commands
       
    }//end of while
}
