
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

void* do_client_f (int sd);
void add_trailing_spaces(char *dest, int size, int num_of_spaces);
int initiate_descriptor();
int write_descriptor(int pid, char*,int file_desc,int deldes);
int check_descriptor(char*);
int delete_descriptor(int file_desc);
int clear_descriptor();
