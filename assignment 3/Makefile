## Assignment 3 - Shell & File Server shell (CS 464/564)
## by Wing Shu Leung & Monica Li
CXX = g++
CC = g++
CXXFLAGS = -g -Wall -Werror -pedantic -ansi -std=c++11
LDFLAGS = -lpthread

all: shfd

tcp-utils.o: tcp-utils.h tcp-utils.cc
	$(CXX) $(CXXFLAGS) -c -o tcp-utils.o tcp-utils.cc

tokenize.o: tokenize.h tokenize.cc
	$(CXX) $(CXXFLAGS) -c -o tokenize.o tokenize.cc

fserv.o: fserv.h fserv.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -c -o fserv.o fserv.cc

shellserv.o: fserv.h tcp-utils.h tokenize.h shellserv.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -c -o shellserv.o shellserv.cc

shfd: shellserv.o fserv.o tcp-utils.o tokenize.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o shfd shellserv.o fserv.o tcp-utils.o tokenize.o

clean:
	rm -f shfd *~ *.o *.bak core \#* shfd.log