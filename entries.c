#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "rdp_manager.h"

extern void mylog(char *fmt, ...);

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
    (void)chmod(entries_file, 0600);

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
 ********************          LOAD_ENTRIES          ********************
 ************************************************************************/
int
load_entries(char *entries_file, entry_t *entries)
{
    FILE	*fp;
    int		i, num_entries;
    char	line[1024], *key, *value, *entname;
    entry_t	*entry;

    if ((fp = fopen(entries_file, "r")) == NULL)
	return(0);

    num_entries = 0;
    entry = &entries[0];
    entname = NULL;
    while (fgets(line, sizeof(line), fp) != NULL)
    {
	line[strlen(line)-1] = '\0';
	alltrim(line);
	if (line[0] == '#' || line[0] == '\0')
	    continue;
	if (line[0] == '[')
	{
	    entry = &entries[num_entries++];
	    entname = strtok(line+1, "]");
	    alltrim(entname);
	    entry->fields[ENTRY_NAME] = strdup(entname);
	    continue;
	}

	key = strtok(line, ": ");
	value = strtok(NULL, "");
	if (value)
	    alltrim(value);
	else
	    value = "";
	mylog("key='%s'  value='%s'\n", key, value);

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
