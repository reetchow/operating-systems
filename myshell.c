#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>

int read_command(char * command, bool print_prompt);
int parse_command(char * input, char * args[], bool * run_bg);
int check_meta_chars(char * args[], int token_count);
void in_redirect(char * args[], int token_count, char * arg_list[]);
void out_redirect(char * args[], int token_count, char * arg_list[]);
void pipe_commands(char * args[], int token_count, bool redirect_input, bool redirect_output);
void signal_handler(int signal);

int main(int argc, char * argv[])
{
	
	bool print_prompt = true;
	bool run_bg = false;

	if(argc > 1)
		if(strcmp(argv[1], "-n") == 0)
			print_prompt = false;
	
	while(1)
	{
		char command[512] = "";
		pid_t pid; //child process ID
		int status;
		char * args[512] = {}; //argument list from the command line
		int in, out;

		int close = read_command(command, print_prompt); //get a command from the user
	
		int token_count = parse_command(command, args, &run_bg); //parse the command and get the number of tokens
		int meta_number = check_meta_chars(args, token_count); //find any meta characters

		if((pid = fork()) > 0)
		{
			if(run_bg == true)
			{
				signal(SIGCHLD, signal_handler);
			}
			else
			{
				waitpid(pid, &status, 0);
			}
		} else {
			if(meta_number == 0){		
				execvp(args[0], args);
			}else if(meta_number == 1){
				char * arg_list[512] = {};
				in_redirect(args, token_count, arg_list);
				execvp(arg_list[0], arg_list);
			}else if(meta_number == 2){
				char * arg_list[512] = {};
				out_redirect(args, token_count, arg_list);
				execvp(arg_list[0], arg_list);		
			}else if(meta_number == 3){
				pipe_commands(args, token_count, false, false);
			}else if(meta_number == 4){
				char * arg_list1[512] = {};
				char * arg_list2[512] = {};
				in_redirect(args, token_count, arg_list1);
				out_redirect(args, token_count, arg_list2);
				execvp(arg_list1[0], arg_list1);
			}else if(meta_number == 5){
				pipe_commands(args, token_count, true, false);
			}else if(meta_number == 6){
				pipe_commands(args, token_count, false, true);
			}else if(meta_number == 7){
				pipe_commands(args, token_count, true, true);
			}
		}
	}
}

void signal_handler(int signal)
{
	//printf("Caught signal %d\n", signal);
	if(signal == SIGCHLD)
	{
		//printf("child ended\n");
		wait(NULL);
	}
}

void in_redirect(char * args[], int token_count, char * arg_list[])
{	
	//handles input redirection
	char * filename;
	char * token;
	int in;

	int i = 0;
	for(i = 0; i < token_count; i++)
	{
		token = args[i];
		if(strcmp(token, "<") == 0)
		{
			filename = args[i + 1];
			break;
		}
		arg_list[i] = token;
	}

	in = open(filename, O_RDONLY);
	dup2(in, 0);
	close(in);
}

void out_redirect(char * args[], int token_count, char * arg_list[])
{
	//handles output redirection
	char * filename; //file to redirect to
	char * token; //each token in args
	int out; //fd for the outfile

	int i = 0;
	for(i = 0; i < token_count; i++)
	{
		token = args[i]; //token is each token in the args list
		if(strcmp(token, ">") == 0)
		{
			filename = args[i + 1]; //get the filename
			break; //break out of the for loop
		}
		arg_list[i] = token; //collect all argument commands before the ">" into arg_list
	}

	//for(i = 0; i < token_count; i++)
	//		printf("right args: '%s'\n", arg_list[i]);

	out = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR); //open the file to write
	dup2(out, 1); //change stdout to be the outfile
	close(out); //close the unused fd for the outfile
}

void pipe_commands(char * args[], int token_count, bool redirect_input, bool redirect_output)
{	
	//handles piping
	char * token;

	char * arg_list1[512] = {};
	char * arg_list2[512] = {};

	int pipefd[2]; //pipe file descriptors
	int pid2; //pid of child process
	pipe(pipefd); //create pipe and store fds
	pid2 = fork(); //fork to clone the process

	if(pid2 != 0)
	{
		//command on left of pipe

		int i = 0;
		int left_token_count = 0;
		char * modified_arg_list1[512];

		for(i = 0; i < token_count; i++)
		{
			
			token = args[i];
			if(strcmp(token, "|") == 0)
				break;
			arg_list1[i] = token;
			left_token_count++;
		}

		if(redirect_input == true)
		{
			in_redirect(arg_list1, left_token_count, modified_arg_list1);
		}

		close(pipefd[0]);
		dup2(pipefd[1], 1);
		close(pipefd[1]);

		if(redirect_input == true)
			execvp(modified_arg_list1[0], modified_arg_list1);
		else
			execvp(arg_list1[0], arg_list1);
	}else{
		//command on right of pipee



		int i, right_token_count = 0;
		bool second_half = false;
		char * modified_arg_list2[512];

		for(i = 0; i < token_count; i++)
		{
			token = args[i]; //each command
			if(second_half == true){
				arg_list2[right_token_count] = token;
				right_token_count++;
			}
			if(strcmp(token, "|") == 0)
				second_half = true;
		}

		close(pipefd[1]); //close the write fd for the pipe
		dup2(pipefd[0], 0); //replace stdin with the read for the pipe
		close(pipefd[0]); //close the read fd for the pip

		if(redirect_output == true)
		{
			out_redirect(arg_list2, right_token_count, modified_arg_list2);
		}

		if(redirect_output == true)
			execvp(modified_arg_list2[0], modified_arg_list2);
		else
			execvp(arg_list2[0], arg_list2);
	}
}

int check_meta_chars(char * args[], int token_count)
{
	/* This function checks the args array token by token to see if any meta characters are found and then returns a number 0-7 accordingly */
	bool p;
	bool in;
	bool out;

	int i = 0;
	for(i = 0; i < token_count; i++)
	{
		if(strcmp(args[i], "|") == 0)
		{
			p = true; //piping character found
		}
		if(strcmp(args[i], "<") == 0)
		{
			in = true; //input redirection character found
		}
		if(strcmp(args[i], ">") == 0)
		{
			out = true; //output redirection character found
		}		
	}

	if(p == 0 && in == 0 && out == 0){
		return 0; //neither of the three
	} else if (p == 0 && in == 1 && out == 0) {
		return 1; //input redirection
	} else if (p == 0 && in == 0 && out == 1) {
		return 2; //output redirection
	} else if (p == 1 && in == 0 && out == 0) {
		return 3; //piping
	} else if (p == 0 && in == 1 && out == 1) {
		return 4; //input and output redirection
	} else if (p == 1 && in == 1 && out == 0) {
		return 5; //piping and input redirection
	} else if (p == 1 && in == 0 && out == 1) {
		return 6; //piping and output redirection
	} else if (p == 1 && in == 1 && out == 1) {
		return 7; //all three
	}
}

int read_command(char * command, bool print_prompt)
{
	/* Reads a command from the user and puts it into the command string */

	char input[512];
	if(print_prompt == true)
	{
		printf("my_shell ");
	}

	if(fgets(input, sizeof(input), stdin) == NULL)
		return -1; //get user input -> input

	char * p;
	p = strchr(input, '\n'); //find the \n char in the input

	if(p != NULL)
	{
		*p = '\0'; //set the \n char to the null termination char instead
	}

	strcpy(command, input); //copy the modified input string into the command string
}

int parse_command(char * input, char * args[], bool * run_bg)
{
	/* Parses the user input string and puts the tokens into the args array */

	//setting up the parsing
	const char delimiters[] = " ";
	char * running; //copy of input -- manipulate this instead
	char * token; //each token will be stored in here
	char tokens[512][512]; //array of tokens generated by separation
	running = strdup(input); //copy input to running
	int token_count = 0; //number of tokens

	//parse the string for the delimiters
	do{
		token = strsep(&running, delimiters); //generate a token once a delimiter is encountered
		strcpy(tokens[token_count], token); //put the token in the tokens array
		args[token_count] = token;
		token_count++; //increment the token count
		//printf("Tokens # %d: '%s'\n", token_count, token); //printing tokens
	}
	while (running != NULL); //keep going until nothing is left in the string
	
	int i = 0;
	if(strcmp(args[token_count - 1],"&") == 0)
	{
		*run_bg = true;
		args[token_count - 1] = NULL; 
		token_count--;
	}	
	//for(i = 0; i < token_count; i++)
	//	printf("args # %d: '%s'\n", i, args[i]);
	
	return token_count;
}
