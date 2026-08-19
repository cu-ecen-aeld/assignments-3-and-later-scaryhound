#include "systemcalls.h"
#include<stdlib.h>  //for system() and EXIT failure
#include<unistd.h>  //for fork(), execv(), dup2(), and close()
#include<sys/wait.h>  //for waitpid(), WIFEXITED and WEXITSTATUS
#include<fcntl.h>   //for open(), O_CREAT, O_TRUNC and O_WRONLY

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/


bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
	int result = system (cmd);
	if (result != 0)
	{
		return false;
	}
    return true;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    fflush(stdout);
    pid_t pid = fork();
    if(pid == -1)
    {
    	va_end(args);
    	return false;
    }
    else if(pid == 0)
    {
    	execv(command[0], command);
    	exit (EXIT_FAILURE);
    }
    else 
    {
    	int status;
    	if(waitpid(pid, &status, 0)== -1)
    	{
    		return false;
    	}
    	
    	if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
    	{
    		return true;
    	}
    	else
    	{	
    		return false;
    	}
    }

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/

    va_end(args);

}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
   // 1. Open the file (with write access, truncate, and create flags)
    int fd = open(outputfile, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) {
        // ERROR: Failed to open the file
        return false;
    }

    // 2. Flush the print queue
    fflush(stdout);

    // 3. Clone the process
    pid_t pid = fork();

    if (pid == -1) {
        // ERROR: Clone failed
        close(fd);
        return false;
    } 
    else if (pid == 0) {
        // CHILD PROCESS
        // 4. The Wiring: Unplug the screen (1) and plug in the file (fd)
        if (dup2(fd, 1) < 0) {
            close(fd);
            exit(EXIT_FAILURE); 
        }
        
        // We don't need this extra handle anymore now that dup2 did its job
        close(fd); 

        // 5. Replace our brain with the new command
        execv(command[0], command);
        
        // If we reach this line, execv() failed!
        exit(EXIT_FAILURE);
    } 
    else {
        // PARENT PROCESS
        // The parent doesn't need to write to the file, so close its copy
        close(fd);
        
        // Wait for the child to finish
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            return false;
        }

        // Check if the child exited normally AND returned a 0 (success)
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return true;
        } else {
            return false;
        }
    }


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

    va_end(args);

    return true;
}
