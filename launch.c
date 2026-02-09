#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include "rdp_manager.h"

#define MAX_ARGS	32

extern entry_t		entries[MAX_ENTRIES];
extern GtkBuilder	*glade_xml;
extern options_t	global_options;
extern char		config_dir[];
extern pid_t		xfreerdp_pid;

extern gboolean		test_connect(char *host, int port);
extern void		mylog(char *fmt, ...);
extern int		detect_freerdp_version(char *progname);

/************************************************************************
 ********************           GEN_VECTOR           ********************
 ************************************************************************/
char *
gen_vector(char *fmt, ...)
{
    va_list	ap;
    char	temp[1024];

    va_start(ap, fmt);
    vsprintf(temp, fmt, ap);
    return(strdup(temp));
}

/************************************************************************
 ********************             LAUNCH             ********************
 ************************************************************************/
void
launch()
{
    FILE		*fp;
    int			i, fd, rownum, fnum, portnum, version;
    gboolean		multimon;
    char		**fields, *user, *passwd, *gw_user, *gw_passwd;
    char		*args[MAX_ARGS], *temp, *p, logfile[1024], *host;
    char		*msg, *domain, *prog;
    GtkListBox		*box;
    GtkListBoxRow	*row;
    GtkEntry		*pwd_widget, *gw_pwd_widget;

    box = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    row = gtk_list_box_get_selected_row(box);
    if (row == NULL)
	return;
    rownum = gtk_list_box_row_get_index(row);
    fields = entries[rownum].fields;

    if ((temp = fields[DISPLAY_SIZE]) == NULL)
	temp = DEFAULT_SIZE;

    // Extract the password(s) from the password window.
    pwd_widget = (GtkEntry *)gtk_builder_get_object(glade_xml, "passwd");
    gw_pwd_widget = (GtkEntry *)gtk_builder_get_object(glade_xml, "gw_passwd");
    passwd = (char *)gtk_entry_get_text(pwd_widget);

    // Extract the value of the checkbox that indicates multi-monitor.
    multimon = atoi(fields[MULTI_MONITOR]);

    // If auto-detect is enabled, detect what version of freerdp we're using.
    prog = global_options.freerdp_path;
    if (prog[0] == '\0')
	prog = XFREERDP;
    if ((version = global_options.freerdp_version) == AUTO_DETECT)
	version = detect_freerdp_version(prog);
    if (version < 0)
    {
	alert("Unable to detect FreeRDP version.");
	return;
    }
    mylog("Detected FreeRDP version: %d\n", version);

    // Build up the argument list to be passed to xfreerdp
    domain = fields[DOMAIN];
    fnum = 0;
    user = fields[USERNAME];
    args[fnum++] = gen_vector("xfreerdp");
    args[fnum++] = gen_vector("/sound");
    args[fnum++] = gen_vector("/audio-mode:0");
    if (version == FREERDPV3)
	args[fnum++] = gen_vector("/cert:ignore");
    else
	args[fnum++] = gen_vector("/cert-ignore");
    if (multimon)
	args[fnum++] = gen_vector("/multimon");
    if (temp[0] != '\0')
	args[fnum++] = gen_vector("/size:%s", temp);
    else
    {
	args[fnum++] = gen_vector("/f");
	args[fnum++] = gen_vector("/floatbar");
    }
    if (domain && domain[0] != '\0')
	args[fnum++] = gen_vector("/u:%s@%s", user, domain);
    else
	args[fnum++] = gen_vector("/u:%s", user);
    args[fnum++] = gen_vector("/p:%s", passwd);
    args[fnum++] = gen_vector("/v:%s:%s", fields[HOST], fields[PORT]);
    args[fnum] = NULL;
    
    if (fields[GATEWAY][0] != '\0')
    {
	// FreeRDPv3 handles gateways quite differently. The general case looks
	// like this: /gw:g:<gateway>:<port>,u:<user>,d:<domain>,p:<password>
	host = fields[GATEWAY];
	portnum = atoi(fields[GW_PORT]);
	gw_passwd = (char *)gtk_entry_get_text(gw_pwd_widget);
	gw_user = fields[GW_USER];
	if (gw_user[0] == '\0')
	    gw_user = user;
	if (gw_passwd[0] == '\0' || strcmp(user, gw_user) == 0)
	    gw_passwd = passwd;
	if (version == FREERDPV2)
	{
	    args[fnum++] = gen_vector("/g:%s:%s", fields[GATEWAY], fields[GW_PORT]);
	    if (domain)
		args[fnum++] = gen_vector("/gu:%s@%s", gw_user, domain);
	    else
		args[fnum++] = gen_vector("/gu:%s", gw_user);
	    args[fnum++] = gen_vector("/gp:%s", gw_passwd);
	}
	else
	{
	    if (domain)
		args[fnum++] = gen_vector("/gw:g:%s:%s,u:%s,d:%s,p:%s",
		    fields[GATEWAY], fields[GW_PORT], user, domain, gw_passwd);
	    else
		args[fnum++] = gen_vector("/gw:g:%s:%s,u:%s,p:%s",
		    fields[GATEWAY], fields[GW_PORT], user, gw_passwd);
	}
	args[fnum++] = NULL;
    }
    else
    {
	host = fields[HOST];
	portnum = atoi(fields[PORT]);
    }

    // Before we bother to try and launch xfreerdp, let's make sure we can
    // actually connect to the gateway / host.
    if (!test_connect(host, portnum))
    {
	msg = "Unable to connect to %s:%d\nLook in %s/logfile for details";
	alert(msg, host, portnum, config_dir);
	return;
    }

    /*
     * Log the command we're about to execute. IMPORTANT: look for
     * arguments which have passwords in them and replace the password
     * with asterisks before logging.
     */
    for (i = 0; args[i] != NULL; ++i)
    {
	if (memcmp(args[i], "/gp:", 4) == 0 ||
	    memcmp(args[i], "/p:", 3) == 0)
	{
	    temp = strdup(args[i]);
	    p = strchr(temp, ':') + 1;
	    while (*p != '\0')
		*p++ = '*';
	    mylog("%s\n", temp);
	    free(temp);
	}
	else
	    mylog("%s\n", args[i]);
    }

    if ((xfreerdp_pid = fork()) == 0)
    {
	for (i = 3; i <= 255; ++i)
	    close(i);
	snprintf(logfile, sizeof(logfile), "%s/xfreerdp.log", config_dir);
	/*
	 * It really should be impossible for the open and dup calls to
	 * fail, but if they do something really bad is going on so we'll
	 * just give up and exit.
	 */
	if ((fd = open(logfile, O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0)
	    exit(1);
	close(1);
	close(2);
	if (dup(fd) < 0 || dup(fd) < 0)
	    exit(1);
	close(fd);
	close(0);
	execv(prog, args);
	/* Shouldn't get here unless exec craps out completely */
	fp = fopen(logfile, "a");
	fprintf(fp, "Execution of '%s' failed: %s\n", prog, strerror(errno));
	fclose(fp);
	// This sleep() is a MAJOR, UGLY KLUDGE!!! It's here because if
	// the child process exits too quickly then the GUI in the parent
	// gets messed up for some reason. This needs a lot more investigation
	// for a more comprehensive solution.
	// UPDATE: Adding SA_RESTART to sa_flags during sig handler
	// initialization *appears* to have fixed this. Leaving this mess
	// in place (for now) purely for documentation purposes.
	//sleep(2);
	exit(1);
    }

    // alert("Please check your phone for a Duo push authentication request.");

    for (i = 0; args[i] != NULL; ++i)
	free(args[i]);
}
