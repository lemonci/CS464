/*
 *Assignment 4 with 
 *Part of the solution for Assignment 3, by Stefan Bruda.
 *
 * This file contains the code for the file server.
 */

#include "shfd.h"

/*
 * The access control structure for the opened files (initialized in
 * the main function), and its size.
 */
rwexcl_t** flocks;
size_t flocks_size;
struct readMajority{
    char ans_read[MAX_LEN];
    int counts;
};
const int ALEN = 1024;
// Using DUP2 no longer rely on file descriptor map
// int fd_array[11][200] = {0};

// Using a global variable to record the smallest free file descriptor
int g_free_fd = 1000;

//timeout? keep things in individual thread?

/*
 * Handles the FOPEN command.
 *
 * Allocates the access control structure, initializes the mutex and
 * condition for the file given as argument, and opens the file for
 * both reading and writing.  Returns the file descriptor or a
 * negative number in case of failure.
 */
int file_init (const char* filename) {
    char msg[MAX_LEN];  // logger string
    snprintf(msg, MAX_LEN, "%s: attempting to open %s\n", __FILE__, filename);
    logger(msg);

    // at this file should be guaranteed not to be opened already
    int fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if ( fd == -1 ) {
        snprintf(msg, MAX_LEN, "%s: open error: %s\n", __FILE__, strerror(errno));
        logger(msg);
        return -1;
    }

    // now that we have a file we allocate and initialize the access
    // control structure:
    rwexcl_t* lck = new rwexcl_t;
    pthread_mutex_init(&lck -> mutex, 0);
    pthread_cond_init(&lck -> can_write,0);
    lck -> reads = 0;
    lck -> fd = fd;
    lck -> owners = 1;

    struct stat fs;
    int ret = fstat (fd, &fs);

    if (ret < 0) {
        // We are not supposed to see this, but it may conceivably happen I guess
        snprintf(msg, MAX_LEN, "%s: cannot stat %s (descriptor %d)\n", __FILE__, filename, fd);
        logger(msg);
        lck -> inode = 0;
    }
    else
        lck -> inode = fs.st_ino;

    lck -> name = new char[strlen(filename) + 1];
    strcpy(lck -> name, filename);

    flocks[fd] = lck;
    snprintf(msg, MAX_LEN, "%s: %s opened on descriptor %d\n", __FILE__, filename, fd);
    logger(msg);
    return fd;
}

/*
 * Handles the FCLOSE command.
 *
 * Physically closes the file iff the file has only one owner.
 */
int file_exit (int fd) {
    char msg[MAX_LEN];  // logger string
    snprintf(msg, MAX_LEN, "%s: attempting to close descriptor %d\n", __FILE__, fd);
    logger(msg);

    if (flocks[fd] == 0)
        return err_nofile;

    // close is a major event, we treat it as a write.
    // so we wait for everybody to finish working with the file
    pthread_mutex_lock(&flocks[fd] -> mutex);
    while (flocks[fd] -> reads != 0) {
        // release the mutex while waiting...
        pthread_cond_wait(&flocks[fd] -> can_write, &flocks[fd] -> mutex);
    }
    // now we have the mutex _and_ the condition, so we process the
    // close event

    flocks[fd] -> owners --;
    if ( flocks[fd] -> owners != 0) {
        snprintf(msg, MAX_LEN, "%s: descriptor %d owned by other clients\n", __FILE__, fd);
        logger(msg);
        // this might have released the file for access, so we
        // broadcast the condition to unlock whatever process happens
        // to wait for us.
        pthread_cond_broadcast(&flocks[fd] -> can_write);
        pthread_mutex_unlock(&flocks[fd] -> mutex);
        return 0;
    }

    // no other client is accessing this file, we destroy the
    // descriptor

    int closed = close(flocks[fd] -> fd);
    delete[] flocks[fd] -> name;
    delete flocks[fd];
    flocks[fd] = 0;
    snprintf(msg, MAX_LEN, "%s: descriptor %d closed\n", __FILE__, fd);
    logger(msg);

    // Note: mutex destroyed, no need to unlock
    return closed;
}

/*
 * Handles the FWRITE command.
 *
 * Ensures exclusive access to given file descriptor using the
 * associated rwexcl_t structure, and writes `stuff_length' bytes from
 * `stuff'.  Returns the number of bytes actually written,
 * `err_nofile' if the file descriptor is invalid, and -1 on error
 * conditions.
 */
int write_excl(int fd, const char* stuff, size_t stuff_length) {
    int result;
    char msg[MAX_LEN];  // logger string

    if (flocks[fd] == 0)
        return err_nofile;

    // The fact that only one thread writes at any given time is ensured
    // by the fact that the successful thread does not release the mutex
    // until the writing process is done.
    pthread_mutex_lock(&flocks[fd] -> mutex);
    // we wait for condition as long as somebody is doing things with
    // the file
    while (flocks[fd] -> reads != 0) {
        // release the mutex while waiting...
        pthread_cond_wait(&flocks[fd] -> can_write, &flocks[fd] -> mutex);
    }
    // now we have the mutex _and_ the condition, so we write

    if (debugs[DEBUG_DELAY]) {
        // ******************** TEST CODE ******************** 
        // bring the write process to a crawl to figure out whether we
        // implement the concurrent access correctly.
        snprintf(msg, MAX_LEN, "%s: debug write delay 5 seconds begins\n", __FILE__);
        logger(msg);
        sleep(5);
        snprintf(msg, MAX_LEN, "%s: debug write delay 5 seconds ends\n", __FILE__);
        logger(msg);
        // ******************** TEST CODE DONE *************** 
    } /* DEBUG_DELAY */

    result = write(fd,stuff,stuff_length);
    if (result == -1) {
        snprintf(msg, MAX_LEN, "%s: write error: %s\n", __FILE__, strerror(errno));
        logger(msg);
    }
    
    // done writing.
    // this might have released the file for access, so we broadcast the
    // condition to unlock whatever process happens to wait for us.
    pthread_cond_broadcast(&flocks[fd] -> can_write);
    // we are done!
    pthread_mutex_unlock(&flocks[fd] -> mutex);
    return result;  // the count of bytes written
}

/*
 * Handles the FSEEK command.
 *
 * A seek affects them all, so we use the same mutual exclusion scheme
 * like the one for the write operation.  Other than this, the
 * function is just a wrapper for lseek.
 */
int seek_excl(int fd, off_t offset) {
    int result;
    char msg[MAX_LEN];  // logger string

    if (flocks[fd] == 0)
        return err_nofile;

    if (debugs[DEBUG_FILE]) {
        snprintf(msg, MAX_LEN, "%s: seek to %d into descriptor %d\n", __FILE__, (int)offset, fd);
        logger(msg);
    }

    // The fact that only one thread writes at any given time is ensured
    // by the fact that the successful thread does not release the mutex
    // until the seek process is done.
    pthread_mutex_lock(&flocks[fd] -> mutex);
    // we wait for condition as long as somebody is doing things with
    // the file
    while (flocks[fd] -> reads != 0) {
        // release the mutex while waiting...
        pthread_cond_wait(&flocks[fd] -> can_write, &flocks[fd] -> mutex);
    }
    // now we have the mutex _and_ the condition, so we change the offset

    result = lseek(fd, offset, SEEK_CUR);
    if (result == -1) {
        snprintf(msg, MAX_LEN, "%s: lseek error: %s\n", __FILE__, strerror(errno));
        logger(msg);
    }
    
    // this might have released the file for access, so we broadcast the
    // condition to unlock whatever process happens to wait for us.
    pthread_cond_broadcast(&flocks[fd] -> can_write);
    // we are done!
    pthread_mutex_unlock(&flocks[fd] -> mutex);
    return result;  // the new offset
}

/*
 * Handles the FREAD command.
 *
 * Waits based on the associated rwexcl_t structure until no write
 * operation happens on the file descriptor fd, and then reads
 * `stuff_length' bytes putting them into `stuff'.
 */
int read_excl(int fd, char* stuff, size_t stuff_length) {
    int result;
    char msg[MAX_LEN];  // logger string

    if (flocks[fd] == 0)
        return err_nofile;    

    if (debugs[DEBUG_FILE]) {
        snprintf(msg, MAX_LEN, "%s: read %lu bytes from descriptor %d\n", __FILE__, stuff_length, fd);
        logger(msg);
    }

    pthread_mutex_lock(&flocks[fd] -> mutex);
    // We increment the number of concurrent reads, so that a write
    // request knows to wait after us.
    flocks[fd] -> reads ++;
    pthread_mutex_unlock(&flocks[fd] -> mutex);

    // now we have the condition set, so we read (we do not need the
    // mutex, concurrent reads are fine):

    if (debugs[DEBUG_DELAY]) {
        // ******************** TEST CODE ******************** 
        // bring the read process to a crawl to figure out whether we
        // implement the concurrent access correctly.
        snprintf(msg, MAX_LEN, "%s: debug read delay 20 seconds begins\n", __FILE__);
        logger(msg);
        sleep(20);
        snprintf(msg, MAX_LEN, "%s: debug read delay 20 seconds ends\n", __FILE__);
        logger(msg);
        // ******************** TEST CODE DONE *************** 
    } /* DEBUG_DELAY */

    result = read(fd, stuff, stuff_length);

    // we are done with the file, we first signal the condition
    // variable and then we return.

    pthread_mutex_lock(&flocks[fd] -> mutex);
    flocks[fd] -> reads --;
    // this might have released the file for write access, so we
    // broadcast the condition to unlock whatever writing process
    // happens to wait after us.
    if (flocks[fd] -> reads == 0)
        pthread_cond_broadcast(&flocks[fd] -> can_write);
    pthread_mutex_unlock(&flocks[fd] -> mutex);

    return result;
}


/*
 * Client handler for the file server.  Receives the descriptor of the
 * communication socket.
 */
//void* file_client (client_t* clnt) {

void* file_client (struct socket_client *pack) {
    //int sd = clnt -> sd;
    //char* ip = clnt -> ip;
    char msg[MAX_LEN]; // logger string
    int sd;         //the thread = socket
    int peer_sd;
    char ip[20];       // ip = client_addr
    struct sockaddr_in client_addr;         //the address of the client....
    unsigned int client_addr_len = sizeof(client_addr);        // ... and its length
    int msock = pack->socket;
	int client = pack->client;
    snprintf(msg, MAX_LEN, "msock: %d, client: %d\n", msock, client);
    logger(msg);
    char req[MAX_LEN];  // current request
    int n;
    int poll_count = 0; //counter for each poll

    while (1){
        new_client:
        if (talive == false){
            pthread_mutex_lock(&thread_mutex);
            tdie = true;
            to_die = curr_threads;
            pthread_mutex_unlock(&thread_mutex);
            handle_threads();           //kill all idle threads
        }

        struct pollfd pollrec;
        pollrec.fd = msock;
        pollrec.events = POLLIN;
        int polled = poll(&pollrec,1,TIME_EVAL);

        if (polled < 0){                      //error
            if (errno == EINTR) continue;
            snprintf(msg, MAX_LEN, "%s: failed at handle thread poll: %s\n", __FILE__, strerror(errno));
            logger(msg);
            talive = false;
            break;
        }
        //to simulate more or sd than the 60 sec while checking the state of talive
        if (polled == 0 && poll_count < 20){    
            poll_count++;
            // snprintf(msg, MAX_LEN, "poll count = %d\n", poll_count);
            // logger(msg);
            continue;
        }

        if (polled  == 0 && poll_count == 20){                      // idling threads
            if (curr_threads <= incr_threads){
                poll_count = 0;
                continue;                 //if the idle threads is same as the initial threads... go back and poll again                 
            }
            else if ((curr_threads - incr_threads - 1) < act_threads){
                poll_count = 0;
                continue;
            }
            else if (tdie){          //if trigger have already start
                poll_count = 0;     // we don't need it since thread will die...just a precaution in case thread stay alive
                handle_threads();
            }
            else{           //we have too many idle batch of threads ---only need one time to trigger
                poll_count = 0; // we don't need it since thread will die...just a precaution in case thread stay alive
                pthread_mutex_lock(&thread_mutex);
                to_die = incr_threads;
                tdie = true;
                pthread_mutex_unlock(&thread_mutex);
                handle_threads();
                //that thread will die
            }
        }  


        //the thread goes to accept on polled == 1
        sd = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (sd < 0){
            if (errno == EINTR) continue;
            snprintf(msg, MAX_LEN, "%s: file server accept: %s\n", __FILE__, strerror(errno));
            logger(msg);
            snprintf(msg, MAX_LEN, "%s: the file server died.\n", __FILE__);
            logger(msg);
            return 0;
        }
        //thread has a client... status on thread change from idle to active
        pthread_mutex_lock(&thread_mutex);
        act_threads++;
        pthread_mutex_unlock(&thread_mutex);
        
        //if all the threads are active, we create new threads if possible
        if(act_threads == curr_threads){
            if (set_threads(pack) != 0){
                snprintf(msg, MAX_LEN, "%s: file client failed to make new threads from set_threads\n", __FILE__);
                logger(msg);
                snprintf(msg, MAX_LEN, "%s: the file client has died prematurely.\n", __FILE__);
                logger(msg);
                talive = false;             //to prevent further problem with threads we should exit file server...
                return 0;
            };
        }

        // assemble client coordinates (communication socket + IP)
        ip_to_dotted(client_addr.sin_addr.s_addr, ip);

        bool* opened_fds = new bool[flocks_size];
        for (size_t i = 0; i < flocks_size; i++)
            opened_fds[i] = false;

        // make sure req is null-terminated...
        req[MAX_LEN-1] = '\0';


        snprintf(msg, MAX_LEN, "%s: new client from %s assigned socket descriptor %d\n",
                __FILE__, ip, sd);
        logger(msg);
        snprintf(msg, MAX_LEN, 
                "Welcome to shfd v.1 [%s]. FOPEN FSEEK FREAD FWRITE FCLOSE QUIT spoken here.\r\n",
                ip);
        send(sd, msg, strlen(msg),0);


        // Loop while the client has something to say and talive is true...
        while (talive) {
            struct pollfd pollinfo;
            pollinfo.fd = sd;
            pollinfo.events = POLLIN;

            //BLOCK ON POLL instead on readline
            int polledinfo = poll(&pollinfo, 1, TIME_EVAL);

            if (polledinfo == 0){               //we wait and we check the talive status
                continue;
            }
            if (polledinfo < 0){                //we wait 
                if (errno == EINTR) continue;
                snprintf(msg, MAX_LEN, "%s: failed at handle thread poll: %s\n", __FILE__, strerror(errno));
                logger(msg);
                talive = false;
                break;
            }

            // POLLEDINFO == 1 ---> we go read the info received from 
            n = readline(sd,req,MAX_LEN-1);

            if (n == recv_nodata) break; 

            if (n == -1){
                snprintf(msg, MAX_LEN, "%s: error readline --> %s\n", __FILE__, strerror(errno));
                logger(msg);
                break;
            }

            //where n >= 0 (with or without data)
            char* ans = new char[MAX_LEN]; // the response sent back to the client
            // we allocate space for the answer dinamically because the
            // result of a FREAD command is virtually unbounded in size
            ans[MAX_LEN-1] = '\0';
            // If we talk to telnet, we get \r\n at the end of the line
            // instead of just \n, so we take care of the possible \r:
            if ( n > 1 && req[n-1] == '\r' ) 
                req[n-1] = '\0';
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: --> %s\n", __FILE__, req);
                logger(msg);
            } /* DEBUG_COMM */
            if ( strncasecmp(req,"QUIT",strlen("QUIT")) == 0 ) {  // we are done!
                snprintf(msg, MAX_LEN, "%s: QUIT received from client %d (%s), closing\n", __FILE__, sd, ip);
                logger(msg);
                if (debugs[DEBUG_COMM]) {
                    snprintf(msg, MAX_LEN, "%s: <-- OK 0 nice talking to you\n", __FILE__);
                    logger(msg);
                } /* DEBUG_COMM */
                send(sd,"OK 0 nice talking to you\r\n", strlen("OK 0 nice talking to you\r\n"),0);
                shutdown(sd, SHUT_RDWR);
                close(sd);
                delete[] ans;
                snprintf(msg, MAX_LEN, "%s: Attempting to close the files opened by this client\n", __FILE__);
                logger(msg);
                for (size_t i = 0; i < flocks_size; i++)
                    if (opened_fds[i])
                        file_exit(i);
                delete[] opened_fds;
                pthread_mutex_lock(&thread_mutex);
                act_threads--;
                pthread_mutex_unlock(&thread_mutex);
                //delete clnt;
                //return 0;
                goto new_client;                  //break from inner loop onto the next client
            }
            
            // ### COMMAND HANDLER ###

            // ### FOPEN ###
            else if (strncasecmp(req,"FOPEN",strlen("FOPEN")) == 0 ) {
                int idx = next_arg(req,' ');
                if (idx == -1 ) {
                    snprintf(ans,MAX_LEN,"FAIL %d FOPEN requires a file name", EBADMSG);
                }
                else { // we attempt to open the file
                    char filename[MAX_LEN];
                    // do we have a relative path?
                    // awkward test, do we have anything better?
                    if (req[idx] == '/') { // absolute
                    snprintf(filename, MAX_LEN, "%s", &req[idx]);
                    }
                    else { // relative
                        char cwd[MAX_LEN];
                        getcwd(cwd, MAX_LEN);
                        snprintf(filename, MAX_LEN, "%s/%s", cwd, &req[idx]);
                    }

                    // Improved file comparison (by inode rather than file name). Replaces (xx) below.

                    // open the file to obtain its inode:
                    int fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
                    // Note: we do not react to errors at this time since we will handle any 
                    // possible error when opening the file for real

                    int inode = -1;
                    struct stat fs;
                    int ret = fstat (fd, &fs);
                    if (ret >= 0)
                        inode = fs.st_ino;
                    close(fd);

                    // Look for a file with the same inode in flocks[]:
                    fd = -1;
                    for (size_t i = 0; i < flocks_size; i++) {
                        if (flocks[i] != 0 && (int)flocks[i] -> inode == inode) {
                            fd = i;
                            pthread_mutex_lock(&flocks[fd] -> mutex);
                            if (! opened_fds[fd])  // file already opened by the same client?
                                flocks[fd] -> owners ++;
                            pthread_mutex_unlock(&flocks[fd] -> mutex);
                            opened_fds[fd] = true;
                            break;
                        }
                    }

                    /*
                    // (xx) Old code for comparing files by file name, replaced by inode comparison (above)
                    int fd = -1;
                    for (size_t i = 0; i < flocks_size; i++) {
                        if (flocks[i] != 0 && strcmp(filename, flocks[i] -> name) == 0) {
                            fd = i;
                            pthread_mutex_lock(&flocks[fd] -> mutex);
                            if (! opened_fds[fd])  // file already opened by the same client?
                                flocks[fd] -> owners ++;
                            pthread_mutex_unlock(&flocks[fd] -> mutex);
                            opened_fds[fd] = true;
                            break;
                        }
                    }
                    */

                    if (fd >= 0) { // already opened
                        snprintf(ans, MAX_LEN,
                                "ERR %d file already opened, please use the supplied identifier", fd);
                    }
                    else { // we open the file anew
                        fd = file_init(filename);
                        if (fd < 0)
                            snprintf(ans, MAX_LEN, "FAIL %d %s", errno, strerror(errno));
                        else {
                            // replace fd with an artificial fd which is larger than 1000
                            // The artificial fd is used as a normal fd which is sent to the client 
                            // and recorded in the fd array, aka, opened_fds[]
                            fd = dup2(fd, g_free_fd);
                            // Move the last free fd to the next so that the artificial fd is not reused
                            g_free_fd++;
                            snprintf(ans, MAX_LEN, "OK %d file opened, please use supplied identifier", fd);
                            opened_fds[fd] = true;
                        }
                    }
                    
                }
                // send the request to peers //check the tokenized reconstruction??
                if (replica == 0) {
                    snprintf(msg, MAX_LEN, "No peers. fopen is completed\n");
                    logger(msg);
                    }
                    
                else { 
                    if(client == 1){
                    /*
					int* p = &fd_array[0][0];
					for (int i=0; i< 200;  i++){
						if (*p==0) break;
						p += 11;
                        */
					
                        for (int i=0; i< replica; i++){
                            peer_sd = connectbyportint(pserv[i].phost,pserv[i].pport);
                            if (peer_sd < 0){
                                snprintf(msg, MAX_LEN, "%s: peer_sd %d read not connect\n",__FILE__, i); 
                                logger(msg);
                                continue;
                            }
                            send(peer_sd,req,strlen(req),0);
                            send(peer_sd,"\n",1,0);
                            int a;
                            char* ans_peer = new char[MAX_LEN];
                            // receive response
                            while ((a = recv_nonblock(peer_sd,ans_peer,MAX_LEN-1,10)) >0 ) {
                                ans_peer[a] = '\0';
                                printf("ans:%s\n", ans_peer);
                                snprintf(msg, MAX_LEN, "ans from peer: %s\n", ans_peer); 
                                logger(msg);
                                // fflush(stdout);
                            }
                            shutdown(peer_sd, SHUT_RDWR);
                            close(peer_sd);
                            snprintf(msg, MAX_LEN, "Connection closed - %d \n", peer_sd);
                            logger(msg);
                        }
                    }
                }
            } // end FOPEN

            // ### FREAD ###
            else if (strncasecmp(req,"FREAD",strlen("FREAD")) == 0 ) {
                int idx = next_arg(req,' ');
                if (idx == -1) // no identifier
                    snprintf(ans,MAX_LEN,"FAIL %d FREAD requires a file identifier", EBADMSG);
                else {
                    int idx1 = next_arg(&req[idx],' ');
                    if (idx1 == -1) // no identifier
                        snprintf(ans,MAX_LEN,"FAIL %d FREAD requires a number of bytes to read", EBADMSG);
                    else {
                        idx1 = idx + idx1;
                        req[idx1 + 1] = '\0';
                        if (debugs[DEBUG_COMM]) {
                            snprintf(msg, MAX_LEN, "%s: (before decoding) will read %s bytes from %s \n",
                                    __FILE__, &req[idx1], &req[idx]); 
                            logger(msg);
                        }
                        idx = atoi(&req[idx]);  // get the identifier and length
                        idx1 = atoi(&req[idx1]);
                        if (debugs[DEBUG_COMM]) {
                            snprintf(msg, MAX_LEN, "%s: (after decoding) will read %d bytes from %d \n",
                                    __FILE__, idx1, idx); 
                            logger(msg);
                        }
                        if (idx <= 0 || idx1 <= 0)
                            snprintf(ans, MAX_LEN, "FAIL %d identifier and length must both be positive numbers", EBADMSG);
                        else { // now we can finally read the thing!
                            // read buffer
                            char* read_buff = new char[idx1+1];
                            int result = read_excl(idx, read_buff, idx1);
                            struct readMajority allAns[MAX_PEER];
                            memset(allAns, 0, MAX_PEER*sizeof(allAns[0]));
                            // ASSUMPTION: we never read null bytes from the file.
                            if (result == err_nofile) {
                                snprintf(ans, MAX_LEN, "FAIL %d bad file descriptor %d", EBADF, idx);
                            }
                            else if (result < 0) {
                                snprintf(ans, MAX_LEN, "FAIL %d %s", errno, strerror(errno));
                            }
                            else {
                                read_buff[result] = '\0';
                                // we may need to allocate a larger buffer
                                // besides the message, we give 40 characters to OK + number of bytes read.
                                delete[] ans;
                                ans = new char[40 + result];
                                strcpy(ans, read_buff);
                                snprintf(ans, MAX_LEN, "OK %d %s", result, read_buff);
                                //send FREAD request to peers
                                
                                for (int i =0; i< MAX_PEER; i++) 
                                    allAns[i].counts = 0;
                                strcpy(allAns[0].ans_read, ans);
                                allAns[0].counts ++;
                                for (int i=0; i< replica; i++){
                                    //connect to peer
                                    peer_sd = connectbyportint(pserv[i].phost,pserv[i].pport);
                                    if (peer_sd < 0){
                                        snprintf(msg, MAX_LEN, "%s: peer_sd %d read not connect\n",__FILE__, i); 
                                        logger(msg);
                                        continue;
                                    }
                                    printf("Connected to %s.\n", pserv[i].phost);
                                    send(peer_sd,req,strlen(req),0);
                                    send(peer_sd,"\n",1,0);
                                    int n;
                                    // receive response
                                    while ((n = recv_nonblock(peer_sd,ans,MAX_LEN-1,10)) >0 ) {
                                    
                                        /*if (n == 0) {
                                            shutdown(peer_sd, SHUT_RDWR);
                                            close(peer_sd);
                                            printf("Connection closed - %s", peer_sd);
                                            return 0; //?
                                        }*/ 
                                        //append it in the buffer
                                        ans[n] = '\0';
                                        printf(ans);
                                        fflush(stdout);
                                    }                          
                                    //store the response in an array
                                    for (int j=0; j<=replica; j++){
                                        if (strcmp(allAns[j].ans_read, ans) == 0){
                                            allAns[j].counts++;
                                        }
                                        else{
                                            for (int k=1; k<=replica; k++){
                                                if(allAns[k].counts == 0){
                                                    strcpy(allAns[k].ans_read, ans);
                                                    allAns[k].counts++;
                                                }
                                            }
                                        }
                                    }
                                    //close
                                    shutdown(peer_sd, SHUT_RDWR);
                                    close(peer_sd);
                                    printf("Connection closed - %d", peer_sd);
                                }
                                delete [] read_buff;
                            }
                            //compare to get the majority.
                            int max_count = 0;
                            int max_pos = 0;
                            for (int i = 0; i<MAX_PEER; i++){
                                if (max_count < allAns[i].counts){
                                    max_count = allAns[i].counts;
                                    max_pos = i;
                                }
                            }
                            //Judge whether to send majority or sync failure.
                            //send response to client
                            if (max_count*2 >= replica+1){ 
                                send(sd,allAns[max_pos].ans_read,strlen(allAns[max_pos].ans_read),0);
                            }else{ 
                                send(sd, "Sync failed.", strlen("Sync failed."), 0);
                            }
                            send(sd,"\n",1,0);
                        }
                    }
                }
            } // end FREAD

            // ### FWRITE ###
            else if (strncasecmp(req,"FWRITE",strlen("FWRITE")) == 0 ) {
                int idx = next_arg(req,' ');
                if (idx == -1) // no argument!
                    snprintf(ans,MAX_LEN,"ERROR %d FWRITE required a file identifier", EBADMSG);
                else {
                    int idx1 = next_arg(&req[idx],' ');
                    if (idx1 == -1) // no data to write
                        snprintf(ans,MAX_LEN,"FAIL %d FWRITE requires data to be written", EBADMSG);
                    else {
                        idx1 = idx1 + idx;
                        req[idx1 + 1] = '\0';
                        idx = atoi(&req[idx]);  // get the identifier and data
                        if (idx <= 0)
                            snprintf(ans,MAX_LEN,
                                    "FAIL %d identifier must be positive", EBADMSG);
                        else { // now we can finally write!
                            if (debugs[DEBUG_FILE]) {
                                snprintf(msg, MAX_LEN, "%s: will write %s\n", __FILE__, &req[idx1]);
                                logger(msg);
                            }
                            int result = write_excl(idx, &req[idx1], strlen(&req[idx1]));
                            
                            // send the request to peers //check the tokenized reconstruction??
                            if (replica == 0) {
                                snprintf(msg, MAX_LEN, "No peers. fwrite is completed\n");
                                logger(msg);
                                }
                            else{
                                for (int i=0; i< replica; i++){
                                    peer_sd = connectbyportint(pserv[i].phost,pserv[i].pport);
                                    send(peer_sd,req,strlen(req),0);
                                    send(peer_sd,"\n",1,0);
                                    shutdown(peer_sd, SHUT_RDWR);
                                    close(peer_sd);
                                    snprintf(msg, MAX_LEN, "Connection closed - %d \n", peer_sd);
                                    logger(msg);
                                }
                            }
                            
                            if (result == err_nofile)
                                snprintf(ans, MAX_LEN, "FAIL %d bad file descriptor %d", EBADF, idx);
                            else if (result < 0) {
                                snprintf(ans, MAX_LEN, "FAIL %d %s", errno, strerror(errno));
                            }
                            else {
                                snprintf(ans, MAX_LEN, "OK 0 wrote %d bytes", result);
                            }
                        }
                    }
                }
            } // end WRITE

            // ### FSEEK ###
            else if (strncasecmp(req,"FSEEK",strlen("FSEEK")) == 0 ) {  
                int idx = next_arg(req,' ');
                if (idx == -1) // no identifier
                    snprintf(ans,MAX_LEN,"FAIL %d FSEEK requires a file identifier", EBADMSG);
                else {
                    int idx1 = next_arg(&req[idx],' ');
                    if (idx1 == -1) // no identifier
                        snprintf(ans,MAX_LEN,"FAIL %d FSEEK requires an offset", EBADMSG);
                    else {
                        idx1 = idx1 + idx;
                        req[idx1 + 1] = '\0';
                        idx = atoi(&req[idx]);  // get the identifier and offset
                        idx1 = atoi(&req[idx1]);
                        if (idx <= 0)
                            snprintf(ans,MAX_LEN,
                                    "FAIL %d identifier must be positive", EBADMSG);
                        else { // now we can finally seek!
                            int result = seek_excl(idx, idx1);
                            if (result == err_nofile)
                                snprintf(ans, MAX_LEN, "FAIL %d bad file descriptor %d", EBADF, idx);
                            else if (result < 0) {
                                snprintf(ans, MAX_LEN, "FAIL %d %s", errno, strerror(errno));
                            }
                            else {
                                snprintf(ans, MAX_LEN, "OK 0 offset is now %d", result);
                            }
                        }
                    }
                }
                // send the request to peers //check the tokenized reconstruction??
                if (replica == 0) {
                    snprintf(msg, MAX_LEN, "No peers. fseek is completed\n");
                    logger(msg);
                }
                else if(client == 1){
                    for (int i=0; i< replica; i++){
                        peer_sd = connectbyportint(pserv[i].phost,pserv[i].pport);
                        send(peer_sd,req,strlen(req),0);
                        send(peer_sd,"\n",1,0);
                        shutdown(peer_sd, SHUT_RDWR);
                        close(peer_sd);
                        snprintf(msg, MAX_LEN, "Connection closed - %d \n", peer_sd);
                        logger(msg);
                    }
                }
            } // end FSEEK

            // ### FCLOSE ###
            else if (strncasecmp(req,"FCLOSE",strlen("FCLOSE")) == 0 ) {  
                int idx = next_arg(req,' ');
                if (idx == -1) // no identifier
                    snprintf(ans,MAX_LEN,"FAIL %d FCLOSE requires a file identifier", EBADMSG);
                else {
                    idx = atoi(&req[idx]);  // get the identifier and offset
                    if (idx <= 0)
                        snprintf(ans,MAX_LEN,
                                "FAIL %d identifier must be positive", EBADMSG);
                    else { // now we can finally close!
                        int result = file_exit(idx);
                        opened_fds[idx] = false;
                        if (result == err_nofile)
                            snprintf(ans, MAX_LEN, "FAIL %d bad file descriptor %d", EBADF, idx);
                        else if (result < 0) {
                            snprintf(ans, MAX_LEN, "FAIL %d %s", errno, strerror(errno));
                        }
                        else {
                            snprintf(ans, MAX_LEN, "OK 0 file closed");
                        }
                    }
                }
                // send the request to peers //check the tokenized reconstruction??
                if (replica == 0) {
                    snprintf(msg, MAX_LEN, "No peers. fclose is completed\n");
                    logger(msg);
                    }
                    
                else if(client == 1){
                    for (int i=0; i< replica; i++){
                        peer_sd = connectbyportint(pserv[i].phost,pserv[i].pport);
                        send(peer_sd,req,strlen(req),0);
                        send(peer_sd,"\n",1,0);
                        shutdown(peer_sd, SHUT_RDWR);
                        close(peer_sd);
                        snprintf(msg, MAX_LEN, "Connection closed - %d \n", peer_sd);
                        logger(msg);
                    }
                }
            } // end FCLOSE

            // ### UNKNOWN COMMAND ###
            else {
                int idx = next_arg(req,' ');
                if ( idx == 0 )
                    idx = next_arg(req,' ');
                if (idx != -1)
                    req[idx-1] = '\0';
                snprintf(ans,MAX_LEN,"FAIL %d %s not understood", EBADMSG, req);
            }

            // ### END OF COMMAND HANDLER ###

            // Effectively send the answer back to the client:
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                logger(msg);
            } /* DEBUG_COMM */
            send(sd,ans,strlen(ans),0);
            send(sd,"\r\n",2,0);        // telnet expects \r\n
            delete[] ans;
        } // end of main for file command loop.

        // read 0 bytes = EOF:
        if (!talive && !reboot){
            send(sd,"FAIL 256 Sorry.Server is shutting down bye!\r\n", strlen("FAIL 256 Sorry.Server is shutting down bye!\r\n"),0);
        }
        if (!talive && reboot){
            send(sd,"FAIL 256 Sorry.Server is Hanging Up. Bye!\r\n", strlen("FAIL 256 Sorry.Server is Hanging Up. Bye!\r\n"),0);
        }
        snprintf(msg, MAX_LEN, "%s: client on socket descriptor %d (%s) went away, closing\n",
                __FILE__, sd, ip);
        logger(msg);

        shutdown(sd, SHUT_RDWR);
        close(sd);
    
        for (size_t i = 0; i < flocks_size; i++)
            if (opened_fds[i])
                file_exit(i);
        delete[] opened_fds;
        
        //this thread has finished its client ... now going to a new one 
        pthread_mutex_lock(&thread_mutex);
        act_threads--;
        pthread_mutex_unlock(&thread_mutex);
        //delete clnt;

    } //end of the while loop of the client socket

    return 0; // it will never reach
}
