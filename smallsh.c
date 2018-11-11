#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#define LINE_MAX_LENGTH 2048
#define MAX_ARGS 512

// declared as a global variable because function pointers are weird
int foregroundOnlyMode = 0;

// helper functions
void foregroundOnlyToggle();
void setIOValues(char**, char**, char**);

int main(){
    int exitFlag = 0;
    int counter = 0;
    int i = 0;
    int pid;
    char userInput[LINE_MAX_LENGTH];
    char* arguments[MAX_ARGS];
    char* inputFile = NULL;
    int inputFileOpener;
    int iRedirect;
    char* outputFile = NULL;
    int outputFileOpener;
    int oRedirect;
    char* token = NULL;
    struct sigaction SIGINTHandler;
    struct sigaction SIGSTPHandler;
    int status = 0;

    // prevent CTRL-C/SIGINT from killing shell
    SIGINTHandler.sa_handler = SIG_IGN;
    sigaction(SIGINT, &SIGINTHandler, NULL);

    // CTRL-Z toggles foregroundOnlyMode
    SIGSTPHandler.sa_handler = foregroundOnlyToggle;
    sigaction(SIGTSTP, &SIGSTPHandler, NULL);

    while(!exitFlag){
        // reset
        for(counter=0; counter<MAX_ARGS; ++counter){
            arguments[counter] = NULL;
        }
        counter = 0;
        i = 0;
        token = NULL;
        inputFile = NULL;
        outputFile = NULL;

        // prompt for and read in input
        printf(": ");
        fflush(stdout);
        fgets(userInput, LINE_MAX_LENGTH, stdin);
        if(userInput[0] == '#' || userInput[0] == '\n'){
            // input is a comment; ignore
            continue;
        } else {
            // not a comment; process
            token = strtok(userInput, " \n");
            while(token != NULL){
                if(strcmp(token, "$$") == 0){
                    // replace token with pid
                    sprintf(token, "%ld", (long)getpid());
                }
                arguments[counter] = token;
                ++counter;
                token = strtok(NULL, " \n");
            }

            // parse arguments for io redirects
            setIOValues(&outputFile, &inputFile, arguments);

            // BUILT-IN COMMANDS
            if(strcmp(arguments[0], "exit") == 0){
                // exit command
                break;
            } else if(strcmp(arguments[0], "cd") == 0){
                // change directory command
                if(arguments[1] == NULL){
                    // no arguments...change to HOME
                    chdir(getenv("HOME"));
                } else {
                    // change to dir provided in arguments, and report
                    // error if unable to navigate to that dir
                    if(chdir(arguments[1]) == -1){
                        printf("Invalid path provided\n");
                        fflush(stdout);
                    }     
                }
            } else if(strcmp(arguments[0], "status") == 0){
                // status command
                if(WIFEXITED(status)){
                    // case: status
                    printf("exit value %d\n", WEXITSTATUS(status));
                } else {
                    // case: signal
                    printf("terminated by signal %d\n", WTERMSIG(status));
                }
                fflush(stdout);
            } else {
                // EXTERNAL COMMANDS
                pid = fork();
                if(pid == 0){
                    // child-specific code
                    if(inputFile != NULL){
                        inputFileOpener = open(inputFile, O_RDONLY);
                        if(inputFileOpener == -1){
                            perror("cannot open specified file for input\n");
                            fflush(stdout);
                            exit(1);
                        } else {
                            iRedirect = dup2(inputFileOpener, 0);
                            close(inputFileOpener);
                        }
                    }
                    if(outputFile != NULL){
                        outputFileOpener = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if(outputFileOpener == -1){
                            perror("cannot open specified file for output\n");
                            fflush(stdout);
                            exit(1);
                        } else {
                            oRedirect = dup2(outputFileOpener, 1);
                            close(outputFileOpener);
                        }
                    }
                    if(execvp(arguments[0], (char* const*)arguments)){
                        fprintf(stderr, "ERROR: %s command failed to execute\n", arguments[0]);
                        fflush(stdout);
                        exit(1);
                    }

                } else if(pid < 0){
                    // an error occurred while forking
                    perror("an error occurred while forking\n");
                    fflush(stdout);
                    exit(1);
                } else {
                    // parent-specific code
                    if (strcmp(arguments[counter-1], "&") == 0 && !foregroundOnlyMode){
                        // run the command in the background (don't wait for it)
                        printf("background pid is %d\n", pid);
                        fflush(stdout);
                    } else {
                        // run the command in the foreground (wait for it)
                        waitpid(pid, &status, 0);
                    }
                    // monitor background processes and report as they complete
                    while( (pid = waitpid(-1, &status, WNOHANG)) > 0 ){
                        printf("background pid %d is done: ", pid);
                        if(WIFEXITED(status)){
                            printf("exit value %d\n", WEXITSTATUS(status));
                        } else {
                            printf("terminated by signal %d\n", status);
                        }
                        fflush(stdout);
                    }
                }
            }
        }
    }
    return 0;
}

void foregroundOnlyToggle(){
    if(foregroundOnlyMode == 0){
        // turn foregroundOnlyMode ON
        write(1, "Entering foreground-only mode (& is now ignored)\n", 49);
        fflush(stdout);
        foregroundOnlyMode = 1;
    } else {
        // turn foregroundOnlyMode OFF
        write(1, "Exiting foreground-only mode\n", 29);
        fflush(stdout);
        foregroundOnlyMode = 0;
    }
}

void setIOValues(char** outputArg, char** inputArg, char** arguments){
    int i = 1;
    int j;
    int foundOutput = 0;
    int foundInput = 0;
    // keep parsing while we still have arguments AND we haven't yet found value
    // for input or a value for output
    while( arguments[i] != NULL && (!foundOutput || !foundInput) ){
        if( !foundInput && strcmp(arguments[i], "<") == 0 ){
            *inputArg = arguments[i+1];
            j = i;
            while(arguments[j+1] != NULL){
                arguments[j] = arguments[j+1];
                ++j;
            }
            foundInput = 1;
        }
        if ( !foundOutput && strcmp(arguments[i], ">") == 0 ){
            *outputArg = arguments[i+1];
            j = i;
            while(arguments[j+1] != NULL){
                arguments[j] = arguments[j+1];
                ++j;
            }
            foundOutput = 1;
        }
        ++i;
    }
}