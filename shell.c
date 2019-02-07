#include "parser/ast.h"
#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>

#define EXIT "exit"
#define CD "cd"
#define SET "set"
#define UNSET "unset"
#define FG "fg"
#define BG "bg"
#define STDIN 0
#define STDOUT 1
#define STDERR 2


pid_t sus_children[256];
pid_t child = -1;
int sus_child_size = -1;

void stop_child(){
	if (child == -1)
		return;
	sus_children[++sus_child_size] = child;
	killpg(child, SIGTSTP);
	child = -1;
}

void resume_child() {
	child = sus_children[sus_child_size--];
	killpg(child, SIGCONT);
}

void ignore() {
	if (child != -1)
		kill(child, SIGKILL);
}

void remove_children() {
	while (waitpid(-1 , NULL, WNOHANG) > 0);
}



char *parse_prompt() {
	if (getenv("PS1") == NULL)
		return "vush$ ";
		
	char *p = getenv("PS1");	
	char *buffer = malloc(1024 * sizeof(char));
	char host[256];
	char workdir[256];
	int offset = 0;
	struct passwd *name;
	uid_t uid = 0;
	name = getpwuid(uid);
	int i;
	for (i = 0; p[i]; i++) {
		
		if (p[i] == '\\') {
			i++;
			switch(p[i]) {
				case 'u':
					offset += strlen(name->pw_name) - 2;
					strcat(buffer, name->pw_name);
					break;
				case 'h':
					if (gethostname(host, sizeof(host)) == -1) perror("hostname");
					offset += strlen(host) - 2;
					strcat(buffer, host);
					break;
				case 'w':
					if (getcwd(workdir, sizeof(workdir)) == NULL)perror("working dir");
					offset += strlen(workdir) - 2;
					strcat(buffer, workdir);
			}
		} else {
			buffer[i+offset] = p[i];
		}
	}
	buffer[i+offset] = '\0';
	return buffer;
}

void initialize(void)
{
    /* This code will be called once at startup */
	
    	if (prompt) {
		prompt = parse_prompt();
	}
	signal(SIGINT,ignore);
	signal(SIGCHLD, remove_children);
	signal(SIGTSTP, stop_child);
}


void exec_pipe(node_t *node) {
	size_t parts = node->pipe.n_parts;
 		size_t n_pipes = parts - 1;
 		pid_t pid;
 		int pipefd[parts * 2];
 		for(size_t i = 0; i < n_pipes; i++) {
	 		if (pipe(pipefd + i * 2) == -1)
	 			perror("pipe");
	 	}
	 	
	 	int commandc = 0;
	 	for (size_t i = 0; i < parts; i++) {
	 		pid = fork();
	 		
			if (pid == 0) {
				if (i != 0)
					dup2(pipefd[(commandc-1)*2], STDIN);
				if ( i != n_pipes)
					dup2(pipefd[commandc*2+1], STDOUT);
				
				for(size_t j = 0; j < 2 * (parts-1); j++) {
	 				close(pipefd[j]);
	 			}
				
				run_command(node->pipe.parts[i]);
				exit(EXIT_FAILURE);
			} else  if (pid < 0){
				perror("fork");
			}
			commandc++;
	 	}
	 	for(size_t i = 0; i < 2 * n_pipes; i++) {
	 		close(pipefd[i]);
	 	}
	 	
	 	for (size_t i = 0; i < parts; i++) {
	 		waitpid(-1, NULL, WUNTRACED);
		}
}

void exec_sequence(node_t *node) {
	run_command(node->sequence.first);
	while (node->sequence.second->type == NODE_SEQUENCE) {
		node_t *temp = node->sequence.second;
		free(node->sequence.first);			
		node->sequence.first = node->sequence.second->sequence.first;
		node->sequence.second = node->sequence.second->sequence.second;
		free(temp);
		run_command(node->sequence.first);
	} 
	run_command(node->sequence.second);
}

void exec_redirect(node_t *node) {
	int stream = -1;
	int file = -1;
	int fd = node->redirect.fd;
	
	switch(fd) {
		case -1:
			file = open(node->redirect.target, O_CREAT | O_WRONLY | O_TRUNC, 0666);
			int saved_stdout, saved_stderr;
			saved_stdout = dup(STDOUT);
			saved_stderr = dup(STDERR);
			if (dup2(file, STDOUT) == -1) perror("dup2");
			if (dup2(STDOUT, STDERR) == -1) perror("dup2");
			run_command(node->redirect.child);
			if (dup2(saved_stderr, STDERR) == -1) perror("dup2");
			if (dup2(saved_stdout, STDOUT) == -1) perror("dup2");
			close(file);
			break;	
		
		default:
			if (node->redirect.mode == REDIRECT_OUTPUT) {
				file = open(node->redirect.target, O_CREAT | O_WRONLY | O_TRUNC, 0666);
				stream = STDOUT;
			} else if (node->redirect.mode == REDIRECT_APPEND) {
				file = open(node->redirect.target, O_CREAT | O_APPEND | O_WRONLY, 0666);
				stream = STDOUT;
			} else if (node->redirect.mode == REDIRECT_INPUT) {
				file = open(node->redirect.target, O_RDONLY, 0666);
				stream = STDIN;
			} else {
				file = dup(node->redirect.fd2);
				stream = fd;
			}
			
			if (file == -1) perror("error");
			int saved_stdin = dup(stream);
			if (dup2(file, stream) < 0) {
				perror("error");
				close(file);
			} else {
				run_command(node->redirect.child);
				if (dup2(saved_stdin, stream) < 0) perror("error");
				close(file);
			}
			break;
	}
	
}

void exec_command(node_t *node) {
	char *program = node->command.program;
	char **argv = node->command.argv;
	if (strcmp(program, EXIT) == 0) {
		if (node->command.argc == 2) {
			exit(atoi(argv[1]));
		} else {
			printf("usage: exit <exit code>\n");
		}

	} else if (strcmp(program, CD) == 0) {
		if (chdir(argv[1]) == -1) perror("error");

	} else if (strcmp(program, SET) == 0) {
		if (putenv(argv[1]) == -1) perror("set");
		
	} else if (strcmp(program, UNSET) == 0) {
		if (unsetenv(argv[1]) == -1) perror("unset");
		
	} else if (strcmp(program, FG) == 0) {
		if (sus_child_size != -1) resume_child();
		waitpid(-1, NULL, WUNTRACED);

	} else if (strcmp(program, BG) == 0) {
		if (sus_child_size != -1) resume_child();
		
	} else {		
		child = fork();
		switch(child) {
			case -1:
				perror("fork");
				break;
			case 0:
				setpgid(0,0);
				execvp(program, argv);
				perror(program);
				exit(EXIT_FAILURE);
				break;
			default:
				waitpid(child, NULL, WUNTRACED);
				break;
		}
		
	}

}

void exec_subshell(node_t *node) {
	pid_t child_pid = fork();
	switch(child_pid) {
		case -1:
			perror("fork");
			break;
		case 0:	
			run_command(node->detach.child);
			exit(EXIT_SUCCESS);
		default:
			waitpid(child_pid, NULL, WUNTRACED);
	}
}
void exec_detach(node_t *node) {
	pid_t child_pid = fork();
	switch(child_pid) {
		case -1:
			perror("fork");
			break;
		case 0:	
			run_command(node->detach.child);
			exit(EXIT_SUCCESS);
			
	}
	
}
				

void run_command(node_t *node)
{
 
	switch(node->type) {
		case NODE_PIPE:
			exec_pipe(node);
			break;
		case NODE_SEQUENCE:
			exec_sequence(node);
			break;
		case NODE_REDIRECT:
			exec_redirect(node);
			break;
		case NODE_COMMAND:
			exec_command(node);
			break;
		case NODE_SUBSHELL:
			exec_subshell(node);
			break;
		case NODE_DETACH:
			exec_detach(node);
			break;
	}
	if (prompt) {
	  	prompt = parse_prompt();
	}
}

