#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "rdp_manager.h"
#include "version.h"
#include "crypto.h"

#define	MAX_ARGS	32
static char		config_dir[512];
static char		entries_file[1024];

FILE			*logfp;
char			logfile[1024];
entry_t			entries[MAX_ENTRIES];
int			num_entries;
int			mode;

char			*widget_names[] = {
    "entry_name",	"host",		"port",		"username",
    "display_size",	"gw_host",	"gw_port",	"gw_username",
    "ignore_cert",	"xfreerdp_args"
};

extern GtkBuilder	*glade_xml;

/************************************************************************
 ********************             MYLOG              ********************
 ************************************************************************/
void
mylog(char *fmt, ...)
{
    va_list		ap;
    time_t		now;
    struct tm		*tm;

    now = time(NULL);
    tm = localtime(&now);
    fprintf(logfp, "%02d/%02d/%02d %02d:%02d:%02d ", tm->tm_year+1900,
	tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    va_start(ap, fmt);
    vfprintf(logfp, fmt, ap);
    fflush(logfp);
}


/************************************************************************
 ********************       ON_WINDOW1_DESTROY       ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_window1_destroy                      (GObject         *object,
                                        gpointer         user_data)
{
    gtk_main_quit();
}

/************************************************************************
 ********************    ON_WINDOW1_DELETE_EVENT     ********************
 ************************************************************************/
G_MODULE_EXPORT gboolean
on_window1_delete_event                (GtkWidget       *widget,
                                        GdkEvent        *event,
                                        gpointer         user_data)
{
    return FALSE;
}

/************************************************************************
 ********************             ALERT              ********************
 ************************************************************************/
void
alert(const char *fmt, ...)
{
  va_list	ap;
  char		temp[512];
  GtkWidget	*dialog, *window1;

  temp[sizeof(temp)-1] = '\0';
  va_start(ap, fmt);
  vsnprintf(temp, sizeof(temp)-1, fmt, ap);
  va_end(ap);
  window1 = (GtkWidget *)gtk_builder_get_object(glade_xml, "window1");
  dialog = gtk_message_dialog_new(GTK_WINDOW(window1),
    GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", temp);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

/************************************************************************
 ********************            ADD_ROW             ********************
 ************************************************************************/
void
add_row(char *entry_name)
{
    GtkLabel		*label;
    GtkListBox		*listbox;
    GtkListBoxRow	*row;

    label = GTK_LABEL(gtk_label_new(entry_name));
    gtk_label_set_xalign(label, 0);
    row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
    gtk_container_add(GTK_CONTAINER(row), GTK_WIDGET(label));
    gtk_list_box_row_set_selectable(row, TRUE);
    gtk_widget_show(GTK_WIDGET(row));
    gtk_widget_show(GTK_WIDGET(label));
    listbox = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    gtk_list_box_insert(listbox, GTK_WIDGET(row), -1);
}

/************************************************************************
 ********************            HANDLER             ********************
 ************************************************************************/
void
handler(int sig)
{
    int		status;
    pid_t	pid;
    /*
     * We're not doing much here, just reaping the child process so it
     * doesn't turn into a zombie process.
     */
    if ((pid = wait(&status)) < 0)
    {
	mylog("handler: wait(): %s\n", strerror(errno));
	return;
    }
    mylog("Child process %d exited with status %d\n", pid, status);
    if (status != 0)
	alert("xfreerdp exited abnormally.\nLook at xfreerdp.log and "
	      "logfile in\n%s\nfor clues as to what went wrong.", config_dir);
}

/************************************************************************
 ********************          MY_GTK_INIT           ********************
 ************************************************************************/
G_MODULE_EXPORT void
my_gtk_init()
{
    int			i;
    char		*home;
    GtkWidget		*window1;
    struct sigaction	newact, oldact;

    memset(&newact, 0, sizeof(newact));
    newact.sa_handler = handler;
    sigaction(SIGCHLD, &newact, &oldact);

    if ((home = getenv("HOME")) == NULL)
    {
	alert("HOME environment variable not set");
	exit(1);
    }
    snprintf(config_dir, sizeof(config_dir), "%s/.config/rdp", home);
    if (access(config_dir, F_OK) < 0)
    {
	if (mkdir(config_dir, 0700) < 0)
	{
	    alert("error creating %s: %s", config_dir, strerror(errno));
	    exit(1);
	}
    }
    snprintf(entries_file, sizeof(entries_file), "%s/entries.dat", config_dir);
    snprintf(logfile, sizeof(logfile), "%s/logfile", config_dir);
    logfp = fopen(logfile, "a");

    mylog("Loading entries from %s\n", entries_file);
    num_entries = load_entries(entries_file, entries);
    for (i = 0; i < num_entries; ++i)
	add_row(entries[i].fields[ENTRY_NAME]);
    window1 = (GtkWidget *)gtk_builder_get_object(glade_xml, "window1");
    gtk_widget_show_all(window1);
}

/************************************************************************
 ********************            ALLTRIM             ********************
 ************************************************************************/
int
alltrim(char *string)
{
    int		i, first, last, ch;

    for (first = 0; (ch = string[first]) != '\0'; ++first)
    {
	if (!isspace(ch))
	    break;
    }
    i = 0;
    last = -1;
    while ((ch = string[first++]) != '\0')
    {
	string[i] = ch;
	if (!isspace(ch))
	    last = i;
	++i;
    }
    string[++last] = '\0';
    return(last);
}

/************************************************************************
 ********************             CLEAR              ********************
 ************************************************************************/
void
clear_display()
{
    int		i;
    GtkEntry	*widget;

    for (i = 0; i < NUM_FIELDS; ++i)
    {
	widget = (GtkEntry *)gtk_builder_get_object(glade_xml, widget_names[i]);
	switch(i)
	{
	    case DISPLAY_SIZE:
		gtk_entry_set_text(widget, DEFAULT_SIZE);
		break;
	    case PORT:
		gtk_entry_set_text(widget, "3389");
		break;
	    case GW_PORT:
		gtk_entry_set_text(widget, "443");
		break;
	    case IGNORE_CERT:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
		break;
	    case EXTRA_ARGS:
		gtk_entry_set_text(widget, "/bpp:8 /gdi:hw -themes -wallpaper");
		break;
	    default:
		gtk_entry_set_text(widget, "");
	}
    }
}

/************************************************************************
 ********************     ON_QUIT_BUTTON_CLICKED     ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_quit_button_clicked()
{
    gtk_main_quit();
}

/************************************************************************
 ********************     ON_ADD_BUTTON_CLICKED      ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_add_button_clicked()
{
    GtkWidget		*win;

    clear_display();
    mode = ADD_MODE;
    win = (GtkWidget *)gtk_builder_get_object(glade_xml, "add_window");
    gtk_widget_show(win);
}

/************************************************************************
 ********************     ON_EDIT_BUTTON_CLICKED     ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_edit_button_clicked()
{
    int			i, rownum;
    char		**fields;
    gboolean		is_active;
    GtkWidget		*win, *widget;
    GtkListBox		*box;
    GtkListBoxRow	*row;

    mode = EDIT_MODE;

    box = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    row = gtk_list_box_get_selected_row(box);
    if (row == NULL)
	return;
    rownum = gtk_list_box_row_get_index(row);
    fields = entries[rownum].fields;

    /* Copy data from the entries table into the GUI */
    for (i = 0; i < NUM_FIELDS; ++i)
    {
	widget = GTK_WIDGET(gtk_builder_get_object(glade_xml, widget_names[i]));
	if (i == IGNORE_CERT)
	{
	    is_active = (fields[i][0] == 'Y');
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), is_active);
	}
	else
	    gtk_entry_set_text(GTK_ENTRY(widget), fields[i]);
    }

    win = (GtkWidget *)gtk_builder_get_object(glade_xml, "add_window");
    gtk_widget_show(win);
}

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
 ********************        LAUNCH_XFREERDP         ********************
 ************************************************************************/
void
launch_xfreerdp()
{
    int			i, fd, rownum, fnum;
    pid_t		pid;
    char		**fields, *gw_user, *extra_args, *argp;
    char		*args[MAX_ARGS], *temp, *p, logfile[1024];
    const gchar		*passwd, *gw_passwd;
    GtkListBox		*box;
    GtkListBoxRow	*row;
    GtkEntry		*pw_entry, *gw_entry;

    box = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    row = gtk_list_box_get_selected_row(box);
    if (row == NULL)
	return;
    rownum = gtk_list_box_row_get_index(row);
    fields = entries[rownum].fields;
    extra_args = strdup(fields[EXTRA_ARGS]);
    pw_entry = GTK_ENTRY(gtk_builder_get_object(glade_xml, "host_password"));
    gw_entry = GTK_ENTRY(gtk_builder_get_object(glade_xml, "gw_password"));

    /* Extract the passwords & extra args from the entry fields */
    passwd = gtk_entry_get_text(pw_entry);
    gw_passwd = gtk_entry_get_text(gw_entry);

    if ((temp = fields[DISPLAY_SIZE]) == NULL)
	temp = DEFAULT_SIZE;
    fnum = 0;
    args[fnum++] = gen_vector("xfreerdp");
    args[fnum++] = gen_vector("/size:%s", temp);
    args[fnum++] = gen_vector("/u:%s", fields[USERNAME]);
    args[fnum++] = gen_vector("/p:%s", passwd);
    args[fnum++] = gen_vector("/v:%s:%s", fields[HOST], fields[PORT]);
    args[fnum++] = gen_vector("+auto-reconnect");
    args[fnum++] = gen_vector("/auto-reconnect-max-retries:20");
    if (fields[IGNORE_CERT][0] == 'Y')
	args[fnum++] = gen_vector("/cert-ignore");
    else
	args[fnum++] = gen_vector("/cert-tofu");

    if (fields[GATEWAY][0] != '\0')
    {
	gw_user = fields[GW_USER];
	if (gw_user[0] == '\0')
	    gw_user = fields[USERNAME];
	if (gw_passwd[0] == '\0')
	    gw_passwd = passwd;
	args[fnum++] = gen_vector("/g:%s:%s", fields[GATEWAY], fields[GW_PORT]);
	args[fnum++] = gen_vector("/gu:%s", gw_user);
	args[fnum++] = gen_vector("/gp:%s", gw_passwd);
    }

    /* Add in the "extra args" */
    argp = strtok(extra_args, " ");
    while (argp)
    {
	args[fnum++] = strdup(argp);
	argp = strtok(NULL, " ");
    }
    args[fnum] = NULL;
    free(extra_args);

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

    if ((pid = fork()) == 0)
    {
	for (i = 3; i <= 255; ++i)
	    close(i);
	snprintf(logfile, sizeof(logfile), "%s/xfreerdp.log", config_dir);
	fd = open(logfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	close(1);
	close(2);
	dup(fd);
	dup(fd);
	close(fd);
	close(0);
	execvp("xfreerdp", args);
	/* Shouldn't get here unless exec craps out completely */
	exit(1);
    }

    for (i = 0; args[i] != NULL; ++i)
	free(args[i]);
}

/************************************************************************
 ********************    ON_CLEAR_BUTTON_CLICKED     ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_clear_button_clicked()
{
    clear_display();
}

/************************************************************************
 ********************    ON_CANCEL_BUTTON_CLICKED    ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_cancel_button_clicked()
{
    GtkWidget		*win;

    win = (GtkWidget *)gtk_builder_get_object(glade_xml, "add_window");
    gtk_widget_hide(win);
}

/************************************************************************
 ********************          CHECK_ENTRY           ********************
 ************************************************************************/
int
check_entry(entry_t *entry)
{
    if (entry->fields[ENTRY_NAME][0] == '\0')
    {
	alert("Entry Name must not be blank");
	return(-1);
    }
    if (entry->fields[USERNAME][0] == '\0')
    {
	alert("User name must not be blank");
	return(-1);
    }
    if (entry->fields[HOST][0] == '\0')
    {
	alert("Host name must not be blank");
	return(-1);
    }
    return(0);
}

/************************************************************************
 ********************          COPY_FROM_GUi         ********************
 ************************************************************************/
void
copy_from_gui(entry_t *entry)
{
    int			i;
    gboolean		is_active;
    GtkWidget		*widget;
    char		*plain;
    const gchar		*textp;

    for (i = 0; i < NUM_FIELDS; ++i)
    {
	widget = GTK_WIDGET(gtk_builder_get_object(glade_xml, widget_names[i]));
	if (i == IGNORE_CERT)
	{
	    is_active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	    textp = is_active ? "Y" : "N";
	}
	else
	    textp = gtk_entry_get_text(GTK_ENTRY(widget));
	plain = strdup(textp);
	alltrim(plain);
	textp = plain;
	entry->fields[i] = (char *)textp;
    }
}

/************************************************************************
 ********************           ADD_ENTRY            ********************
 ************************************************************************/
int
add_entry()
{
    int			i;
    entry_t		*entry;

    if (num_entries+1 >= MAX_ENTRIES)
    {
	alert("Maximum number of entries exceeded");
	return(-1);
    }

    entry = &entries[num_entries++];
    copy_from_gui(entry);

    if (check_entry(entry) < 0)
    {
	for (i = 0; i < NUM_FIELDS; ++i)
	    free(entry->fields[i]);
	--num_entries;
	return(-1);
    }

    add_row(entry->fields[ENTRY_NAME]);

    return(0);
}

/************************************************************************
 ********************          UPDATE_ENTRY          ********************
 ************************************************************************/
int
update_entry()
{
    int			i, rownum;
    char		**fields;
    entry_t		*entry;
    GtkListBox		*box;
    GtkListBoxRow	*row;
    GList		*child;
    GtkLabel		*label;

    box = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    row = gtk_list_box_get_selected_row(box);
    rownum = gtk_list_box_row_get_index(row);

    entry = &entries[rownum];
    fields = entries[rownum].fields;

    for (i = 0; i < NUM_FIELDS; ++i)
	free(fields[i]);
    copy_from_gui(entry);

    /*
     * Update the label for this entry in the main window.
     * I promise that each row only has one child!
     */
    child = gtk_container_get_children(GTK_CONTAINER(row));
    label = GTK_LABEL(child->data);
    gtk_label_set_text(label, fields[ENTRY_NAME]);

    if (check_entry(entry) < 0)
	return(-1);

    return(0);
}

/************************************************************************
 ********************      ON_OK_BUTTON_CLICKED      ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_ok_button_clicked()
{
    GtkWidget	*win;
    int		status;

    status = 0;
    if (mode == ADD_MODE)
	status = add_entry();
    else
	status = update_entry();
    if (status == 0)
    {
	if (save_entries(entries_file, entries, num_entries) < 0)
	{
	    alert("error saving to %s: %s", entries_file, strerror(errno));
	    return;
	}
	win = (GtkWidget *)gtk_builder_get_object(glade_xml, "add_window");
	gtk_widget_hide(win);
    }
}

/************************************************************************
 ********************    ON_DELETE_BUTTON_CLICKED    ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_delete_button_clicked()
{
    int			i, j, rownum;
    char		**fields;
    GtkListBox		*box;
    GtkListBoxRow	*row;

    box = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    row = gtk_list_box_get_selected_row(box);
    if (row == NULL)
	return;
    rownum = gtk_list_box_row_get_index(row);

    /* Avoid a memory leak and free the strings in the entry */
    fields = entries[rownum].fields;
    for (j = 0; j < NUM_FIELDS; ++j)
	free(fields[j]);

    for (i = rownum; i < num_entries-1; ++i)
	entries[i] = entries[i+1];
    --num_entries;

    gtk_container_remove(GTK_CONTAINER(box), GTK_WIDGET(row));

    if (save_entries(entries_file, entries, num_entries) < 0)
    {
	alert("error saving to %s: %s", entries_file, strerror(errno));
	return;
    }
}

/************************************************************************
 ********************    ON_ABOUT_BUTTON_CLICKED     ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_about_button_clicked()
{
    alert("RDP Session Manager version %s", VERSION);
}

/************************************************************************
 ********************   ON_LAUNCH_BUTTON_CLICKED     ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_launch_button_clicked()
{
    GtkWidget		*win, *passwd, *gw_label, *gw_passwd;
    GtkListBoxRow	*row;
    GtkListBox		*box;
    int			rownum;
    char		**fields;

    win = GTK_WIDGET(gtk_builder_get_object(glade_xml, "auth_window"));
    passwd = GTK_WIDGET(gtk_builder_get_object(glade_xml, "host_password"));
    gw_label = GTK_WIDGET(gtk_builder_get_object(glade_xml, "gw_passwd_label"));
    gw_passwd = GTK_WIDGET(gtk_builder_get_object(glade_xml, "gw_password"));
    box = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    row = gtk_list_box_get_selected_row(box);
    if (row == NULL)
	return;
    rownum = gtk_list_box_row_get_index(row);
    fields = entries[rownum].fields;
    gtk_entry_set_text(GTK_ENTRY(passwd), "");
    gtk_entry_set_text(GTK_ENTRY(gw_passwd), "");

    if (fields[GATEWAY][0] == '\0')
    {
	gtk_widget_hide(gw_label);
	gtk_widget_hide(gw_passwd);
    }
    else
    {
	gtk_widget_show(gw_label);
	gtk_widget_show(gw_passwd);
    }

    gtk_widget_show(win);
}

/************************************************************************
 ********************    ON_LISTBOX_ROW_ACTIVATED    ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_listbox_row_activated()
{
    mylog("Inside on_listbox_row_activated\n");
    on_launch_button_clicked();
}

/************************************************************************
 ********************   ON_AUTH_OK_BUTTON_CLICKED    ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_auth_ok_button_clicked()
{
    GtkWindow	*win;

    win = GTK_WINDOW(gtk_builder_get_object(glade_xml, "auth_window"));
    gtk_widget_hide(GTK_WIDGET(win));
    launch_xfreerdp();
}

/************************************************************************
 ******************** ON_AUTH_CANCEL_BUTTON_CLICKED  ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_auth_cancel_clicked()
{
    GtkWindow	*win;

    win = GTK_WINDOW(gtk_builder_get_object(glade_xml, "auth_window"));
    gtk_widget_hide(GTK_WIDGET(win));
}
