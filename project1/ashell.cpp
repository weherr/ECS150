
#include "ashell.h"

using namespace std;


char RXChar;
string command;
char back = '\b';
char space = ' ';
char newlin = '\n';
char reline = '\r';
char bell = '\a';
char buf[256];
char greater_sign = '>';
char percent_sign = '%';
char left_bracket = '[';
char slash = '/';

int command_length;
int delete_count;

vector<string> past_commands;
int past_commands_index = 0;
string lastArrow;

vector< Process > allProcesses;
vector<string> subCommand;

vector<int> allPipes;



void ResetCanonicalMode(int fd, struct termios *savedattributes){
    tcsetattr(fd, TCSANOW, savedattributes);
}

void SetNonCanonicalMode(int fd, struct termios *savedattributes){
    struct termios TermAttributes;
    char *name;
    
    // Make sure stdin is a terminal. 
    if(!isatty(fd)){
        fprintf (stderr, "Not a terminal.\n");
        exit(0);
    }
    
    // Save the terminal attributes so we can restore them later. 
    tcgetattr(fd, savedattributes);
    
    // Set the funny terminal modes. 
    tcgetattr (fd, &TermAttributes);
    TermAttributes.c_lflag &= ~(ICANON | ECHO); // Clear ICANON and ECHO. 
    TermAttributes.c_cc[VMIN] = 1;
    TermAttributes.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &TermAttributes);
}

int execute(Process p);
void display();
void ff(string dirName);
void parseCommand();


int main(int argc, char *argv[]){
    struct termios SavedTermAttributes;
    //char RXChar;
    
    SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);

    display();
    
    while(1){
        read(STDIN_FILENO, &RXChar, 1);
        if(0x04 == RXChar){ // quit
            write(STDIN_FILENO, &newlin, 1);
            break;
        }
        else{

            if(isprint(RXChar)){
                write(STDIN_FILENO, &RXChar, 1); // Write from STDIN to RXChar
                command += RXChar;               // Add RXChar to command
                command_length += 1;             // Increment command length
                delete_count += 1;               // Increment delete_count
                //printf("RX: '%c' 0x%02X\n",RXChar, RXChar);   
            }
            else{

                //check for arrow keys first
                if(RXChar == 0x1B){
                    read(STDIN_FILENO, &RXChar, 1);
                    if(RXChar == 0x5B){
                        read(STDIN_FILENO, &RXChar, 1);

                        // UP ARROW
                        if(RXChar == 0x41){
                            if(past_commands.size() > 0 && past_commands_index < past_commands.size()){

                                if(lastArrow == "down"){
                                    past_commands_index++;
                                }

                                lastArrow = "up";
                                string replacementCommand = past_commands.at(past_commands_index);
                                past_commands_index++;

                                //delete old command
                                while(command_length != 0){
                                    write(STDIN_FILENO, &back, 1);
                                    write(STDIN_FILENO, &space, 1);
                                    write(STDIN_FILENO, &back, 1);
                                    command_length--;
                                }

                                command = replacementCommand;
                                command_length = command.size();
                                delete_count = command.size();

                                //print new command
                                for (int i = 0; i <= command.size(); i++) { 
                                  char x = command[i];
                                  write(STDIN_FILENO, &x, 1);
                                }
                                
                            }
                            else{
                                write(STDOUT_FILENO, &bell, 1);
                            }
                        }


                        // DOWN ARROW
                        else if (RXChar == 0x42){
                            if(past_commands_index == 1){
                                while(command_length != 0){
                                    write(STDIN_FILENO, &back, 1);
                                    write(STDIN_FILENO, &space, 1);
                                    write(STDIN_FILENO, &back, 1);
                                    command_length--;
                                } // while
                                past_commands_index = 0;
                                delete_count = 0;
                                command = "";

                            }
                            else if(past_commands_index > 0 && past_commands_index <= 10){
                                past_commands_index--;

                                // DEAL with switching arrows
                                if(lastArrow == "up")
                                    past_commands_index--;
                                lastArrow = "down";


                                string replacementCommand = past_commands.at(past_commands_index);
                                //delete old command
                                while(command_length != 0){
                                    write(STDIN_FILENO, &back, 1);
                                    write(STDIN_FILENO, &space, 1);
                                    write(STDIN_FILENO, &back, 1);
                                    command_length--;
                                } // while

                                command = replacementCommand;
                                command_length = command.size();
                                delete_count = command.size();

                                //print new command
                                for (int i = 0; i <= command.size(); i++) { 
                                  char x = command[i];
                                  write(STDIN_FILENO, &x, 1);
                                } // for


                            } //if 

                            else{
                                write(STDOUT_FILENO, &bell, 1);
                            }
                        }
                    }
                }

                // if enter
                if(RXChar == 0x0A){
                    // check for exit in main
                    if(command == "exit"){
                        write(STDOUT_FILENO, &newlin, 1);
                        ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                        exit(1);
                    }

                    // add new commnad to past commands vector
                    // If more than 10 items delete last thing
                    past_commands.insert(past_commands.begin(), command);
                    if(past_commands.size() > 10){
                        past_commands.pop_back();
                    }

                    write(STDOUT_FILENO, &newlin, 1);
                    parseCommand();

                    // Check for cd in main because no need to fork
                    if(subCommand[0] == "cd" && subCommand.size() == 1){
                        string h;
                        h = getenv("HOME");
                        chdir(&h[0]);
                    } // cd home

                    else if(subCommand[0] == "cd"){
                        int lastIdx = command.size();
                        string dir = command.substr(3,lastIdx-1);
                        memset(buf, 0, sizeof(buf));
                        getcwd(buf, sizeof(buf));
                        string dirnew = string(buf);

                        if(subCommand[1] == ".."){
                            int found;
                            found = dirnew.find_last_of("/");
                            dirnew = dirnew.substr(0,found);           
                        }
                        else{
                            dirnew = dirnew + '/' +dir;
                        }

                        string direrr = "No such file or directory";
                        if(chdir(&dirnew[0])!= 0){
                            write(STDOUT_FILENO,&direrr[0],direrr.size());
                            write(STDOUT_FILENO, &newlin, 1);
                        }
                    } // cd everything else


                    else{

                        bool needPipe = false;

                        //previous output
                        int pfd[2];
                        pid_t pid;
                        int commIndex = 0;
                        int pre = 0;
                        int i = 0;

                        for(int x=0; x< allProcesses.size(); x++){
                            int fdIn, fdOut;

                            if(allProcesses[x].input != ""){
                                char* arg =  (char*)malloc(allProcesses[x].input.size() * sizeof(char));
                                strcpy(arg, allProcesses[x].input.c_str());

                                fdIn = open(arg, O_RDONLY);  
                            }

                            if(allProcesses[x].output != ""){
                                char* arg =  (char*)malloc(allProcesses[x].output.size() * sizeof(char));
                                strcpy(arg, allProcesses[x].output.c_str());

                                fdOut = open(arg, O_CREAT | O_RDWR | O_TRUNC, S_IRWXU);
                            }

                            if(allProcesses.size() > 1){
                                pipe(pfd);
                                needPipe = true;
                            }

                            pid_t pid;
                            pid = fork();
                            if(pid == 0){
                                if(allProcesses[x].input != "")
                                    dup2(fdIn, 0);
                                if(allProcesses[x].output != "")
                                    dup2(fdOut, 1);
                                if(needPipe)
                                    dup2(pre, 0);

                                if(x+1 < allProcesses.size())
                                    dup2(pfd[1], 1);

                               // close(pfd[0]);


                                if(execute(allProcesses[x]) == -1)
                                    perror("could not execute");
           
                                exit(0);

                            }
                            if(allProcesses[x].input != "")
                                close(fdIn);
                            if(allProcesses[x].output != "")
                                close(fdOut);

                            //close(pfd[1]);
                            wait(NULL);

                            pre = pfd[0];

                        }
                    }

                    delete_count = 0;
                    command = "";
                    command_length = 0;
                    past_commands_index = 0;
                    subCommand.clear();
                    allProcesses.clear();
                    display();
                }

                // Backspace characters
                else if (RXChar == 0x7F || RXChar == 0x08 || RXChar == 0x7E || RXChar == 0x33){
                    if (delete_count > 0) {
                    write(STDIN_FILENO, &back, 1);
                    write(STDIN_FILENO, &space, 1);
                    write(STDIN_FILENO, &back, 1);
                    command = command.substr(0, command.size() - 1);
                    command_length -= 1;
                    delete_count -= 1;
                    }
                }
                //printf("RX: ' ' 0x%02X\n",RXChar);
            }
        }
    }
    
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    return 0;
}


int execute(Process p){
    if (p.subCommand[0] == "pwd") {
        memset(buf, 0, sizeof(buf));
        getcwd(buf, sizeof(buf));
        write(STDOUT_FILENO, &buf, sizeof(buf));
        write(STDOUT_FILENO, &newlin, 1);
        return 0;
    } // pwd()

    else if(p.subCommand[0] == "ls"){
        memset(buf, 0, sizeof(buf));
        getcwd(buf, sizeof(buf));

        if(p.subCommand.size() > 1){
            string direrr = "No such file or directory";
            if(chdir(&p.subCommand[1][0])!= 0){
                write(STDOUT_FILENO,&direrr[0],direrr.size());
                write(STDOUT_FILENO, &newlin, 1);
                return 0;
            }
        }

        struct stat stats;

        string t = "./";
        DIR* dict = opendir(&t[0]);
        dirent* f;
        while(f = readdir(dict)){

            if (stat(f->d_name, &stats) == -1)
                perror(f->d_name);

            else{

                int mode = stats.st_mode;       // Set mode equal to mode bits for current file
                char modes[12] = "---------- "; // The mode bit string

                if (S_ISDIR(mode) ) modes[0] = 'd'; // If directory
                //Users
                if (mode & S_IRUSR) modes[1] = 'r';
                if (mode & S_IWUSR) modes[2] = 'w';
                if (mode & S_IXUSR) modes[3] = 'x'; 
                //Group
                if (mode & S_IRGRP) modes[4] = 'r';
                if (mode & S_IWGRP) modes[5] = 'w'; 
                if (mode & S_IXGRP) modes[6] = 'x'; 
                // Others
                if (mode & S_IROTH) modes[7] = 'r'; 
                if (mode & S_IWOTH) modes[8] = 'w'; 
                if (mode & S_IXOTH) modes[9] = 'x';


                write(STDOUT_FILENO, modes, 12);
                write(STDOUT_FILENO, f->d_name, strlen(f->d_name));
                write(STDOUT_FILENO, &newlin, 1);
            }
        }
        closedir(dict);

        chdir(&buf[0]);
        return 0;
    } // ls

    else if(p.subCommand[0] == "ff"){
        if(p.subCommand.size() == 1){
            string ffError = "ff command requires a filename!";
            write(STDOUT_FILENO, &ffError[0], ffError.size());
            write(STDOUT_FILENO, &newlin, 1);
            return 0;
        }
        else{
            memset(buf, 0, sizeof(buf));
            getcwd(buf, sizeof(buf));
            string bufHold = buf;
            //cout << bufHold << endl;
            if(p.subCommand.size() > 2){
                    string direrr = "No such file or directory";
                    if(chdir(&p.subCommand[2][0])!= 0){
                        write(STDOUT_FILENO,&direrr[0],direrr.size());
                        write(STDOUT_FILENO, &newlin, 1);
                        return 0;
                    }
            }
            memset(buf, 0, sizeof(buf));
            getcwd(buf, sizeof(buf));
            ff(buf);
            chdir(&bufHold[0]);
        }
        return 0;
    } // ff

    else{

        char* path = &p.subCommand[0][0];

        char** arg = (char**)malloc(p.subCommand.size() * sizeof(char*));
        for(int i = 0; i < p.subCommand.size(); i++){
        		 arg[i] =  (char*)malloc(p.subCommand[i].size() * sizeof(char));
        		 strcpy(arg[i], p.subCommand[i].c_str());
        }
		 execvp(path,arg);

    }

}


void display() {
    int num_char = 0;
    int last_slash;
    memset(buf, 0, sizeof(buf));
    getcwd(buf, sizeof(buf));
    for (int x = 0; x <= sizeof(buf); x++) {
        if (buf[x] == '/')
            last_slash = x;
        if (isprint(buf[x]))
            num_char += 1;
    }
    if (num_char <= 16) {
        write(STDOUT_FILENO, &buf, sizeof(buf));
        write(STDOUT_FILENO, &percent_sign, 1);
        write(STDOUT_FILENO, &space, 1);
    }
    else {
        int count = 256 - last_slash;
        write(STDOUT_FILENO, "/...", 4);
        write(STDOUT_FILENO, &buf[last_slash], count);
        write(STDOUT_FILENO, &percent_sign, 1);
        write(STDOUT_FILENO, &space, 1);
    }
}




void parseCommand(){
    int index = 0;
    Process first;
    allProcesses.push_back(first);

    // Creates remianing processes not including the first one
    while(index < command.size()){
        if(command[index] == '|'){
            Process x;
            allProcesses.push_back(x);
        }
        index++;
    }

    int whichProcess = 0;
    index = 0;
    string temp;
    int tempIndex;

    while(index < command.size()){
         while(command[index] != ' ' && index < command.size()){
            if(command[index] == '|'){
                allProcesses[whichProcess].setsubCommand(subCommand);
                whichProcess++;
                index++;
                subCommand.clear();
            }

            else if(command[index] == '<'){
                string in;
                index++;
                while(command[index] == ' ')
                    index++;
                while(command[index] != ' ' && index < command.size()){
                    in += command[index];
                    index++;
                }
                allProcesses[whichProcess].setInput(in);
            }

            else if(command[index] == '>'){
                string out;
                index++;
                while(command[index] == ' ')
                    index++;
                while(command[index] != ' ' && index < command.size()){
                    out += command[index];
                    index++;
                }
                allProcesses[whichProcess].setOutput(out);
            }

            else{
                temp += command[index];
                index++;
            }
       }       
       index++;
       if(temp!= "")
           subCommand.push_back(temp);
       temp = "";
    }
    allProcesses[whichProcess].setsubCommand(subCommand);


    // FOR TESTING THE PARSER
    // for(int i=0; i< allProcesses.size(); i++){
    //     for(int x=0; x< allProcesses[i].subCommand.size(); x++){
    //         cout << allProcesses[i].subCommand[x] << " ";
    //     }

    //     cout << endl;
    //     cout << allProcesses[i].input << " " << allProcesses[i].output << endl;
    // }


}

void ff(string dirName){

	struct stat stats;

    string t = "./";
    DIR* dict = opendir(&t[0]);
    dirent* f;
    while(f = readdir(dict)){

        if (stat(f->d_name, &stats) == -1)
            perror(f->d_name);
        else{
            string dirNameHold = dirName;

    		int mode = stats.st_mode;
    		if (S_ISDIR(mode)){
                if(f->d_name[0] == '.'){
                    continue;
                }

    			dirNameHold += '/';
    			dirNameHold += f->d_name;
                chdir(&dirNameHold[0]);
    			ff(dirNameHold);
    		}
            else{
                if(f->d_name == subCommand[1]){

                    write(STDOUT_FILENO, &dirName[0], dirName.size());
                    write(STDOUT_FILENO, &slash, 1);
                    write(STDOUT_FILENO, &f->d_name[0], strlen(f->d_name));
                    write(STDOUT_FILENO, &newlin, 1);
                }
            }
    	}
	}
    closedir(dict);
    chdir("..");
}






