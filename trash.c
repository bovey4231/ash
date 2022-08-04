#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

typedef struct job {
    int jobId;
    pid_t pidNum;
    pid_t pids[20];
    char* command;
    struct job* next;
}job; 

struct shellState {
    job* foregroundJob;
    job** jobListHead;
};

struct shellState shellState;

char ** tokenize(char* input) {
    char** tokens = malloc(50*sizeof(char*));
    char* inputCopy = malloc(1024*sizeof(char));
    strcpy(inputCopy,input);

    char* token = strtok(inputCopy," \n\r\t");
    
    int i = 0;
    while (token != NULL) {
        tokens[i] = token;
        token = strtok(NULL, " ");
        i++;
   }

   tokens[i] = NULL;

   return tokens;
}

char*** pipingUtil(char** tokens, int numBars) {
    char*** commandList = malloc((numBars+1)*sizeof(char**)); // commands
    int j = 0;
    int i = 0;

    for (i; i < numBars+1; i++) {
        commandList[i] = malloc(10*sizeof(char*)); // args
        
        int k = 0;
        while((tokens[j] != NULL) && (strcmp(tokens[j],"|"))) {      // copy from tokens into commandList[i] until we hit a |
            commandList[i][k] = malloc(50*sizeof(char));  // tokens
            strcpy(commandList[i][k], tokens[j]);
            j++;
            k++;
        }
        commandList[i][k] = NULL;
        j++;
    }
    commandList[i] = NULL;
    return commandList;
}
    
int checkPipes(char ** tokens) {
    // return number of pipes in tokens
    int i = 0;
    int numBars = 0;

    while (tokens[i] != NULL) {
        if (!strcmp(tokens[i],"|")) {
            numBars++;
        }
        i++;
    }
    return numBars;
}

int checkAmper(char** tokens) {
    // return 1 if last token is amper or 0 otherwise
    // also removes the amper from the tokens
    int i = 0;
    while (tokens[i] != NULL) {
        i++;
    }
    if (!strcmp(tokens[i-1], "&") && (i != 1)) {
        return 1;
    } else {
        return 0;
    }

}

char* getStatus(pid_t jobPid) {

    int c;
    char pidString[10];
    char procfilename[100];
    FILE *procfile;

    sprintf(pidString, "%d", jobPid);
    strcpy(procfilename, "/proc/");
    strcat(procfilename, pidString);
    strcat(procfilename, "/stat");
    procfile = fopen(procfilename, "r");

    if (procfile == NULL) {
        perror("Failed to open file: ");
        printf("%s", procfilename);
        exit(EXIT_FAILURE);
    }

    do {
        c = fgetc(procfile);
    } while (c != ')');

    fgetc(procfile);
    c = fgetc(procfile);
    fclose(procfile);

    // sleeping, runnable, stopped, idle, zombie or done
    switch (c) {
    case 'D':
        return "done";
        break;
    case 'R':
        return "runnable";
        break;    
    case 'T':
        return "stopped";
        break;     
    case 'I':
        return "idle";
        break;    
    case 'Z':
        return "zombie";
        break; 
    case 'S':
        return "sleeping";
        break;       
    }

    return NULL;
}

void systemCall(char** tokens, char** history, int historyIndex, job** head, char* workingDirectory) {
    int amper;
    if(amper = checkAmper(tokens)) { // if this is a bg job then remove the amper 
       int i = 0;

        while (tokens[i] != NULL) {
        i++;
        }
        if (!strcmp(tokens[i-1], "&") && (i != 1)) {
            tokens[i-1] = NULL;
        }
    }

    if (!strcmp(tokens[0],"fg")) {
        if ((*head)->pidNum == 0) {
            return;
        }
        
        job* temp;
        job **jobPtr = malloc(10*sizeof(job));

        temp = *head; 
        *jobPtr = *head; // by default call on most recent job
        
        char* statusString = malloc(10*sizeof(char));
        statusString = getStatus(temp->pids[temp->pidNum-1]); // make sure job is stopped and not running

        while(temp->pidNum != 0) {
            statusString = getStatus(temp->pids[temp->pidNum-1]);
            if (!strcmp(statusString,"stopped")) {
                *jobPtr = temp;
                break;
            }
            temp = temp->next;
        }

        if (strcmp(statusString,"stopped")){
            return;
        }
        temp = *head;

        if ((tokens[1] != NULL) && (atoi(tokens[1]) > 0) && (atoi(tokens[1]) <= (*head)->jobId)) {
            int jobId = atoi(tokens[1]);
            while(temp->pidNum != 0) {
                if (temp->jobId == jobId) {
                    *jobPtr = temp; // change the job to the jobId in tokens[1]
                    break;
                }
                temp = temp->next;
            }
        }
        int status;

        for(int i = 0; i < (*jobPtr)->pidNum; i++) {
            kill((*jobPtr)->pids[i], SIGCONT);
        }

        shellState.foregroundJob = *jobPtr;
        printf("%s\n", (*jobPtr)->command);
        tcsetpgrp(0, getpgrp());
        waitpid((*jobPtr)->pids[(*jobPtr)->pidNum-1], &status, WUNTRACED);

        if(WIFEXITED(status)) { // if exited normally wait on other commands in pipe
            for (int i = 0; i < (*jobPtr)->pidNum; i++) {
                waitpid((*jobPtr)->pids[i], NULL, WNOHANG);
            }
        }
        if (WIFSTOPPED(status)) {
            return;
        } else {  // only remove from jobs list if process is waited on not stopped via signal
            if ((*jobPtr) == (*head)) {
                (*head) = (*head)->next;   
            } else {
                job* prev, *temp;
                temp = *head; 
                prev = temp;
                
                while(temp->pidNum != 0) {
                    if (temp == (*jobPtr)) {
                        prev->next = temp->next;
                    }
                    prev = temp;
                    temp = temp->next;
                } 
            }
            
        }
        shellState.foregroundJob = NULL;
        return;
    }

    if (!strcmp(tokens[0],"bg")) {
        if ((*head)->pidNum == 0) {
            return;
        }
        
        job* temp;
        job *bgJob;
        temp = *head; 
        bgJob = *head; // by default call on most recent job

        char* statusString = getStatus(temp->pids[temp->pidNum-1]); // make sure job is stopped and not running

        while(temp->pidNum != 0) {
            statusString = getStatus(temp->pids[temp->pidNum-1]);
            if (!strcmp(statusString,"stopped")) {
                bgJob = temp;
                break;
            }
            temp = temp->next;
        }

        if (strcmp(statusString,"stopped")){
            printf("bg: job [%d] already in background\n", bgJob->jobId);
            return;
        }
        temp = *head;

        if ((tokens[1] != NULL) && (atoi(tokens[1]) > 0) && (atoi(tokens[1]) <= (*head)->jobId)) {
            int jobId = atoi(tokens[1]);
            while(temp->pidNum != 0) {
                if (temp->jobId == jobId) {
                    bgJob = temp; // change the job to the jobId in tokens[1]
                    break;
                }
                temp = temp->next;
            }
        }
        int status;

        printf("[%d] %s\n", bgJob->jobId, bgJob->command);

        for(int i = 0; i < bgJob->pidNum; i++) {
            kill(bgJob->pids[i], SIGCONT);
        }
        return;        
    }

    if (!strcmp(tokens[0],"kill")) {
        if ((*head)->pidNum == 0) {
            return;
        }
        
        job* temp;
        job *killJob;
        temp = *head; 
        killJob = *head; // by default call on most recent job

        char* statusString = getStatus(temp->pids[temp->pidNum-1]); // make sure job is stopped and not running

        while(temp->pidNum != 0) {
            statusString = getStatus(temp->pids[temp->pidNum-1]);
            if (!strcmp(statusString,"stopped")) {
                killJob = temp;
                break;
            }
            temp = temp->next;
        }

        if (strcmp(statusString,"stopped")){
            return;
        }
        temp = *head;

        if ((tokens[1] != NULL) && (atoi(tokens[1]) > 0) && (atoi(tokens[1]) <= (*head)->jobId)) {
            int jobId = atoi(tokens[1]);
            while(temp->pidNum != 0) {
                if (temp->jobId == jobId) {
                    killJob = temp; // change the job to the jobId in tokens[1]
                    break;
                }
                temp = temp->next;
            }
        }
        int status;

        printf("[%d] Killed %s\n", killJob->jobId, killJob->command);

        for(int i = 0; i < killJob->pidNum; i++) {
            kill(killJob->pids[i], SIGKILL);
        }

        for(int i = 0; i < killJob->pidNum; i++) {
            waitpid(killJob->pids[i], NULL, WUNTRACED);
        }

        job* prev;   // remove the killed job
        temp = *head;
        prev = temp;

        while(temp->pidNum != 0) { 
            if (temp->jobId == killJob->jobId) {
                if (temp == *head) { // if the node we want to remove is the head then we make the next node the head
                    *head = (*head)->next;
                } else { // else node is in middle of list and we point the previous node to the one after temp
                    prev->next = temp->next;
                }
            }
            prev = temp;
            temp = temp->next;
        }
        return;         
    }

    if (!strcmp(tokens[0],"jobs")) {
        job* temp;
        temp = *head; 

        char* statusString = malloc(10*sizeof(char));

        while(temp->pidNum != 0) {   
            statusString = getStatus(temp->pids[temp->pidNum-1]);
            printf("[%d] <%s> %s\n", temp->jobId, statusString, temp->command);
            temp = temp->next;
        }

        return;
    }

    if (!strcmp(tokens[0],"cd")) {
        if (tokens[1] == NULL) {
            chdir(workingDirectory);
        } else {
            if (chdir(tokens[1]) == -1) {
                printf("cd: %s: ", tokens[1]);
                fflush(stdout);
                perror("");
            }
        }
        return;
    } 
    
    if (!strcmp(tokens[0],"history") || !strcmp(tokens[0],"h")) {
        if (tokens[1] != NULL) {
            if( (atoi(tokens[1]) > 0) && (atoi(tokens[1]) < historyIndex) && 
            (atoi(tokens[1]) > historyIndex - 10)) {     
                
                // if amper add the amper back to tokens

                char** historyTokens = malloc(10*sizeof(char*));
                historyTokens = tokenize(history[(atoi(tokens[1])-1)%10]);

                // write the command into the history
                strcpy(history[(historyIndex-1)%10], history[(atoi(tokens[1])-1)%10]);

                // stop infinite recursion
                if (historyTokens[1] != NULL) {
                    if ((!strcmp(historyTokens[0],"h")) && (!strcmp(historyTokens[1],tokens[1]))) {
                        return; 
                    }
                }
                // call the command specified in the second token
                systemCall(historyTokens, history, historyIndex, head, workingDirectory);

                free(historyTokens);
                return;
            }
        } else {
            for (int i = historyIndex-10 ; i < historyIndex; i++) {
                if (i >= 0) {
                    printf("%d: %s\n", i+1 , history[i%10]);
                }
            }
            return;
        }
    } 

    pid_t pid;
    int status;
    int numBars = checkPipes(tokens); 
    int pipes[numBars][2];
    pid_t pipePids[numBars+1];
    char*** commandList = pipingUtil(tokens, numBars);
    
    for (int i = 0; i < numBars; i++) {
        pipe(pipes[i]);
    }
    
    for (int i = 0; i < numBars+1; i++) {
        pid = fork();
        if (pid == 0) {
            if (i != 0) { // if we are not in the 1st command then we change input from STDIN to the previous' pipes read end
                dup2(pipes[i-1][0],STDIN_FILENO);
                close(pipes[i-1][0]);
            }

            if (i != numBars) { // if we are not at the last command then we change output from STDOUT to the pipes write end
                dup2(pipes[i][1],STDOUT_FILENO);
                close(pipes[i][1]);
            } 

            if (execvp(commandList[i][0],commandList[i]) == -1) {
                exit(EXIT_FAILURE);
            }
        }
        // in parent process close pipes that werent closed in child
        if (i != 0) {
            close(pipes[i-1][0]);
        }

        if(i != numBars) {
            close(pipes[i][1]);
        }

        pipePids[i] = pid;
    }

    if(!amper) { // if not amper (fg job) then we store job into shellstate variable and wait
        job *foregroundJob = malloc(sizeof(job));

        char* commandLine = malloc(1024*sizeof(char)); // detokenize to store into job struct
        int i = 0;
        while (tokens[i] != NULL) {
            strcat(commandLine,tokens[i]);
            strcat(commandLine," ");
            i++;
        }

        foregroundJob->pidNum = numBars+1;
        for (int i = 0; i < numBars+1; i++) {
            foregroundJob->pids[i] = pipePids[i]; // store all pids of pipe
        }
        foregroundJob->command = commandLine;
        foregroundJob->jobId = (*shellState.jobListHead)->jobId+1;

        shellState.foregroundJob = foregroundJob;

        waitpid(pid, &status, WUNTRACED);

        if(WIFEXITED(status)) { // if exited normally wait on other commands in pipe
            for (int i = 0; i < foregroundJob->pidNum; i++) {
                waitpid(foregroundJob->pids[i], &status, WNOHANG);
            }
        }

        shellState.foregroundJob = NULL; // if the commnand is done or stopped then we cant stop ( ctrl Z )
    }

    if (amper) {    // if amper we add command to joblist and print its id + pid
        job *newJob = malloc(sizeof(job));
        newJob->jobId = (*head)->jobId+1;

        char* commandLine = malloc(1024*sizeof(char)); // detokenize to store into job struct
        int i = 0;
        while (tokens[i] != NULL) {
            strcat(commandLine,tokens[i]);
            strcat(commandLine," ");
            i++;
        }
        strcat(commandLine,"&");

        newJob->pidNum = numBars+1;
        for (int i = 0; i < numBars+1; i++) {
            newJob->pids[i] = pipePids[i]; // store all pids of pipe
        }
        newJob->command = commandLine;
        newJob->next = *head;
        
        *head = newJob; 

        printf("[%d] %d\n", (*head)->jobId, pid); // print last pid
    } 

    return;
}

void checkBackGround(job** head) { 
    job* temp, *prev;
    temp = *head; 
    prev = temp;
    int status;
    
    while(temp->pidNum != 0) { 
        int returnedVal = waitpid(temp->pids[temp->pidNum-1], &status, WNOHANG);
        if (returnedVal > 0) {  // check that the last process in pipe is done
            for (int i = 0; i < temp->pidNum-1; i ++) { // wait on other processes of job
                waitpid(temp->pids[i], &status, WNOHANG);
            }

            printf("[%d] <Done> %s\n", temp->jobId, temp->command);
            if (temp == *head) { // if the node we want to remove is the head then we make the next node the head
                *head = (*head)->next;
            } else { // else node is in middle of list and we point the previous node to the one after temp
                prev->next = temp->next;
            }
        }
        prev = temp;
        temp = temp->next;
    }

    return;
}

void sigStop(int sig) { // handle ctrl Z
  
    if (shellState.foregroundJob != NULL){
        kill(shellState.foregroundJob->pids[shellState.foregroundJob->pidNum-1], SIGTSTP);

        // add job to job list and print "[1]+ Stopped python3 count.py"
        job** head = shellState.jobListHead;
        job* temp;
        temp = *head; 

        printf("\n[%d] Stopped %s \n", shellState.foregroundJob->jobId, shellState.foregroundJob->command);
        

        while(temp->pidNum != 0) {
            if (temp->pids[(temp->pidNum-1)] == shellState.foregroundJob->pids[shellState.foregroundJob->pidNum-1]) {
                shellState.foregroundJob = NULL;
                return; // check stopped job isnt already in job list 
            }
            temp = temp->next;
        }
        shellState.foregroundJob->next = (*head);
        (*head) = shellState.foregroundJob;
        shellState.foregroundJob = NULL;
    }

    return;
}

int main(void) {
    char input[1024];
    char** history = malloc(100*sizeof(char*)); 
    for(int i = 0; i < 11; i++){
        history[i] = malloc(1024*sizeof(char));
    } // initialize history array
    int historyIndex = 0;

    char workingDirectory[200];
    getcwd(workingDirectory, sizeof(workingDirectory));
    
    job* head = malloc(sizeof(job));
    head->jobId = 0;
    head->next = NULL;

    signal(SIGTSTP, &sigStop);

    while(1) {
        printf("ash>");

        if (fgets(input, 1024, stdin) == NULL) {
            freopen("/dev/tty", "r", stdin);  // if we reach the end of a file then reopen stdin with keyboard
            fgets(input, 1024, stdin);
        }  

        if (input[strlen(input)-1] == '\n') { 
            input[strlen(input)-1] = '\0';
        }

        if (isatty(STDIN_FILENO) == 0) { // if we are in a file print the inputs
            printf("%s\n", input);
        }

        if (strlen(input) > 0) {
            char** tokens = malloc(20*sizeof(char*));
            tokens = tokenize(input);

            if (tokens[0] == NULL) {
                continue;
            }

            strcpy(history[historyIndex%10], input);    // put input into history array
            historyIndex++;

            shellState.jobListHead = &head; // update shell state

            checkBackGround(&head);

            systemCall(tokens, history, historyIndex, &head, workingDirectory);  
            
            free(tokens);
        }
        
        checkBackGround(&head);
    }
    return 0;
}