#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fstream>
#include <iostream>
using namespace std;

string s = "tititoto";
if (s.rfind("! ", 0) == 0) { // pos=0 limits the search to the prefix
  // s starts with prefix
}