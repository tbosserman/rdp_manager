#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include "rdp_manager.h"

#define MAXMATCH 2
#define TRUE	1
#define FALSE	0
#define PATTERN "^This is FreeRDP version ([0-9.]+) "

extern void mylog(char *fmt, ...);

/************************************************************************
 ********************     DETECT_FREERDP_VERSION     ********************
 ************************************************************************/
int
detect_freerdp_version(char *progname)
{
    int		i, code, pipefd[2], status, matched;
    pid_t	pid;
    char	line[128], *basename;
    FILE	*fp;
    regex_t	regex;
    regmatch_t	matches[MAXMATCH];

    if (pipe(pipefd) < 0)
    {
	mylog("pipe() failed: %s\n", strerror(errno));
	return(-1);
    }
    if ((pid = fork()) == 0)
    {
	close(1);
	if (dup(pipefd[1]) < 0)
	{
	    mylog("dup() failed: %s\n", strerror(errno));
	    exit(1);
	}
	close(pipefd[0]);
	close(pipefd[1]);
	for (i = strlen(progname) - 1; i >= 0 && progname[i] != '/'; --i);
	basename = progname + i + 1;
	execl(progname, basename, "--version", NULL);
	mylog("error executing %s: %s\n", progname, strerror(errno));
	exit(1);
    }
    
    if (pid < 0)
    {
	mylog("fork() failed: %s\n", strerror(errno));
	return(-1);
    }
    close(pipefd[1]);
    fp = fdopen(pipefd[0], "r");

    if ((code = regcomp(&regex, PATTERN, REG_EXTENDED)) != 0)
    {
	mylog("regcomp() returned %d", code);
	return(-1);
    }

    matched = FALSE;
    while (fgets(line, sizeof(line), fp))
    {
	if (regexec(&regex, line, MAXMATCH, matches, 0) == 0)
	{
	    matched = TRUE;
	    break;
	}
    }
    fclose(fp);
    waitpid(pid, &status, 0);
    regfree(&regex);

    if (matched)
    {
	switch(line[matches[1].rm_so])
	{
	    case '2': return(FREERDPV2);
	    case '3': return(FREERDPV3);
	}
    }

    return(-1);
}
