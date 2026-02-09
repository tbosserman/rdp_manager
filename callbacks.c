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

extern gboolean netmon(gpointer user_data);
extern gboolean check_reboot(gpointer user_data);
extern gboolean test_connect(char *host, int port);
extern int resolve_hostname(char *);
extern void launch();

static aes256_key_t	crypto_key;
static char		entries_file[1024];
static char		key_file[1024];

FILE			*logfp;
char			config_dir[512];
char			logfile[1024];
entry_t			entries[MAX_ENTRIES];
int			num_entries;
int			mode;
options_t		global_options;
pid_t			xfreerdp_pid;

char			*widget_names[] = {
    "entry_name",	"host",		"port",		"domain",
    "username",		"display_size",	"multi_monitor","gw_host",
    "gw_port",		"gw_username"
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
    va_list		ap;
    char		temp[1024];
    GtkMessageDialog	*dialog;

    temp[sizeof(temp)-1] = '\0';
    va_start(ap, fmt);
    vsnprintf(temp, sizeof(temp)-1, fmt, ap);
    va_end(ap);
    dialog = (GtkMessageDialog *)gtk_builder_get_object(glade_xml, "alert_window");
    gtk_message_dialog_format_secondary_text(dialog, "%s", temp);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_hide(GTK_WIDGET(dialog));
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
handler(int sig, siginfo_t *siginfo, void *ucontext)
{
    int		status;
    pid_t	pid;

    if (sig == SIGCHLD && siginfo->si_pid != xfreerdp_pid)
	return;

    /*
     * We're not doing much here, just reaping the child process so it
     * doesn't turn into a zombie process.
     */
    if ((pid = wait(&status)) < 0)
    {
	if (errno != ECHILD)
	    mylog("In signal handler wait() returned %d: %s\n", pid,
		strerror(errno));
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
    GtkComboBox		*combobox;
    GtkEntry		*entry;
    struct sigaction	newact, oldact;

    memset(&newact, 0, sizeof(newact));
    newact.sa_flags = SA_SIGINFO | SA_RESTART;
    newact.sa_sigaction = handler;
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

    if (crypto_init(home) < 0)
    {
	alert("unable to initialize cryptography system");
	exit(1);
    }
    snprintf(key_file, sizeof(key_file), "%s/.keyfile", config_dir);
    if (access(key_file, F_OK) < 0)
    {
	if (gen_aeskey(&crypto_key) < 0)
	{
	    alert("Unable to generate crypto key");
	    exit(1);
	}
	if (store_key(key_file, &crypto_key) < 0)
	{
	    alert("error writing to %s: %s", key_file, strerror(errno));
	    exit(1);
	}
    }
    else
    {
	if (load_key(key_file, &crypto_key) < 0)
	{
	    alert("error reading %s", key_file, strerror(errno));
	    exit(1);
	}
    }

    mylog("Loading entries from %s\n", entries_file);
    // Initialize global options in case they aren't specified in config file.
    global_options.access_mode = 0;
    global_options.freerdp_version = AUTO_DETECT;
    global_options.freerdp_path = NULL;

    num_entries = load_entries(entries_file, entries);
    if (global_options.freerdp_path == NULL)
	global_options.freerdp_path = strdup("");
    for (i = 0; i < num_entries; ++i)
	add_row(entries[i].fields[ENTRY_NAME]);

    // Set the GUI objects to show what was specified in the config file.
    combobox = GTK_COMBO_BOX(gtk_builder_get_object(glade_xml,
               "access_mode_menu"));
    gtk_combo_box_set_active(combobox, global_options.access_mode);
    combobox = GTK_COMBO_BOX(gtk_builder_get_object(glade_xml,
	       "freerdp_version"));
    gtk_combo_box_set_active(combobox, global_options.freerdp_version);
    entry = GTK_ENTRY(gtk_builder_get_object(glade_xml, "freerdp_path"));
    gtk_entry_set_text(entry, global_options.freerdp_path);
    window1 = (GtkWidget *)gtk_builder_get_object(glade_xml, "window1");
    gtk_widget_show_all(window1);
    /* Call netmon once by to notify user immediately if no internet */
    (void)netmon(NULL);

    g_timeout_add_seconds(15, netmon, (gpointer)&global_options);
    g_timeout_add_seconds(300, check_reboot, NULL);
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
	    case MULTI_MONITOR:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
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
    char		**fields, temp[16];
    GtkWidget		*win;
    GtkListBox		*box;
    GtkListBoxRow	*row;
    GObject		*widget;

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
	widget = gtk_builder_get_object(glade_xml, widget_names[i]);
	if (i == MULTI_MONITOR)
	{
	    strcpy(temp, "0");
	    if (fields[i])
		strcpy(temp, fields[i]);
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), atoi(temp));
	}
	else
	    gtk_entry_set_text(GTK_ENTRY(widget), fields[i]);
    }

    win = (GtkWidget *)gtk_builder_get_object(glade_xml, "add_window");
    gtk_widget_show(win);
}

/************************************************************************
 ********************      ON_PASSWD_OK_CLICKED      ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_passwd_ok_clicked()
{
    GtkWidget		*pwdwin;

    pwdwin = (GtkWidget *)gtk_builder_get_object(glade_xml, "passwd_window");
    gtk_widget_hide(pwdwin);
    launch();
}

/************************************************************************
 ********************    ON_PASSWD_CANCEL_CLICKED    ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_passwd_cancel_clicked()
{
    GtkWidget		*pwdwin;

    pwdwin = (GtkWidget *)gtk_builder_get_object(glade_xml, "passwd_window");
    gtk_widget_hide(pwdwin);
}

/************************************************************************
 ********************   ON_LAUNCH_BUTTON_CLICKED     ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_launch_button_clicked()
{
    int			rownum;
    char		**fields, *gateway, *user, *gw_user, temp[1024];
    GtkWidget		*pwdwin, *passwd_text, *gw_passwd, *gw_passwd_text;
    GtkEntry		*pwd_entry, *gw_pwd_entry;
    GtkEntryBuffer	*buffer, *gw_buffer;
    GtkListBox		*box;
    GtkListBoxRow	*row;

    box = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    row = gtk_list_box_get_selected_row(box);
    if (row == NULL)
	return;
    rownum = gtk_list_box_row_get_index(row);
    fields = entries[rownum].fields;

    pwdwin = (GtkWidget *)gtk_builder_get_object(glade_xml, "passwd_window");
    pwd_entry = (GtkEntry *)gtk_builder_get_object(glade_xml, "passwd");
    gw_pwd_entry = (GtkEntry *)gtk_builder_get_object(glade_xml, "gw_passwd");
    buffer = gtk_entry_get_buffer(pwd_entry);
    gw_buffer = gtk_entry_get_buffer(gw_pwd_entry);
    passwd_text = (GtkWidget *)gtk_builder_get_object(glade_xml, "passwd_text");
    gw_passwd_text = (GtkWidget *)gtk_builder_get_object(glade_xml, "gw_passwd_text");
    gw_passwd = (GtkWidget *)gtk_builder_get_object(glade_xml, "gw_passwd");

    gateway = fields[GATEWAY];
    user = fields[USERNAME];
    gw_user = fields[GW_USER];
    snprintf(temp, sizeof(temp), "Enter password for %s:  ", user);
    gtk_label_set_text((GtkLabel *)passwd_text, temp);

    // See if gw_username is set or is different from username.
    // Hide the gw_password prompt fields if same or not set.
    if (gateway[0] == '\0' || gw_user[0] == '\0' || strcmp(user, gw_user) == 0)
    {
	gtk_widget_hide(gw_passwd_text);
	gtk_widget_hide(gw_passwd);
    }
    else
    {
	snprintf(temp, sizeof(temp), "Enter password for %s:  ", gw_user);
	gtk_label_set_text((GtkLabel *)gw_passwd_text, temp);
	gtk_widget_show(gw_passwd_text);
	gtk_widget_show(gw_passwd);
    }

    gtk_widget_show(pwdwin);
    gtk_entry_buffer_delete_text(buffer, 0, -1);
    gtk_entry_buffer_delete_text(gw_buffer, 0, -1);
    gtk_entry_grab_focus_without_selecting(pwd_entry);
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
check_entry(char *fields[])
{
    int		errcount;
    char	temp[256], msg[2048], *host, *gw_host;

    errcount = 0;
    strcpy(msg, "Please fix the following errors:\n");
    host = fields[HOST];
    gw_host = fields[GATEWAY];

    if (fields[ENTRY_NAME][0] == '\0')
    {
	strcat(msg, "\n· Entry Name must not be blank");
	errcount++;
    }
    if (fields[USERNAME][0] == '\0')
    {
	strcat(msg, "\n· User name must not be blank");
	errcount++;
    }
    // 2025/07/23: per request from ssaunders@, allow blank domain.
#ifdef REQUIRE_DOMAIN
    if (fields[DOMAIN][0] == '\0')
    {
	strcat(msg, "\n· Domain name must not be blank");
	errcount++;
    }
#endif
    if (host[0] == '\0')
    {
	strcat(msg, "\n· Host name must not be blank");
	errcount++;
    }
    if (fields[PORT][0] == '\0')
    {
	strcat(msg, "\n· Port number must not be blank");
	errcount++;
    }

    // Do a little hostname verification via DNS
    if (gw_host[0] != '\0')
    {
	if (fields[GW_PORT][0] == '\0')
	{
	    strcat(msg, "\n· Gateway port number must not be blank");
	    errcount++;
	}
	if (resolve_hostname(gw_host) != 0)
	{
	    snprintf(temp, sizeof(temp),
		"\n· Unable to resolve host %s", gw_host);
	    strcat(msg, temp);
	    errcount++;
	}
    }
    else
    {
	if (resolve_hostname(host) != 0)
	{
	    snprintf(temp, sizeof(temp),
		"\n· Unable to resolve host %s", host);
	    strcat(msg, temp);
	    errcount++;
	}
    }

    if (errcount > 0)
	alert(msg);

    return(errcount);
}

/************************************************************************
 ********************           ADD_ENTRY            ********************
 ************************************************************************/
int
add_entry()
{
    int			i, multimon;
    char		*textp, temp[16];
    entry_t		*entry;
    GObject		*widget;

    if (num_entries+1 >= MAX_ENTRIES)
    {
	alert("Maximum number of entries exceeded");
	return(-1);
    }

    entry = &entries[num_entries++];
    for (i = 0; i < NUM_FIELDS; ++i)
    {
	widget = gtk_builder_get_object(glade_xml, widget_names[i]);
	if (i == MULTI_MONITOR)
	{
	    multimon = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	    sprintf(temp, "%d", multimon);
	    entry->fields[i] = strdup(temp);
	}
	else
	{
	    textp = strdup((char *)gtk_entry_get_text(GTK_ENTRY(widget)));
	    alltrim(textp);
	    entry->fields[i] = textp;
	}
    }

    if (check_entry(entry->fields) != 0)
    {
	--num_entries;
	for (i = 0; i < NUM_FIELDS; ++i)
	    free(entry->fields[i]);
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
    gboolean		multimon;
    char		**fieldp, *fields[NUM_FIELDS], temp[16];
    const gchar		*textp;
    GtkListBox		*box;
    GtkListBoxRow	*row;
    GObject		*widget;
    GList		*child;
    GtkLabel		*label;

    box = GTK_LIST_BOX(gtk_builder_get_object(glade_xml, "listbox"));
    row = gtk_list_box_get_selected_row(box);
    rownum = gtk_list_box_row_get_index(row);
    memset(fields, 0, sizeof(fields));

    for (i = 0; i < NUM_FIELDS; ++i)
    {
	widget = gtk_builder_get_object(glade_xml, widget_names[i]);
	if (i == MULTI_MONITOR)
	{
	    multimon = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	    sprintf(temp, "%d", multimon);
	    fields[i] = strdup(temp);
	}
	else
	{
	    textp = gtk_entry_get_text(GTK_ENTRY(widget));
	    fields[i] = strdup(textp);
	}
	alltrim(fields[i]);
    }

    if (check_entry(fields) != 0)
    {
	for (i = 0; i < NUM_FIELDS; ++i)
	    if (fields[i])
		free(fields[i]);
	return(-1);
    }

    for (i = 0; i < NUM_FIELDS; ++i)
    {
	fieldp = &entries[rownum].fields[i];
	if (*fieldp)
	    free(*fieldp);
	*fieldp = fields[i];
    }

     // Update the label for this entry in the main window.
     // I promise that each row only has one child!
    child = gtk_container_get_children(GTK_CONTAINER(row));
    label = GTK_LABEL(child->data);
    gtk_label_set_text(label, fields[ENTRY_NAME]);

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
 ********************   ON_OPTIONS_BUTTON_CLICKED    ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_options_button_clicked()
{
    GtkWidget		*win, *entry;

    win = (GtkWidget *)gtk_builder_get_object(glade_xml, "options_window");
    gtk_widget_show(win);
    entry = (GtkWidget *)gtk_builder_get_object(glade_xml, "freerdp_path");
    //gtk_widget_grab_focus(win);
    gtk_widget_grab_focus(entry);
}

/************************************************************************
 ********************     ON_OPTIONS_OK_CLICKED      ********************
 ************************************************************************/
int
on_options_ok_clicked(GtkButton *button, gpointer user_data)
{
    GtkComboBox		*combobox;
    GtkEntry		*entry;
    char		*new_path;
    int			value;

    entry = (GtkEntry *)gtk_builder_get_object(glade_xml, "freerdp_path");
    new_path = strdup((char *)gtk_entry_get_text(entry));
    alltrim(new_path);
    if (new_path[0] != '\0' && access(new_path, X_OK) < 0)
    {
	alert("%s: %s", new_path, strerror(errno));
	free(new_path);
	return(-1);
    }
    free(global_options.freerdp_path);
    global_options.freerdp_path = new_path;

    combobox = (GtkComboBox *)gtk_builder_get_object(glade_xml, "access_mode_menu");
    value = gtk_combo_box_get_active(combobox);
    global_options.access_mode = value;

    combobox = (GtkComboBox *)gtk_builder_get_object(glade_xml, "freerdp_version");
    value = gtk_combo_box_get_active(combobox);
    global_options.freerdp_version = value;

    return(0);
}

/************************************************************************
 ********************        OPTIONS_CLICKED         ********************
 ************************************************************************/
G_MODULE_EXPORT void
options_clicked(GtkButton *button, gpointer user_data)
{
    GtkWidget		*win;
    GtkComboBox		*combobox, *version;
    GtkButton		*reset, *cancel;
    GtkEntry		*entry;

    reset = GTK_BUTTON(gtk_builder_get_object(glade_xml, "options_reset"));
    cancel = GTK_BUTTON(gtk_builder_get_object(glade_xml, "options_cancel"));
    win = (GtkWidget *)gtk_builder_get_object(glade_xml, "options_window");
    combobox = (GtkComboBox *)gtk_builder_get_object(glade_xml, "access_mode_menu");
    version = (GtkComboBox *)gtk_builder_get_object(glade_xml, "freerdp_version");
    entry = (GtkEntry *)gtk_builder_get_object(glade_xml, "freerdp_path");

    if (button == reset)
    {
	gtk_combo_box_set_active(combobox, 0);
	gtk_combo_box_set_active(version, 0);
	gtk_entry_set_text(entry, "");
	return;
    }

    if (button == cancel)
    {
	gtk_combo_box_set_active(combobox, global_options.access_mode);
	gtk_combo_box_set_active(version, global_options.freerdp_version);
	gtk_entry_set_text(entry, global_options.freerdp_path);
	gtk_widget_hide(win);
	return;
    }

    if (on_options_ok_clicked(button, user_data) < 0)
	return;
    if (save_entries(entries_file, entries, num_entries) < 0)
	alert("error saving to %s: %s", entries_file, strerror(errno));
    gtk_widget_hide(win);
}

/************************************************************************
 ********************    ON_LISTBOX_ROW_ACTIVATED    ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_listbox_row_activated()
{
    on_launch_button_clicked();
}

/************************************************************************
 ********************      ON_PASSWD_ICON_PRESS      ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_passwd_icon_press(GtkEntry *entry, GtkEntryIconPosition icon_pos,
    GdkEvent *event, gpointer data)
{
    gtk_entry_set_visibility(entry, TRUE);
}

/************************************************************************
 ********************     ON_PASSWD_ICON_RELEASE     ********************
 ************************************************************************/
G_MODULE_EXPORT void
on_passwd_icon_release(GtkEntry *entry, GtkEntryIconPosition icon_pos,
    GdkEvent *event, gpointer data)
{
    gtk_entry_set_visibility(entry, FALSE);
}
