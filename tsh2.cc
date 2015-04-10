// 
// tsh - A tiny shell program with job control

// Started: November 26, 2014

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
// waitfg, sigchld_handler, sigst_handler, sigint_handler
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
  int bg;
  pid_t pid;
  sigset_t SignalBlockingMask;
  //Create a set of signals for the system to block on command */
  sigemptyset(&SignalBlockingMask);
  sigaddset(&SignalBlockingMask, SIGCHLD); // Added SIGCHLD to the list of signals we want to block
  // we want to be able to block (temporarily) any SIGCHLD signals going to the parent
  // process to avoid issues with race conditions. We want to make sure that parent executes
  // what it needs to execute before any handler caused by the child inteferes with it
  // 
  // This will also help us make sure that any child process that executes and then terminates
  // very quickly, won't send a SIGCHLD signal to the kernel before the parent process has had a 
  // chance to add the child process to the jobs lists


  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  bg = parseline(cmdline, argv); 
  if (argv[0] == NULL)  
    return;   /* ignore empty lines */
  
  if (!builtin_cmd(argv)) // If its not a built in command... otherwise it would be excuted here by builtin_cmd
  {
    // establish the mask to block SIGCHLD signals to the parent function
  	sigprocmask(SIG_BLOCK, &SignalBlockingMask, 0);

  	if ((pid = fork()) < 0) // the only time this should happen is if fork fails
  	{
  		printf("There has been a forking error");
  		return;
  	}



  	if (pid ==0) // if its the child process...
  	{
      setpgid(0,0);  //this will set up a new group PID for the child process. This is crucial because before this,
      // the shell (tsh) is running in the foreground and if it calls any child process, that child process is also
      // part of the shells process group. Our SIGINT handler sends a SIGINT signal to every process in the group which 
      // in this case would include the shell, so this would also kill the shell. Not what we need.

  		if (execvp(argv[0],argv) <0) // if it's not built in and running results in an error (<0), the comamnd doesn't exist
  		{                             // otherwise the command is run here
  			printf("%s: Comand not found.\n",argv[0]);
  			exit(0);
  		}
  	}

  	else // if its the parent process
  	{
  		if (bg == 1) // if the command was specified to be ran in the background...
  		{
  			addjob(jobs,pid,BG,cmdline); //add job to list of jobs with BG status
  			printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
  		}
  		else //otherwise add it to list of foreground jobs
      {
        addjob(jobs, pid, FG, cmdline);
  		}
    // NOW we can unmask the parent process because we have succesfully added the jobs to the job list
    // and so now we can allow the kernel to recieve SIGCHLD signals and therefore remove child processes
  	sigprocmask(SIG_UNBLOCK, &SignalBlockingMask, 0);
    waitfg(pid); // This will wait until the current foreground process is no longer the foreground process
    //and then proceed
  	}
  	
  }
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
  
  
  if (!strcmp(argv[0], "quit")) // if the users command is quit... 
  {
  	exit(0);
  }

  if (!strcmp(argv[0], "jobs")) // if the users command is jobs...
  {
  	listjobs(jobs);
    return 1;
  }

  if (!strcmp(argv[0], "fg"))
  {
    do_bgfg(argv);
    return 1;
  }

  if (!strcmp(argv[0], "bg"))
  {
    do_bgfg(argv);
    return 1;
  }
  
  
  // if cmd isn't one of the built ins, return 0
  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  pid_t pid;
  struct job_t *jobp=NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid = atoi(argv[1]);
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
  //string cmd(argv[0]);
  //
   pid = jobp->pid; //update the pid to current jobs pid
   if (jobp->state == ST) // if the pointed at job is a stopped at the moment
   {
    if (!strcmp(argv[0],"fg")) // if the user call the fg method, we need to restart the process in the FG
    {
      jobp->state = FG; // changes the jobs state to foreground
      kill(-pid,SIGCONT); // kills the process and all of its child processes, but then by sending back the
                         // the SIGCONT signal, it continues its execution, this time in the FG
      waitfg(jobp->pid); //let the current foreground process finish before startinga new one
    } 

    if (!strcmp(argv[0], "bg"))
    {
      printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
      jobp->state = BG; // change the jobs state to background, and then restart it with SIGCONT
      kill(-pid,SIGCONT);
    }
   }
   if (jobp->state = BG)
   {
    if (!strcmp(argv[0],"fg"))
    {
      jobp->state = FG; //change the state to being a foreground process
      waitfg(jobp->pid); //wait until current foreground process finishes, then continue
      //just changing the jobs state will add it to the list of foreground queued jobs and it 
      //will execute once the current foreground process finishes
    }
   }





  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  struct job_t *currentJob = getjobpid(jobs, pid);
  while (currentJob->state == FG) // sleep until currentJob is no longer running in the foreground
    sleep(1);
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
  //establish an int so that we can pass a pointer to it when calling waitpid so we can later
  // check the status of waitpid	
  int returnStatus;
  pid_t pid;								
  // WHOHANG allows for waitpid to return immidiately even if the child process is not terminated
  // WUNTRACED tells waitpid to report on stopped children as well as terminated children
  while ((pid = waitpid(-1, &returnStatus, WNOHANG | WUNTRACED)) > 0 ) 
  {
      if (WIFEXITED(returnStatus)) 
      { // The above will return true if the child was terminated normally. In the case, simply delete the job
          deletejob(jobs, pid);
      }
      if (WIFSIGNALED(returnStatus)) 
      { // see if the child process was terminated by a signal that wasn't handled by a handler (SIGINT, SIGSTP) specified below
          printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(returnStatus));
          																	   //WTERMSIG checks return status of process stopped by signal
          deletejob(jobs,pid);
      }
      if (WIFSTOPPED(returnStatus)) 
      { /*checks if child process that caused return is currently stopped */
          getjobpid(jobs, pid)->state = ST;
          printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(returnStatus));
          																	//This message check can only be applied when WIFSTOPPED returns true
      }
  }
      
  if (pid < 0 && errno != ECHILD) 
  {
      printf("waitpid() error, child doesn't exist: %s\n", strerror(errno));
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

pid_t processPid = fgpid(jobs);
if (fgpid(jobs) != 0)
{
kill(-processPid, SIGINT); //by including the negative sign, we call all the process with the group PID of 
//the processes GPID. This will kill the foreground process as well as any children that it may have
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

pid_t processPid = fgpid(jobs);
if (fgpid(jobs) != 0) 
{
  kill(-processPid, SIGTSTP); //by including the negative sign, we call all the process with the group PID of 
//the processes GPID. This will kill the foreground process as well as any children that it may have
}
return;
}

/*********************
 * End signal handlers
 *********************/




