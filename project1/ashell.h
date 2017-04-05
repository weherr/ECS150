#ifndef _ASHELL_H
#define _ASHELL_H
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <bitset>
#include <string.h>
#include <dirent.h>
#include <vector>
#include <iostream>
#include <fcntl.h>

class Process {
public:
	std::string input;     // Where to take input form
	std::string output;    // Where to output to
	std::vector<std::string> subCommand;

	bool pipeIn;
	bool pipeOut;

	int pipeInFd;
	int pipeOutFd;

	Process(){
		input = "";
		output = "";
		pipeIn = false;
		pipeOut = false;
		pipeInFd = -1;
		pipeOutFd = -1;

	}
	~Process() {}

	void setInput(std::string in){
		input = in;
	}

	void setOutput(std::string out){
		output = out;
	}

	void setsubCommand(std::vector<std::string> x){
		subCommand = x;
	}
};

#endif
