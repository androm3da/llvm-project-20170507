//===- llvm/System/Unix/Program.cpp -----------------------------*- C++ -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Reid Spencer and is distributed under the 
// University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific portion of the Program class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include <llvm/Config/config.h>
#include "Unix.h"
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <iostream>

extern char** environ;

namespace llvm {
using namespace sys;

// This function just uses the PATH environment variable to find the program.
Path
Program::FindProgramByName(const std::string& progName) {

  // Check some degenerate cases
  if (progName.length() == 0) // no program
    return Path();
  Path temp;
  if (!temp.setFile(progName)) // invalid name
    return Path();
  if (temp.executable()) // already executable as is
    return temp;

  // At this point, the file name is valid and its not executable
 
  // Get the path. If its empty, we can't do anything to find it.
  const char *PathStr = getenv("PATH");
  if (PathStr == 0) 
    return Path();

  // Now we have a colon separated list of directories to search; try them.
  unsigned PathLen = strlen(PathStr);
  while (PathLen) {
    // Find the first colon...
    const char *Colon = std::find(PathStr, PathStr+PathLen, ':');

    // Check to see if this first directory contains the executable...
    Path FilePath;
    if (FilePath.setDirectory(std::string(PathStr,Colon))) {
      FilePath.appendFile(progName);
      if (FilePath.executable())
        return FilePath;                    // Found the executable!
    }

    // Nope it wasn't in this directory, check the next path in the list!
    PathLen -= Colon-PathStr;
    PathStr = Colon;

    // Advance past duplicate colons
    while (*PathStr == ':') {
      PathStr++;
      PathLen--;
    }
  }
  return Path();
}

static void RedirectFD(const std::string &File, int FD) {
  if (File.empty()) return;  // Noop

  // Open the file
  int InFD = open(File.c_str(), FD == 0 ? O_RDONLY : O_WRONLY|O_CREAT, 0666);
  if (InFD == -1) {
    ThrowErrno("Cannot open file '" + File + "' for "
              + (FD == 0 ? "input" : "output") + "!\n");
  }

  dup2(InFD, FD);   // Install it as the requested FD
  close(InFD);      // Close the original FD
}

static bool Timeout = false;
static void TimeOutHandler(int Sig) {
  Timeout = true;
}

int 
Program::ExecuteAndWait(const Path& path, 
                        const char** args,
                        const char** envp,
                        const Path** redirects,
                        unsigned secondsToWait
) {
  if (!path.executable())
    throw path.toString() + " is not executable"; 

#ifdef HAVE_SYS_WAIT_H
  // Create a child process.
  int child = fork();
  switch (child) {
    // An error occured:  Return to the caller.
    case -1:
      ThrowErrno(std::string("Couldn't execute program '") + path.toString() + 
                 "'");
      break;

    // Child process: Execute the program.
    case 0: {
      // Redirect file descriptors...
      if (redirects) {
        if (redirects[0])
          if (redirects[0]->isEmpty())
            RedirectFD("/dev/null",0);
          else
            RedirectFD(redirects[0]->toString(), 0);
        if (redirects[1])
          if (redirects[1]->isEmpty())
            RedirectFD("/dev/null",1);
          else
            RedirectFD(redirects[1]->toString(), 1);
        if (redirects[1] && redirects[2] && 
            *(redirects[1]) != *(redirects[2])) {
          if (redirects[2]->isEmpty())
            RedirectFD("/dev/null",2);
          else
            RedirectFD(redirects[2]->toString(), 2);
        } else {
          dup2(1, 2);
        }
      }

      // Set up the environment
      char** env = environ;
      if (envp != 0)
        env = (char**) envp;

      // Execute!
      execve (path.c_str(), (char** const)args, env);
      // If the execve() failed, we should exit and let the parent pick up
      // our non-zero exit status.
      exit (errno);
    }

    // Parent process: Break out of the switch to do our processing.
    default:
      break;
  }

  // Make sure stderr and stdout have been flushed
  std::cerr << std::flush;
  std::cout << std::flush;
  fsync(1);
  fsync(2);

  struct sigaction Act, Old;

  // Install a timeout handler.
  if (secondsToWait) {
    Timeout = false;
    Act.sa_sigaction = 0;
    Act.sa_handler = TimeOutHandler;
    sigemptyset(&Act.sa_mask);
    Act.sa_flags = 0;
    sigaction(SIGALRM, &Act, &Old);
    alarm(secondsToWait);
  }

  // Parent process: Wait for the child process to terminate.
  int status;
  while (wait(&status) != child)
    if (secondsToWait && errno == EINTR) {
      // Kill the child.
      kill(child, SIGKILL);
        
      // Turn off the alarm and restore the signal handler
      alarm(0);
      sigaction(SIGALRM, &Old, 0);

      // Wait for child to die
      if (wait(&status) != child)
        ThrowErrno("Child timedout but wouldn't die");
        
      return -1;   // Timeout detected
    } else {
      ThrowErrno("Error waiting for child process");
    }

  // We exited normally without timeout, so turn off the timer.
  if (secondsToWait) {
    alarm(0);
    sigaction(SIGALRM, &Old, 0);
  }

  // If the program exited normally with a zero exit status, return success!
  if (WIFEXITED (status))
    return WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    throw std::string("Program '") + path.toString() + 
          "' received terminating signal.";
    
#else
  throw std::string(
    "Program::ExecuteAndWait not implemented on this platform!\n");
#endif
  return 0;
}

}
// vim: sw=2 smartindent smarttab tw=80 autoindent expandtab
