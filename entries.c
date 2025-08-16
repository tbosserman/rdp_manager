#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include "rdp_manager.h"

extern void mylog(char *fmt, ...);

extern GtkBuilder	*glade_xml;
extern options_t	global_options;

#define ACCESS_MODE_KEY	0
static char *global_keys[] = {
    "access_mode",	// REMOTE or LOCAL
    NULL
};
static char *mode_values[] = {
    "LOCAL",
    "REMOTE"
};

/************************************************************************
 ********************          SAVE_ENTRIES          ********************
 ************************************************************************/
int
save_entries(char *entries_file, entry_t *entries, int num_entries)
{
    int		i, j;
    FILE	*fp;
    entry_t	*entry;

    if ((fp = fopen(entries_file, "w")) == NULL)
	return(-1);
    (void)fchmod(fileno(fp), 0600);

    // Save the global config values
    fprintf(fp, "[GLOBAL]\n");
    fprintf(fp, "access_mode: %s\n\n", mode_values[global_options.access_mode]);

    for (i = 0; i < num_entries; ++i)
    {
	entry = &entries[i];
	fprintf(fp, "[%s]\n", entry->fields[ENTRY_NAME]);
	for (j = 1; j < NUM_FIELDS; ++j)
	    fprintf(fp, "%s: %s\n", widget_names[j], entry->fields[j]);
	fputc('\n', fp);
    }
    fclose(fp);

    return(0);
}

/************************************************************************
 ********************         GLOBAL_OPTIONS         ********************
 ************************************************************************/
void
parse_global_options(char *key, char *value)
{
    int		i;
    char	*global_key;
    GtkComboBox	*combobox;

    mylog("DEBUG: global key: '%s', value='%s'\n", key, value);
    for (i = 0; (global_key = global_keys[i]) != NULL; ++i)
    {
	if (strcmp(key, global_key) == 0)
	    break;
    }

    mylog("DEBUG: in global_options, i=%d\n", i);
    switch(i)
    {
	case ACCESS_MODE_KEY:
	    if (strcmp(value, "LOCAL") == 0)
		global_options.access_mode = LOCAL;
	    else if (strcmp(value, "REMOTE") == 0)
		global_options.access_mode = REMOTE;
	    else
		mylog("Unrecognized mode in global options: %s\n", value);
	    combobox = GTK_COMBO_BOX(gtk_builder_get_object(glade_xml,
		"access_mode_menu"));
	    gtk_combo_box_set_active(combobox, global_options.access_mode);

	default:
	    mylog("Ignoring global config key '%s'\n", key);
	    return;
    }
}

/************************************************************************
 ********************          LOAD_ENTRIES          ********************
 ************************************************************************/
int
load_entries(char *entries_file, entry_t *entries)
{
    FILE	*fp;
    int		i, num_entries, global;
    char	line[1024], *key, *value, *entname;
    entry_t	*entry;

    if ((fp = fopen(entries_file, "r")) == NULL)
	return(0);

    num_entries = 0;
    entry = &entries[0];
    entname = NULL;
    global = FALSE;
    while (fgets(line, sizeof(line), fp) != NULL)
    {
	line[strlen(line)-1] = '\0';
	alltrim(line);
	if (line[0] == '#' || line[0] == '\0')
	    continue;
	if (line[0] == '[')
	{
	    entname = strtok(line+1, "]");
	    alltrim(entname);
	    // KLUDGE WARNING: hacking some global options into the entries file
	    if (strcmp(entname, "GLOBAL") == 0)
		global = TRUE;
	    else
	    {
		global = FALSE;
		entry = &entries[num_entries++];
		entry->fields[ENTRY_NAME] = strdup(entname);
		entry->fields[MULTI_MONITOR] = strdup("0");
	    }
	    continue;
	}

	key = strtok(line, ": ");
	value = strtok(NULL, "");
	if (value)
	    alltrim(value);
	else
	    value = "";

	// Another KLUDGE WARNING. I'm shoehorning global options into the
	// entries.dat file.
	if (global)
	{
	    parse_global_options(key, value);
	    continue;
	}

	for (i = 0; i < NUM_FIELDS; ++i)
	    if (strcmp(key, widget_names[i]) == 0)
		break;
	if (i >= NUM_FIELDS)
	{
	    mylog("Ignoring key '%s'\n", key);
	    continue;
	}
	entry->fields[i] = strdup(value);
    }

    fclose(fp);
    return(num_entries);
}
