#include<stdio.h>
#include<string.h>
#include<stdlib.h>  // for exit(), free()
#include<unistd.h>  // for fork(), getcwd(), chdir(), execvp(), pipe(), dup2()
#include<sys/wait.h>  // for waitpid()
#include<signal.h>   // signal
#include<fcntl.h>   // for open(), close()

#define MAX_COMMANDS 100
#define MAX_ARGS 100

char *arguments[MAX_ARGS];     // stores the arguments of the current command

int parseInput(char *command) {   // parses one command into multiple arguments
    char *token;
    int argc=0;

    while((token=strsep(&command," \t"))!=NULL) {  // separation of the command using spaces and tabs

         if(strlen(token)==0) {    // to ignore empty tokens caused by multiple spaces
            continue;
         }

         if(argc<MAX_ARGS-1) {   // stores the argument
            arguments[argc++]=token;
         }
    }

    arguments[argc]=NULL;   // 'NULL' acts a terminator to help execvp()
    return argc;
}

void executeCD() {   // changes the working directory of the shell

    if(arguments[1]==NULL || arguments[2]!=NULL) {      // cd must have exactly one argument
        printf("Shell: Incorrect command\n");
        return;
    }

    if(chdir(arguments[1])!=0) {    // changes the working directory of the shell
        printf("Shell: Incorrect command\n");
    }
}

void executeCommand(char *command) {   // to execute a command
    pid_t pid;
    int argc;

    argc=parseInput(command);   // to parse the command

    if(argc==0) {    // empty command
        return;
    }

    if(strcmp(arguments[0],"cd")==0) {   // to execute the cd command
        executeCD();
        return;
    }

    pid=fork();    // create a child process;

    if(pid<0) {
        printf("Shell: Incorrect command\n");
        return;
    }

    if(pid==0) {    //this is the child process
        signal(SIGINT,SIG_DFL);     // restore default signal handling in the child
        signal(SIGTSTP,SIG_DFL);

        execvp(arguments[0],arguments);   // execute the command
        printf("Shell: Incorrect command\n");   // prints this only if execution fails
        exit(1);
    }

    waitpid(pid,NULL,WUNTRACED);   // parent waits for the child
}

void executeParallelCommands(char *input) {  // execute commands separated by && in parallel
    char *command;
    char *operatorPosition;
    pid_t pids[MAX_COMMANDS];
    int processCnt=0;
    int argc, i;

    while(input!=NULL && strlen(input)>0) {

        operatorPosition=strstr(input,"&&");

        if(operatorPosition!=NULL) {
            *operatorPosition='\0';
            *(operatorPosition+1)='\0';
            command=input;
            input=operatorPosition+2;
        }
        else {
            command=input;
            input=NULL;
        }

        argc=parseInput(command);

        if(argc==0)
            continue;

        if(strcmp(arguments[0],"cd")==0) {
            executeCD();
            continue;
        }

        if(processCnt >= MAX_COMMANDS)
            break;

        pids[processCnt]=fork();

        if(pids[processCnt]<0) {
            printf("Shell: Incorrect command\n");
            return;
        }

        if(pids[processCnt]==0) {
            signal(SIGINT,SIG_DFL);
            signal(SIGTSTP,SIG_DFL);

            execvp(arguments[0],arguments);

            printf("Shell: Incorrect command\n");
            exit(1);
        }

        processCnt++;
    }

    //Wait for all normal commands after all have been started
    for(i=0;i<processCnt;i++)
        waitpid(pids[i],NULL,WUNTRACED);
}

void executeSequentialCommands(char *input) {  // execute commands separated by ## sequentially
    char *command;
    char *operatorPosition;
    int argc;
    int length;

    while(input!=NULL && strlen(input)>0) {

        operatorPosition=strstr(input,"##");

        if(operatorPosition!=NULL) {
            *operatorPosition='\0';
            *(operatorPosition+1)='\0';
            command=input;
            input=operatorPosition+2;
        }
        else {
            command=input;
            input=NULL;
        }

        /* Remove leading spaces and tabs. */
        while(*command==' ' || *command=='\t')
            command++;

        /* Remove trailing spaces and tabs. */
        length=strlen(command);
        while(length>0 && (command[length-1]==' ' || command[length-1]=='\t')) {
            command[length-1]='\0';
            length--;
        }

        if(strlen(command)==0)
            continue;

        /*
         * Parse the command so cd can be handled directly by the shell.
         */
        argc=parseInput(command);

        if(argc==0)
            continue;

        if(strcmp(arguments[0],"cd")==0) {
            executeCD();
        }
        else {
            executeCommand(command);
        }
    }
}

void executeCommandRedirection(char *input) {   // execute a command with stdout redirected
    char *command;
    char *fileName;
    char *operatorPosition;
    pid_t pid;
    int fd;
    int argc;
    int length;

    operatorPosition=strchr(input,'>');

    if(operatorPosition==NULL) {
        printf("Shell: Incorrect command\n");
        return;
    }

    *operatorPosition='\0';
    command=input;
    fileName=operatorPosition+1;

    /* Remove leading spaces and tabs from filename. */
    while(*fileName==' ' || *fileName=='\t')
        fileName++;

    /* Remove trailing spaces and tabs from filename. */
    length=strlen(fileName);
    while(length>0 && (fileName[length-1]==' ' || fileName[length-1]=='\t')) {
        fileName[length-1]='\0';
        length--;
    }

    if(strlen(fileName)==0) {
        printf("Shell: Incorrect command\n");
        return;
    }

    argc=parseInput(command);

    if(argc==0) {
        printf("Shell: Incorrect command\n");
        return;
    }

    /* cd cannot be redirected because it is a shell built-in. */
    if(strcmp(arguments[0],"cd")==0) {
        printf("Shell: Incorrect command\n");
        return;
    }

    pid=fork();

    if(pid<0) {
        printf("Shell: Incorrect command\n");
        return;
    }

    if(pid==0) {
        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);

        fd=open(fileName,O_WRONLY | O_CREAT | O_TRUNC,0644);

        if(fd<0) {
            printf("Shell: Incorrect command\n");
            exit(1);
        }

        dup2(fd,STDOUT_FILENO);
        close(fd);

        execvp(arguments[0],arguments);

        printf("Shell: Incorrect command\n");
        exit(1);
    }

    waitpid(pid,NULL,WUNTRACED);
}

void executePipeline(char *input) {   // to execute commands connected using pipes
    char *commands[MAX_COMMANDS];
    char *token;
    int commandCnt=0;
    int i;

    int pipefd[2];
    int previousPipe=-1;

    pid_t pid;
    pid_t pids[MAX_COMMANDS];

    while((token=strsep(&input,"|"))!=NULL) {    // splits the input into individual commands

        commands[commandCnt]=token;
        commandCnt++;

        if(commandCnt>=MAX_COMMANDS) {
            break;
        }
    }

    if(commandCnt<2) {     // a pipeline must contain atleast two commands
        printf("Shell: Incorrect command\n");
        return;
    }

    for(i=0;i<commandCnt;i++) {    // to check that no command in the pipeline is empty
        char *temp=commands[i];

        while(*temp==' ' || *temp=='\t') {
            temp++;
        }

        if(*temp=='\0') {
            printf("Shell: Incorrect command\n");
            return;
        }
    }

    for(i=0;i<commandCnt;i++) {   // create one process for every command

        if(i<commandCnt-1) {   // create a pipe for every command except the last

            if(pipe(pipefd)==-1) {
                printf("Shell: Incorrect command\n");
                return;
            }
        }

        pid=fork();     // creates a child process

        if(pid<0) {
            printf("Shell: Incorrect command\n");
            return;
        }

        if(pid==0) {
            signal(SIGINT,SIG_DFL);    // these both signals make the child react normally to Ctrl+C and Ctrl+Z
            signal(SIGTSTP,SIG_DFL);

            if(previousPipe!=-1) {   // if there is a previous pipe, use it as standard input
                dup2(previousPipe,STDIN_FILENO); 
                close(previousPipe);
            }

            if(i<commandCnt-1) {    // if the current command is not the last command, redirect stdout to the new pipe
                close(pipefd[0]);
                dup2(pipefd[1],STDOUT_FILENO);
                close(pipefd[1]);
            }

            parseInput(commands[i]);    // parse the command into arguments

            execvp(arguments[0],arguments);     // execute the command
            printf("Shell: Incorrect command\n");    // if execution fails
            exit(1);
        }

        pids[i]=pid;      // store the child PID

        if(previousPipe!=-1) {   // parent no longer needs the previous pipe
            close(previousPipe);
            previousPipe=-1;
        }

        if(i<commandCnt-1) {  // save the read end of the current pipe and the next command will use it as stdin
            close(pipefd[1]);
            previousPipe=pipefd[0];
        }
    }

    for(i=0;i<commandCnt;i++) {      // wait for all the pipeline processes
        waitpid(pids[i],NULL,WUNTRACED);
    }

    if(previousPipe!=-1) {    // close any remaining pipe
        close(previousPipe);
    }
}

int main() {
    char *input=NULL;      // pointer used by getline() to store the input line

    size_t inputSize=0;    // getline() uses this variable to store the allocated size
    ssize_t inputLength;    // stores the number of characters read by getline()

    char currentWorkingDirectory[1024];   // stores the current working directory

    signal(SIGINT,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);

    while(1) {    // Infinite loop. The shell exits only when we enter "exit"

        if(getcwd(currentWorkingDirectory,sizeof(currentWorkingDirectory))==NULL) {
            printf("Shell: Incorrect command\n");
            continue;
        }

        printf("%s$",currentWorkingDirectory);   // prints the prompt exactly as required
        fflush(stdout);     // makes sure the prompt is displayed immediately

        inputLength=getline(&input,&inputSize,stdin);   // reading the input using getline()

        if(inputLength==-1) {  // stop safely if EOF is received
            break;
        }
        /* Remove the newline character added by getline(). */
        if(inputLength>0 && input[inputLength-1]=='\n') {
            input[inputLength-1]='\0';
        }

        if(strlen(input)==0) {   // if the command is empty, display the prompt again
            continue;
        }

        if(strcmp(input,"exit")==0) {   // checks whether the user wants to exit
            printf("Exiting shell...\n");
            break;
        }

        if(strchr(input,'|')!=NULL) {   // pipeline has the highest priority among the special command formats supported here
            executePipeline(input);
        }

        else if(strstr(input,"&&")!=NULL) {  // checks for parallel execution
            executeParallelCommands(input);
        }

        else if(strstr(input,"##")!=NULL) {  // checks for sequential execution
            executeSequentialCommands(input);
        }

        else if(strchr(input,'>')!=NULL) {   // checks for output redirection
            executeCommandRedirection(input);
        }

        else if(strncmp(input,"cd",2)==0 && (input[2]==' ' || input[2]=='\t' || input[2]=='\0')) {
            /* cd is a shell built-in and must run in the parent process. */
            parseInput(input);
            executeCD();
        }

        else {
            executeCommand(input);
        }
    }

    free(input);  // Release memory allocated by getline()
    return 0;
}