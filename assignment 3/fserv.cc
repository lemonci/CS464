#include <sys/wait.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <iostream>
#include "tokenize.h"
#include "tcp-utils.h"

void* do_client_f (int sd);
void add_trailing_spaces(char *dest, int size, int num_of_spaces);
int initiate_descriptor();
int write_descriptor(int pid, char*,FILE* file_desc,int deldes);
int check_descriptor(char*);
int delete_descriptor(int file_desc);
int clear_descriptor();

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

struct cmdArgs{
  bool delay = false;
};
struct cmdArgs cmd;
int main (int argc, char** argv)
{
  int port = PORT_NUMBER;
  int qlen = QLENGTH;

  long int msock, ssock;

  struct sockaddr_in client_addr;
  unsigned int client_addr_len = sizeof(client_addr);

  initiate_descriptor();

  msock = passivesocket(port,qlen);
  if (msock < 0)
  {
      perror("passivesocket");
      return 1;
  }
  printf("Server up and listening on port %d.\n", port);

  pthread_t tt;
  pthread_attr_t ta;
  pthread_attr_init(&ta);
  pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);


  while (1)
  {
    ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
    if (ssock < 0) {
        if (errno == EINTR) continue;
        perror("accept");
        return 1;
    }

    if (pthread_create(&tt, &ta, (void* (*) (void*))do_client_f, (void*)ssock) != 0 )
    {
        perror("pthread_create");
        return 1;
    }
}
return 0;
}


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
                        if (cmd.delay == true) sleep(5);
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
                        
                        if (cmd.delay == true) sleep(5);
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
                    if (cmd.delay == true) sleep(5);
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
