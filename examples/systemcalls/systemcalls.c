#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

    if (cmd == NULL)
        return false;

    int ret = system(cmd);
    if (ret == -1 || !WIFEXITED(ret) || !WEXITSTATUS(ret) == 0)
        return false;

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

    pid_t pid = fork();
    if (pid == 0) {
        int ret = execv(command[0], command);
        if (ret == -1) {
            exit(1);
        }
    } else if (pid == -1) {
        return false;
    }

    int status;
    int ret = waitpid(pid, &status, 0);
    if (ret == -1) return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return false;

    va_end(args);
    return true;
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

    FILE *f = fopen(outputfile, "w");
    if (f == NULL) return false;

    pid_t pid = fork();
    if (pid == 0) {
        int ret = dup2(fileno(f), fileno(stdout));
        if (ret == -1) exit(1);

        ret = execv(command[0], command);
        if (ret == -1) exit(1);
    } else if (pid == -1) {
        return false;
    }

    int status;
    int ret = waitpid(pid, &status, 0);
    if (ret == -1) return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return false;


    va_end(args);
    return true;
}
