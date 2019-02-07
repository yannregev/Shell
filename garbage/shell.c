#include "parser/ast.h"
#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>


#define EXIT "exit"
#define CD "cd"
#define LS "ls"
#define ECHO "echo"
#define SLEEP "sleep"
#define MKDIR "mkdir"
#define TOUCH "touch"

char path[1024];
pid_t child_pid;

void ignore() {
	printf("\n");
	signal(SIGINT,ignore);
}
void kill_child() {
	kill(child_pid,SIGKILL);
	child_pid = 0;
	signal(SIGINT, ignore);
}

void initialize(void)
{
    /* This code will be called once at startup */
    if (prompt) {
		prompt = "vush$ ";
		strcpy(path, prompt);
		getcwd(path + 6, 1024);
		strcat(path, "$ ");
        	prompt = path;
		signal(SIGINT, ignore);
	}
}



void run_command(node_t *node)
{
    /* For testing: */
 //   	print_tree(node);
    	if (node->type == NODE_REDIRECT) {
    		if (node->redirect.mode == REDIRECT_OUTPUT || node->redirect.mode == REDIRECT_APPEND) {
			int file = (node->redirect.mode == REDIRECT_OUTPUT) ? open(node->redirect.target, O_CREAT | O_WRONLY | O_TRUNC, 0666) :
					 												open(node->redirect.target, O_CREAT | O_APPEND | O_WRONLY, 0666);
			
			int saved_stdout = dup(1);
			if (dup2(file, 1) < 0) {
				perror("error");
				close(file);
			}
			else {
				run_command(node->redirect.child);
				if (dup2(saved_stdout, 1) < 0) {
					perror("error");
				}
				close(file);
			}
		}
		
	} else if (node->type == NODE_COMMAND) {
		char *program = node->command.program;
		char **argv = node->command.argv;
		if (strcmp(program, EXIT) == 0) {
			if (node->command.argc == 2) {
				int state = atoi(argv[1]);
				exit(state);
			} else {
				exit (0);
			}

		} else if (strcmp(program, CD) == 0) {
			if (chdir(argv[1]) == -1) {
				perror("error");
			}

		} else if (strcmp(program,ECHO) == 0) {
			for (size_t i = 1; i < node->command.argc; i++) {
				printf("%s ",argv[i]);
			}
			printf("\n");
			
	 	} else if (strcmp(program, MKDIR) == 0) {
			for (size_t i = 1; i < node->command.argc; i++) {
				if (mkdir(argv[i], 0777) == -1) {
					perror("error");
				}
			}

		} else if (strcmp(program, TOUCH) == 0) {
			for (size_t i = 1; i < node->command.argc; i++) {
					int fd = open(argv[i], O_CREAT, 0666);
				if (fd == -1) {
					perror("error");
				} else {
					close(fd);
				}
			}			

		} else if (strcmp(program, SLEEP) == 0) {
			if (node->command.argc != 2) {
				printf("usage: sleep seconds\n");
			} else {
				int time = atoi(argv[1]);
				sleep(time);
			}

		} else if (strcmp(program, LS) == 0) {
			child_pid = fork();
			if (child_pid == 0) {
				if (execv("/bin/ls", argv) == -1) {
					perror(program);
				}
				exit(0);
			} else {
				wait(NULL);
			}
			
		} else if (strncmp("./",program,strlen("./")) == 0) {
			child_pid = fork();
			if (child_pid == 0) {
				if (execv(program, argv) == -1) {
					perror(program);
				}
				exit(0);
			} else {
				wait(NULL);
			}
		} else {
			char command[1024];
			strcpy(command, program);
			for (size_t i = 1; i < node->command.argc; i++) {
				strcat(command, " ");
				strcat(command, argv[i]);
			}
			if (system(command) == -1)
				perror("error");
			
		}

	} else if (node->type == NODE_SEQUENCE) {
//		run_command(node->sequence.first);
//		run_command(node->sequence.second);
		printf("got here\n");
	}

    if (prompt){
		strcpy(path, "vush$ ");
		getcwd(path + 6,1024);
		strcat(path, "$ ");
        prompt = path;
	}
}

/*
		child_pid = fork();
		if (child_pid == -1) {
			perror("error");
		} else {
			signal(SIGINT, kill_child);
		}
		
		if (child_pid != 0) {
			wait(NULL);
			return;
		}
*/

/*
			//might be used for ls...
			struct dirent **namelist;
			int n = scandir(".",&namelist,NULL,alphasort);
			while(n--) {
				if (strcmp(namelist[n]->d_name,".") != 0 && strcmp(namelist[n]->d_name,"..") != 0)
					printf("%s\t",namelist[n]->d_name);
				free(namelist[n]);
			}
			free(namelist);
			printf("\n");
*/

