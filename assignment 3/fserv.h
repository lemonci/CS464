#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>

void* do_client_f (int);