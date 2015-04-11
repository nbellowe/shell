// gg
// tsh - A tiny shell program with job control
//
// Nathan Bellowe 102343874 and Sarah Niemeyer 100027519
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
    pid_t pid;
    //
    // The 'bg' variable is TRUE if the job should run
    // in background mode or FALSE if it should run in FG
    //
    int bg = parseline(cmdline, argv);
    if (argv[0] == NULL)
        return;   /* ignore empty lines */

    sigset_t mask;
    Sigemptyset(&mask);                     //mask sigchild signal until after
    Sigaddset(&mask, SIGCHLD);              //job is added so as to not delete
    Sigprocmask(SIG_BLOCK, &mask, NULL);    //a non-existant job

    if(!builtin_cmd(argv)){ // Handle if the first arg is quit/fg/bg/jobs
        //if the first word is not a builtin command, it must be a program.
        if((pid=Fork()) == 0){ //Therefore, fork a child program.
            //Note Fork() returns 0 and enters this block if it is the child.

            setpgid(0,0); // assign to new pgid so Signals don't kill shell?
            //Sarah I don't understand this pgif. Lets talk about it next time.

            Execve(argv[0], argv, NULL);
            return; //don't want child process becoming a shell! :)
        }
        addjob(jobs, pid, (bg ? BG : FG), cmdline); //Add to jobs with
                                                    //  newjob.state == bg.
        Sigprocmask(SIG_UNBLOCK, &mask, NULL); // now that job is added, unblock
        if(!bg) //If its a foreground task
        {
            waitfg(pid); //Foreground tasks need to wait until they are finished.
        }
    }
    return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is ia C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv)
{
    if(!strcmp(argv[0], "quit")){ // if the input is 'quit'
        sigchld_handler(0); //good idea to kill child processes.
        exit(0); //exit shell
    }
    if(!strcmp(argv[0], "jobs")){ // if its jobs
        listjobs(jobs); //list running jobs.
        return 1;
    }
    if(!strcmp(argv[0], "fg") || !strcmp(argv[0], "bg")){ //if its 'fg' or 'bg'
        do_bgfg(argv);
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

    //BEGIN OUR CODE

    pid_t pid = jobp -> pid;
    int oldstate = jobp -> state;
    int newstate = !strcmp(argv[0], "fg") ? FG : BG;
    jobp -> state = newstate;
    if(oldstate == ST)//if the job has stopped we need to send a signal
    {                      //to continue.
        Kill(-pid, SIGCONT); //kill really sends signal to continue program
    }
    if(newstate == FG) //if its a foreground job
    {
        waitpid(-pid, NULL, 0); //wait for task to complete because 'fg'
    }
    return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
    while(fgpid(jobs)==pid){   //spin while the inputted pid is still the fg pid
      Sleep(100);   //blocking call to sleep.
    }
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
    pid_t pid;
    int CODE;

    while(1){
    	pid = waitpid(-1, &CODE, WNOHANG|WUNTRACED); //get zombies --
                                                     //don't hang & untraced
        if(pid <= 0){ return; } //Base case: no more zombies
        if ( WIFEXITED(CODE) || WIFSIGNALED(CODE)) { //remove terminated jobs.
    		deletejob(jobs, pid);
    	}else if( WIFSTOPPED(CODE) ){ //But if stopped, just change the state.
    		getjobpid(jobs, pid) -> state = ST;
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
    pid_t fg = fgpid(jobs); //pid for fg process.
    if(fgpid(jobs)!=0){ //if there is one...
        Kill(-fg, SIGINT); //kill it.
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

    pid_t fg = fgpid(jobs); //pid for fg process.
    if(fgpid(jobs)!=0){ //if there is one...
        Kill(-fg, SIGTSTP); //stop it.
        getjobpid(jobs,fg)->state = ST; //Set state to stopped.
    }
    return;

}
/*********************
 * End signal handlers
 *********************/
