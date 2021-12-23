# Notes for reading Bruda's solution - Assignment 3
## Misc.cc
### Linked files
library: shfd.h \
output file: shfd.log, shfd.pid
### global variable
bool falive: flag for keep file server alive, true if the file server is alive (and kicking).
pthread_mutex_t logger_mutex: \
extern char **environ: ? \
bool debugs[3] = {false, false, false}: What to debug (nothing by default):
### functions
void logger(const char * msg): use mutex lock to do printf
void ip_to_dotted(unsigned int ip, char* buffer):  Simple conversion of IP addresses from unsigned int to dotted notation. \
int next_arg(const char* line, char delim): look for delimiter (or for the end of line, whichever happens first), if at the end of line, return -1 (no argument), otherwise return the index of the next delimiter. \
void* file_server (int msock): \
void* shell_server (int msock): \
int main (int argc, char** argv, char** envp) :
