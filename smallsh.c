#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

char input[2048]; // Holds the user's input
char *inFile, *outFile; // Hold names of files to redirect to
char *args[512]; // Array to hold the arguments to pass to execvp()
int numArgs; // The number of applicable arguments
int  redirectIn, redirectOut; // Flags to determine whether to redirect input or output
char exitStatus[2048] = "exit value 0"; // Holds the most recent exit status
pid_t processes[500]; // Array for background processes
int numProcesses = 0; // The number of background processes
int backgroundProcess; // Flag to determine if the process is to be run in the background
char shellPid[2048]; // Holds the shell PID as a string
int fgOnly = 0; // Flag to determine whether the program is in foreground-only mode

// Function to allow a SIGINT signal to terminate the process
void sigintFunc() {
	struct sigaction SIGINT_action = {{0}};
	SIGINT_action.sa_handler = SIG_DFL;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);
}

// Function to enter or exit foreground-only mode with SIGTSTP signal
void sigtstpHandler(int n) {
	char* status;

	// If not already in foreground-only mode, switches and changes status
	if(fgOnly == 0) {
		fgOnly = 1;
		status = "\nEntering foreground-only mode (& is now ignored)\n";
	}
	// If already in foreground-only mode, switches and changes status
	else {
		fgOnly = 0;
		status = "\nExiting foreground-only mode\n";
	}

	write(1, status, strlen(status)); // Prints the status
}

// Function to prevent process from entering or exiting foreground-only mode with SIGTSTP signal
void sigtstpFunc() {
	struct sigaction SIGTSTP_action = {{0}};
	SIGTSTP_action.sa_handler = SIG_IGN;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

// Function to check if any background processes have completed
void checkProcesses() {
	pid_t pid; // Holds PID of background process
	int childExitStatus = -5; // Initialized to junk value

	// Loops through each background process
	for(int i = 0; i < numProcesses; i++) {
		// Checks if process has terminated; WNOHANG prevents blocking parent process
		pid = waitpid(processes[i], &childExitStatus, WNOHANG);

		// If the process has terminated
		if(pid) {
			kill(pid, SIGKILL); // Kills the process
			printf("background pid %d is done: ", pid); // Prints the process's PID

			// Prints out the exit status...
			if(WIFEXITED(childExitStatus)) {
				printf("exit value %d\n", WEXITSTATUS(childExitStatus));
			}
			// ... or the terminating signal, if relevant
			else {
				printf("terminated by signal %d\n", WTERMSIG(childExitStatus));
			}

			fflush(stdout);

			// Removes the terminated process from the array
			for(int j = i; j < numProcesses-1; j++) {
				processes[j] = processes[j+1];
			}

			numProcesses--; // Decrements the number of background processes
		}
	}
}

// Function to perform output redirection
void redirectOutput() {
	// For background processes, redirects to /dev/null if not given target
	if((strcmp(outFile, "") == 0) && (backgroundProcess == 1) && (fgOnly == 0)) {
		outFile = "/dev/null";
	}

	// Opens the output target
	int outFD = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	// Error message if target could not be opened
	if(outFD == -1) {
		fprintf(stderr, "cannot open %s for output\n", outFile);
		fflush(stdout);
		exit(1);
	}	

	// Redirects the output	
	dup2(outFD, 1);
	close(outFD);
}

// Function to redirect input
void redirectInput() {
	// For background processes, redirects to /dev/null if not given source
	if((strcmp(inFile, "") == 0) && (backgroundProcess == 1) && (fgOnly == 0)) {
		inFile = "/dev/null";	
	}

	// Opens the input source
	int inFD = open(inFile, O_RDONLY);

	// Error message if source could not be opened
	if(inFD == -1) {
		fprintf(stderr, "cannot open %s for input\n", inFile);
		fflush(stdout);
		exit(1);
	}

	// Redirects the input
	dup2(inFD, 0);
	close(inFD);
}

// Function for running child process
void runChildProcess() {
	sigtstpFunc(); // Prevents child from entering or exiting foreground-only mode

	// Ensures only foreground processes can terminate with SIGINT
	if((backgroundProcess == 0) || (fgOnly == 1)) {
		sigintFunc();
	}

	// For processes that are redirecting input	
	if((redirectIn == 1) || ((backgroundProcess == 1) && (fgOnly == 0))) {	
		redirectInput();
	}

	// For processes that are redirecting output
	if((redirectOut == 1) || ((backgroundProcess == 1) && (fgOnly == 0))) {
		redirectOutput();
	}

	// Executes the command	
	execvp(args[0], args);
	perror(args[0]); // Prints error message if necessary
	exit(1);
}

// Function for running parent process
void runParentProcess(int pid) {
	int childExitStatus = -5; // Initialize to junk value

	// If background process
	if((backgroundProcess == 1) && (fgOnly == 0)) {
		processes[numProcesses++] = pid; // Adds child process PID to array
		printf("background pid is %d\n", pid); // Prints child process PID
		fflush(stdout);
	}
	// If foreground process
	else {
		waitpid(pid, &childExitStatus, 0); // Waits until child process terminates

		// Stores exit status of child process...
		if(WIFEXITED(childExitStatus)) {
			sprintf(exitStatus, "exit value %d", WEXITSTATUS(childExitStatus));
		}
		// ... or termination signal, if relevant
		else if(WIFSIGNALED(childExitStatus)) {
			sprintf(exitStatus, "terminated by signal %d", WTERMSIG(childExitStatus));
			printf("%s\n", exitStatus);
			fflush(stdout);
		}
	}
}

// Function for forking a new process
void newProcess() {
	pid_t spawnpid = -5; // Initializes to junk value

	spawnpid = fork(); 

	switch(spawnpid) {
		// If fork() was unsuccessful
		case -1:
			perror("Hull breach!"); // Prints error message
			exit(1);
			break;
		// For a child process
		case 0:
			runChildProcess(); // Calls function to run child process
			break;
		// For a parent process
		default:
			runParentProcess(spawnpid); // Calls function to run parents process
	}
}

// Function to print the last exit status receeived
void printStatus() {
	printf("%s\n", exitStatus);
	fflush(stdout);
}

// Function to change the directory
void changeDirectory() {
	// If not given a directory to change to 
	if(numArgs == 1) {
		chdir(getenv("HOME")); // Changes to HOME directory
	}
	// If given directory to change to
	else {
		if(chdir(args[1]) != 0) { // Attempts to change to directory
			printf("%s: no such file or directory\n", args[1]); // Prints error message if unsuccessful
			fflush(stdout);
		}
	}
}

// Function to kill background processes and exit program
void exitProgram() {
	// Loops for each background process
	for(int i = 0; i < numProcesses; i++) {
		kill(processes[i], SIGKILL); // Kills process
	}

	exit(0); // Exits program
}

// Function to run the commands
void runCommand() {
	// For "exit" command
	if(strcmp(args[0], "exit") == 0) {
		exitProgram();	
	}
	// For "cd" command
	else if(strcmp(args[0], "cd") == 0) {
		changeDirectory();	
	}
	// For "status" command
	else if(strcmp(args[0], "status") == 0) {
		printStatus();	
	}
	// For non-built-in commands
	else {
		newProcess();
	}
}

// Function to replace instances of "$$" with the shell PID
char* str_replace(const char *s, const char *oldW, const char *newW) {
	char *result;
	int i, cnt = 0;
	int newWlen = strlen(newW);
	int oldWlen = strlen(oldW);

	// Counts number of times "$$" occurs in string
	for(i = 0; s[i] != '\0'; i++) {
		if(strstr(&s[i], oldW) == &s[i]) {
			cnt++;

			i += oldWlen - 1; // Jumps to the index after "$$"
		}
	}

	// Creates a new string of sufficient length
	result = (char *)malloc(i + cnt * (newWlen - oldWlen) + 1);

	i = 0;

	// Replaces instances of "$$" with the shell PID 
	while(*s) {
		if(strstr(s, oldW) == s) {
			strcpy(&result[i], newW);
			i += newWlen;
			s += oldWlen;
		}
		else {
			result[i++] = *s++;
		}
	}

	result[i] = '\0';
	return result; // Returns the resulting string
}

// Function to evaluate and parse the input
void tokenize() {
	char delims[] = " \n"; // The delimiters
	int getOutName = 0; // Flag to determine if it should look for an output target
	int getInName = 0; // Flag to determine if it should look for an input source
	char old[] = "$$"; // To pass to the str_replace() function

	// Resets variables for each input instance
	numArgs = 0, redirectIn = 0, redirectOut = 0, backgroundProcess = 0;
	inFile = "", outFile = "";

	char *token = strtok(input, delims); // Gets the first token

	// Loops for each token
	while(token != NULL) {
		token = str_replace(token, old, shellPid); // To replace all instances of "$$" with the shell PID
	
		// If the user wants to redirect the input source
		if(strcmp(token, "<") == 0) {
			redirectIn = 1;
			getInName = 1;
		}
		// Gets the new input source
		else if((redirectIn == 1) && (getInName == 1)) {
			inFile = token;
			getInName = 0;
		}
		// If the user wants to redirect the output target
		else if(strcmp(token, ">") == 0) {
			redirectOut = 1;
			getOutName = 1;
		}
		// Gets the new output target
		else if((redirectOut == 1) && (getOutName == 1)) {
			outFile = token;	
			getOutName = 0;
		}
		else {
			args[numArgs++] = token; // Adds the token to the arguments array
			getInName = 0;
			getOutName = 0;
		}

		token = strtok(NULL, delims); // Gets the next token
	}	

	// If last token was "&"
	if(strcmp(args[numArgs-1], "&") == 0) {
		backgroundProcess = 1; // Sets the backgroundProcess flag
		numArgs--; // Decrements the number of arguments
	}
	
	args[numArgs] = '\0'; // Necessary for passing the array to execvp()
}

// Function to get input from the user
void getInput() {
	memset(input, 0, 2048*sizeof(input[0])); // Clears the buffer
	printf(": "); // Prompts for input
	fflush(stdout);
	fgets(input, 2048, stdin); // Gets the input
}

// Function to copy the shell PID into a character array
void setShellPid() {
	pid_t pid = getpid();
	sprintf(shellPid, "%d", pid);
}

// Function to call to initialize the SIGINT and SIGTSTP signals
void initSigs() {
	// For the SIGINT signal
	struct sigaction SIGINT_action = {{0}};
	SIGINT_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &SIGINT_action, NULL);

	// For the SIGTSTP signal
	struct sigaction SIGTSTP_action = {{0}};
	SIGTSTP_action.sa_handler = sigtstpHandler;
	sigfillset(&SIGTSTP_action.sa_mask);
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

int main() {
	setShellPid(); // Sets the shell PID

	initSigs(); // Sets up the signals

	// Loops until the user enters "exit"
	while(1) {
		checkProcesses(); // Checks for completed background processes
		getInput(); // Gets input from the user
		tokenize(); // Parses the input
	
		// Calls the runCommand() function if necessary	
		if((numArgs > 0) && (input[0] != '#')) {
			runCommand();
		}
	}

	return 0;
}
