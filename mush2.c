#include <stdio.h>
#include <stdlib.h>
#include <mush.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#define READ 0
#define WRITE 1

void usage(char* name);
void prompt();
void cdfun(pipeline p);
void singlefun(pipeline p);
void pipefun(pipeline p);
void handler(int num);

int main(int argc, char* argv[])
{
    /* variable declarations */
    FILE* infile = NULL;    
    char* line = NULL;
    pipeline pipe = NULL;
    int keeprun = 0;
    int lineno = 1;
    int batch = 1;
    struct sigaction sa;

    /* set up signal handling */
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, 0);

    /* check for usage */   
    if(argc < 1)
    {
	usage(argv[0]);
	exit(EXIT_FAILURE);
    } 

    /* if assign file to infile */
    if(argc == 2)
    {
	infile = fopen(argv[1], "r");
	
	if(infile == NULL)
	{
	    perror(argv[1]);
	    exit(EXIT_FAILURE);
	}
        batch = 0;	
    }
    else
    {
	infile = stdin;
    }

    /* set run flag to true */
    keeprun = 1;

    /* display prompt */
    prompt(batch);    

    /* run shell forever */
    while(keeprun)
    {
        /* check if readLongString worked */
        if( (line = readLongString(infile)) == NULL)
        {
	    /* check if for EOF */
            if( feof(infile) )
	    {
		keeprun = 0;
		if(batch != 0)
		{
	          printf("\n");
		}
	    }
        }
	else
	{
	    /* pass line to crack_pipeline */
	    pipe = crack_pipeline(line);

	    if(pipe == NULL)
	    {
	        /* pipeline is empty */
		if( clerror == E_EMPTY)
		{
		    fprintf(stderr, "Invalid null command, line %d.\n", lineno);
		}
		
		/* increment line count */
		lineno++;

		/* free line */
		free(line);
	    }
	    else
	    {
		/* check for cd argument */
	        if( strcmp(pipe->stage->argv[0], "cd") == 0)
	        {
		    cdfun(pipe);
	        }

		/* check for single argument */
		if(pipe->length == 1)
		{
		    /* call single function */
		    singlefun(pipe);		    
		}
	
		/* pipe */
		if(pipe->length > 1)
		{
		    /* call pipe function */
		    pipefun(pipe);
		}
	    }
	}

	/* check is run flag is true */
	if(keeprun)
  	{
	    prompt(batch);
	}
    }

    yylex_destroy(); 
    return 0;
}

/* usage function */
void usage(char* name)
{
    fprintf(stderr, "usage: %s\n", name);
}

/* set prompt of program */
void prompt(int b)
{
    /* check if in interactive mode */

    /* check if stdin and stdout are tty */
    if(b && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) )
    {  
	/* prompt */ 
        printf("8-P ");
	fflush(stdout);
    }
}

/* cd function */
void cdfun(pipeline p)
{
    char* dir = NULL;

    /* if argv[1] is empty go to home directory */   
    if(p->stage->argv[1] == NULL)
    {
	/* set dir to home */
	if( (dir = getenv("HOME")) == NULL)
	{
	    dir = getpwuid(getuid())->pw_dir;
	}
    }
    else
    {
	/* set dir to argument passed in */
	dir = p->stage->argv[1];
    }
	
    /* change directory */
    if( (chdir(dir)) == -1)
    {
	perror("Unable to change to directory");
    }   
}

/* single stage function */
void singlefun(pipeline p)
{
    pid_t pid = 0;
    int fdin;
    int fdout;

    /* call fork() */
    pid = fork();

    /* check for for error */ 
    if(pid < 0)
    {
	perror("singlefun fork()");
	exit(EXIT_FAILURE);
    }

    /* child process */
    if(pid == 0)
    {
	/* check for infile redirection */
	if(p->stage[0].inname != NULL)
	{
	    if( -1 == (fdin = open(p->stage[0].inname, O_RDONLY)) )
	    {
		perror("fdin");
		exit(EXIT_FAILURE);
	    }
	    
	    if( -1 == dup2(fdin, STDIN_FILENO) )
	    {
		perror("dup2 fdin");
		exit(EXIT_FAILURE);
	    }
	    close(fdin);	    	    
	}

	/* check for outfile redirection */
	if(p->stage[0].outname != NULL)
	{
	    if( -1 == (fdout = creat(p->stage[0].outname, 0644)) )
	    {
		perror("fdout");
		exit(EXIT_FAILURE);
	    }

	    if( -1 == dup2(fdout, STDOUT_FILENO) )
	    {
		perror("dup2 fdout");
		exit(EXIT_FAILURE);
	    }
	    close(fdout);	  
	}

	/* execute program */
	if( -1 == (execvp(p->stage[0].argv[0], p->stage->argv)) )
	{
	    perror(p->stage[0].argv[0]);
	    exit(EXIT_FAILURE);
	}
    }

    /* wait for program to finish */
    if( -1 == wait(NULL) )
    {
        if(errno == EINTR)
	{
	   /* nothing */
        }
	else
	{
	    perror("wait() singlefun");
	}
    }

}

/* pipe function */
void pipefun(pipeline p)
{
    pid_t pid;
    int i, j;
    int pipe1[2];
    int pipe2[2];
    int status;
    int fdin, fdout;

    /* iterate through stages */
    for(i = 0; i < p->length; i++)
    {
	  /* if i is even open pipe1 */
	  if(i % 2 == 0)
	  {
	    if(pipe(pipe1) == -1)
	    {
		perror("First pipe");
		exit(EXIT_FAILURE);
	    }
	  }

	  /* if i is odd open pipe2 */
	  if(i % 2 != 0)
	  {
	    if(pipe(pipe2) == -1)
	    {
		perror("Second pipe");
		exit(EXIT_FAILURE);
	    }
	  }

	/* call fork() */
	pid = fork();

	/* check for fork() error */
        if(pid < 0)
        {
	    perror("pipefun fork()");
	    exit(EXIT_FAILURE);
        }

	/* child process */
	if(pid == 0)
	{
	    /* first command */
	    if(i == 0)
	    {
		/* check for infile redirection */
		if(p->stage[0].inname != NULL)
		{
	    	    if( -1 == (fdin = open(p->stage[0].inname, O_RDONLY)) )
	            {
		      perror("fdin");
		      exit(EXIT_FAILURE);
	    	    }
	    
	    	   if( -1 == dup2(fdin, STDIN_FILENO) )
	    	   {
		     perror("dup2 fdin");
		     exit(EXIT_FAILURE);
	    	   }
	    	   close(fdin);	    	    
		}

		/* make output go to pipe1 */
		if( -1 == dup2(pipe1[WRITE], STDOUT_FILENO) )
	    	{
		    perror("dup2");
		    exit(EXIT_FAILURE);
	    	}
	    /* end of if */
	    }
	    /* last command change stdin for necessary pipe */
	    else if( (p->length - 1) == i)
	    {
		/* check for outfile redirection */
		if(p->stage[i].outname != NULL)
		{

	    	if( -1 == (fdout = creat(p->stage[i].outname, 0644)) )
	    	{
		  perror("fdout");
		  exit(EXIT_FAILURE);
	    	}

	    	if( -1 == dup2(fdout, STDOUT_FILENO) )
	    	{
	 	  perror("dup2 fdout");
		  exit(EXIT_FAILURE);
	    	}
	    	close(fdout);	  
		}

		/* if i is odd get input from pipe2 */
		if(i % 2 != 0)
		{
		    if( -1 == dup2(pipe1[READ], STDIN_FILENO) )
	    	    {
		        perror("dup2");
		        exit(EXIT_FAILURE);
	    	    }
   
		}
		/* if i is even get input from pipe1 */
		/* pipe1 is first created in iteration */
		if(i % 2 == 0)
		{
		    if( -1 == dup2(pipe2[READ], STDIN_FILENO) )
	    	    {
		        perror("dup2");
		        exit(EXIT_FAILURE);
	    	    }

		}
	    /* end of if-else */
	    }
	    /* middle command both pipes are used */
	    else
	    {
		/* if i is odd */
		if(i % 2 != 0)
		{
		    /* get input from pipe1 */
		    if( -1 == dup2(pipe1[READ], STDIN_FILENO) )
	    	    {
		        perror("dup2");
		        exit(EXIT_FAILURE);
	    	    }
		
   		    /* make output go to pipe2 */
		    if( -1 == dup2(pipe2[WRITE], STDOUT_FILENO) )
	    	    {
		        perror("dup2");
		        exit(EXIT_FAILURE);
	    	    }
		}
		/* if i is even read from pipe2 */
		/* write to pipe 1 */
		if(i % 2 == 0)
		{
		    /* get input from pipe2 */
		    if( -1 == dup2(pipe2[READ], STDIN_FILENO) )
	    	    {
		        perror("dup2");
		        exit(EXIT_FAILURE);
	    	    }

		    /* make output go to pipe1 */
		    if( -1 == dup2(pipe1[WRITE], STDOUT_FILENO) )
	    	    {
		        perror("dup2");
		        exit(EXIT_FAILURE);
	    	    }
		}
		/* end of else */
	    }	

	    /* run execvp command check for error */
	    if( -1 == (execvp(p->stage[i].argv[0], p->stage[i].argv)) )
	    {
		perror(p->stage[i].argv[0]);
		exit(EXIT_FAILURE);
	    }
	
	/* end of child */
	}

	/* parent process */
	else
	{
	/* first command */
	if( i == 0)
	{
	    /* close parent copy of pipe1 */
	    close(pipe1[WRITE]);
	}
	/* last command */
	else if( (p->length - 1) == i)
	{
	    /* check if i is odd */
	    if(i % 2 != 0)
	    {
		/* close parent copy of write end of pipe2 */
		close(pipe2[WRITE]);
	    }
	    /* check if i is even */
	    if(i % 2 == 0)
	    {
		/* close parent copy of write end of pipe1 */
		close(pipe1[WRITE]);
	    }
	}
	/* middle commands */
	else
	{
	    /* check if i is odd */
	    if(i % 2 != 0)
	    {
		/* close pipes */
		close(pipe1[READ]);
		close(pipe2[WRITE]);
	    }
	    /* check if i is even */
	    if(i % 2 == 0)
	    {
		/* close pipes */
		close(pipe2[READ]);
		close(pipe1[WRITE]);
	    }
	}
	}
	/* end of parent */
    } 
    /* end of for loop */

    j = 0;
    while(j < p->length)
    {
	/* wait for children to terminate */
	if( wait(&status) == -1)
	{
	    if(errno == EINTR)
 	    {
	        /* nothing */
	    }
            else
	    {
	        perror("wait() pipes");
	    }
            
	}
	j++;

    }
}

/* signal handling function */
void handler(int num)
{
    /* empty handler */
    putchar('\n');
}


