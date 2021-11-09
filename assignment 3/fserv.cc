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
int create_file(char* file_name);
void add_trailing_spaces(char *dest, int size, int num_of_spaces);
int initiate_descriptor();
int write_descriptor(int pid, char file_name[80],int file_desc,int deldes=0);
int check_descriptor(char file_name[80]);
int delete_descriptor(int file_desc);
int clear_descriptor();

#define PORT_NUMBER 28648;
#define QLENGTH 32;
#define FILE_QUANTITY 10;
pthread_mutex_t lock;

struct rwexcl_t {
    pthread_mutex_t mutex; //mutex for the whole structure
    pthread_cond_t can_write; //condition variable, name says it all
    unsigned int reads; //number of simultaneous rads ( a write process should wait until this number is 0)
    unsigned int owners; //how many clients have the file opened
    int fd; //the file descriptor (also used as file id for the clients)

    char* name; // the (absolue) name of the file
};



int main (int argc, char** argv)
{
  //for loop create fileArray
	// fileArray[writeAddr].name= (char *) malloc(80);
  int port = PORT_NUMBER;
  int qlen = QLENGTH;

  long int msock, ssock;

  struct sockaddr_in client_addr;
  unsigned int client_addr_len = sizeof(client_addr);

  clear_descriptor();
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
    int n,fp;
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
                  int chkfd = check_descriptor(com_tok[1]); //check if the file is open per file_table. If zero, not open.
                  if(chkfd==0) //file doesn't appear in file_table
                  {
                    fp = create_file(com_tok[1]); //BUG need to judge if the file exists in the file system
                    int wd = write_descriptor(getpid(),com_tok[1],fp); //write file information to file_table
                    if(fp!=-1 && wd!=-1)
                    {
                      snprintf(ack1, sizeof ack1,"%s %d\n", ackOK,fp);
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
                    snprintf(ack1, sizeof ack1,"%s %d\n", ackERR,chkfd);
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
                int skrt = lseek(atoi(com_tok[1]), atoi(com_tok[2]), SEEK_CUR);
                if(skrt==-1)
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackERR,ENOENT);
                  send(sd,ack1,strlen(ack1),0);
                }
                else
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackOK,0);
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

                char rdbyts[1000];
                int rdrt = read(atoi(com_tok[1]), rdbyts,atoi(com_tok[2]));
                if(rdrt==-1)
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackERR,-1);
                  send(sd,ack1,strlen(ack1),0);
                }
                else
                {
                  sprintf(ack1,"%s %d %s\n", ackOK,rdrt,rdbyts);
                  send(sd,ack1,strlen(ack1),0);
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
          { // ADD condition to check if the file can be written

                int wtrt = write(atoi(com_tok[1]),com_tok[2],strlen(com_tok[2]));
                if(wtrt==-1)
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackERR,-1);
                  send(sd,ack1,strlen(ack1),0);
                }
                else
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackOK,0);
                  send(sd,ack1,strlen(ack1),0);
                }
            }
        }
        else if(strcmp(com_tok[0],"FCLOSE")==0)
        {
          if(num_tok!=3)
          {
            snprintf(ack1, sizeof ack1,"%s %d Improper FCLOSE.\nFCLOSE format -> FCLOSE identifier \n", ackFAIL,errno);
            send(sd,ack1,strlen(ack1),0);
          }
          else
          { // check if someone else is reading/writing the file 

                //close(atoi(com_tok[1]));
                int ddrt = delete_descriptor(atoi(com_tok[1]));
                if(ddrt==0)
                {
                  close(atoi(com_tok[1]));
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackOK,ddrt);
                  send(sd,ack1,strlen(ack1),0);
                }
                else
                {
                  snprintf(ack1, sizeof ack1,"%s %d\n", ackERR,ddrt);
                  send(sd,ack1,strlen(ack1),0);
                }
          }

        } //Add compare if QUIT
        else
        {
          send(sd,ack,strlen(ack),0);
          send(sd,"I hope that's not a valid command for me.",strlen("I hope that's not a valid command for me."),0);
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


int create_file(char* file_name)
{
	//This function handles user-program-file-descriptor-table.
	int fp;

	//cooking the file path and name
	char *path;
	path = (char*) malloc(50*sizeof(char));
	strcat(path,"");
	strcat(path,file_name);
	printf("-----> File %s created for client",path);

	//creating the file
	fp = open(path,O_RDWR | O_CREAT,S_IRWXU);  //Actually, we don't need the path. We can just create a file with filename.

  if(fp!=-1)
  {
    return fp;
  }
  else
  {
    return -1;
  }

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
  int fd = open("file_table",O_RDWR | O_CREAT,S_IRWXU);
  close(fd);
  if(fd!=-1)
  {
    return 0;
  }
  else
  {
    return -1;
  }
}

int write_descriptor(int pid, char file_name[80],int file_desc,int deldes=0) //Rewrite
{
    int writeAddr = -1;
    for (int i = 0; i < FILE_QUANTITY; i++) {
	if (fileArray[i].fd == -1)
	{
	writeAddr = i;
	break;
	}
    }
    if (writeAddr == -1) return -1;
    //else	
    fileArray[writeAddr].mutex = ?;//mutex for the whole structure
    fileArray[writeAddr].can_write = ?; //condition variable, name says it all
    fileArray[writeAddr].reads = 0; //number of simultaneous rads ( a write process should wait until this number is 0)
    fileArray[writeAddr].owners = 1; //how many clients have the file opened
    fileArray[writeAddr].fd = file_desc; //the file descriptor (also used as file id for the clients)
    strcpy(fileArray[writeAddr].name, file_name); // the (absolue) name of the file
	
    return 0;
}

int check_descriptor(char file_name[80])
{
    for (int i = 0; i < FILE_QUANTITY; i++) {
	if (fileArray[i].fd == -1) continue;
	if (strcmp(fileArray[i].name, file_name) == 0)
	{
	    return fileArray[i].fd;
	}
    }
    return -1;
}

int delete_descriptor(int file_desc)
{
    for (int i = 0; i < FILE_QUANTITY; i++) {
        if (fileArray[i].fd == file_desc){
	    fileArray[i].fd = -1;
	    return 0;
	}
    }
    return -1;
}
