// 
// tsh - A tiny shell program with job control
// 
// <Put your name and login ID here>
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>
#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];
  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); // this line will return true if it is a background job, it is false if it is a foreground job
  pid_t pid;
  struct job_t *job;
  sigset_t mask;

  if(!builtin_cmd(argv)) // if this is not true, then a child process is forked and executed. If it is, then it will use the builtin_cmd function
  {
    pid = fork();
    setpgid(0,0); // this sets the group ID for foreground processes so background processes dont get linked with foreground processes

    if(pid == 0) // if the return value of fork is 0, then we are in the child process
    {
      execv(argv[0],argv); // this creates the child process, PID remains unchanged
      printf("%s: Command not found\n", argv[0]); // if the command is not found, it will return a neg if it is not found. 
      exit(0); // exits the shell so that there won't be multiple instances of the shell running.
    }

    else // this condition is when it is a parent 
    {
      if(!bg) // this will execute if it is in a foreground process
      {
        addjob(jobs,pid,FG,cmdline); // this will add a job to struct with a foregound state
        waitfg(pid); // this line prevents zombies, it will make the parent wait for the child and reap it after
      }
      else
      {
        addjob(jobs, pid, BG, cmdline); // this adds a job to struct with a background state
        job = getjobpid(jobs, pid); // gets the job ID of the new job
        printf("[%d] (%d) %s", job->jid, pid, cmdline);
      }
      
    }
    
  }
  if (argv[0] == NULL)  
    return;   /* ignore empty lines */

  return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
  string cmd(argv[0]); // uses the same CMD in do_bgfg

  if(cmd == "quit") // the quit command
  {
    exit(0);
  }

  else if((strcmp(argv[0],"bg")) == 0 || (strcmp(argv[0],"fg") == 0))
  {
    do_bgfg(argv);
    return 1;
  }

  else if(cmd == "jobs")
  {
    listjobs(jobs); // this line calls the list of jobs
    return 1;
  }
  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  struct job_t *jobp=NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  string cmd(argv[0]);
  if((strcmp(argv[0],"bg")) == 0)
  {
  	jobp->state = BG; //this changes the state to background
  	kill(-jobp->pid, SIGCONT); //this will tell the system to run the job again
  	printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
  }
  if((strcmp(argv[0],"fg")) == 0)
  {
  	jobp->state = FG; //this changes the state to background
  	kill(-jobp->pid, SIGCONT); 
  	waitfg(jobp->pid); //this lines ensures that there is only 1 foreground process running
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  while(pid ==fgpid(jobs))
  {
    sleep(.1); // when the PID is = to foreground PID, does nothing.
  } // when it is not in a foreground process, it will stop sleeping
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig)
{
  int status; // this is where waitpid stores the value, if the value is 0, then it will return 0 otherwise 
  pid_t pid; // this specifies which child process the caller wants to wait for
  // if the value is -1, then waitpid will wait for any child process to end


// this tells us that if there is a child process running, if so it needs to wait until it is done executing

  while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0 ) // WNOHANG checks the child process without interupting the caller
  // WUNTRACED is for the non terminated processes
  {
    if(WIFSTOPPED(status)) // if the process is stopped, change the state to stopped, will return true if there is a child process
    {
      struct job_t *job = getjobpid(jobs,pid);
      job -> state =  ST;
      printf("Job [%d] (%d) stopped by signal 20\n", job->jid, pid);
      return;
    }

    else if(WIFSIGNALED(status)) // this catches the ctrl-c
    {
      struct job_t *job = getjobpid(jobs, pid);
      printf("Job [%d] (%d) terminated by signal 2\n", job->jid, pid);
      deletejob(jobs,pid);

    }

    else
    {
      deletejob(jobs,pid);
    }
    
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
  pid_t pid =fgpid(jobs);
  if(pid > 0) // if this is = to 0 that means that it is not a foreground job
  {
    kill(-pid, sig); // this is the default action for sigint, it kills the process
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
  pid_t pid = fgpid(jobs);
  if(pid > 0)
  {
    kill(-pid, sig);
  }
  return;
}

/*********************
 * End signal handlers
 *********************/




