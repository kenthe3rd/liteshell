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
char *replace_str(char *str, char *orig, char *rep);

int main(){
    int exitFlag = 0;
    int counter = 0;
    int i = 0;
    int pid;
    char* argument = NULL;
    char stringPID[16];
    sprintf(stringPID, "%ld", (long)getpid());
    char userInput[LINE_MAX_LENGTH];
    char* arguments[MAX_ARGS];
    char inputFile[1024];
    int inputFileOpener;
    int iRedirect;
    char outputFile[1024];
    int outputFileOpener;
    int oRedirect;
    char* token = NULL;
    struct sigaction SIGINTHandler;
    struct sigaction SIGSTPHandler;
    int status = 0;
    int backgroundRequest;

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
        for(i=0; i<1024; ++i){
            inputFile[i] = '\0';
            outputFile[i] = '\0';
        }
        counter = 0;
        i = 0;
        backgroundRequest = 0;
        token = NULL;
        argument = NULL;

        // prompt for and read in input
        printf(": ");
        fflush(stdout);
        fgets(userInput, LINE_MAX_LENGTH, stdin);
        if(userInput[0] == '#' || userInput[0] == '\n' || userInput[0] == '\0'){
            // input is a comment; ignore
            continue;
        } else {
            // not a comment; process
            token = strtok(userInput, " \n");
            while(token != NULL){
                argument = replace_str(token, "$$", stringPID);
                if(strcmp(token, "<") == 0){
                    // process input redirect
                    token = strtok(NULL, " \n");
                    argument = replace_str(token, "$$", stringPID);
                    strcpy(inputFile, argument);
                } else if(strcmp(token, ">") == 0){
                    // process output redirect
                    token = strtok(NULL, " \n");
                    argument = replace_str(token, "$$", stringPID);
                    strcpy(outputFile, argument);
                } else {
                    // add an argument
                    arguments[counter] = argument;
                    ++counter;
                }
                token = strtok(NULL, " \n");
            }
            if(strcmp(arguments[counter-1], "&") == 0){
                // set the background request flag and strip & out of args
                arguments[counter-1] = NULL;
                backgroundRequest = 1;
            }
            // parse arguments for io redirects

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
                    if (!backgroundRequest || foregroundOnlyMode){
                        // child in foreground...allow for interrupts
                        SIGINTHandler.sa_handler = SIG_DFL;
                        SIGINTHandler.sa_flags = 0;
                        sigaction(SIGINT, &SIGINTHandler, NULL);
                    }
                    if(inputFile[0] != '\0'){
                        inputFileOpener = open(inputFile, O_RDONLY);
                        if(inputFileOpener == -1){
                            perror("cannot open specified file for input\n");
                            fflush(stdout);
                            exit(1);
                        } else {
                            iRedirect = dup2(inputFileOpener, 0);
                            close(inputFileOpener);
                        }
                    } else {
                        inputFileOpener = open("/dev/null", O_RDONLY);
                        iRedirect = dup2(inputFileOpener, 0);
                        close(inputFileOpener);
                    }
                    if(outputFile[0] != '\0'){
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
                    if (backgroundRequest && !foregroundOnlyMode){
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
        foregroundOnlyMode = 1;
    } else {
        // turn foregroundOnlyMode OFF
        write(1, "Exiting foreground-only mode\n", 29);
        foregroundOnlyMode = 0;
    }
}


// stolen from
// https://www.linuxquestions.org/questions/programming-9/replace-a-substring-with-another-string-in-c-170076/
char *replace_str(char *str, char *orig, char *rep){
  static char buffer[4096];
  char *p;

  if(!(p = strstr(str, orig)))  // Is 'orig' even in 'str'?
    return str;

  strncpy(buffer, str, p-str); // Copy characters from 'str' start to 'orig' st$
  buffer[p-str] = '\0';

  sprintf(buffer+(p-str), "%s%s", rep, p+strlen(orig));

  return buffer;
}