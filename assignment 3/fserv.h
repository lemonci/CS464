
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

void* do_client_f (int);
void add_trailing_spaces(char *, int, int);
int initiate_descriptor(void);
int write_descriptor(int, char*,FILE*,int);
int check_descriptor(char *);
int create_file(char*);
int delete_descriptor(int);
int clear_descriptor(void);
