#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "rdp_xml.h"
#include "version.h"

#define TESTMODE	0

extern void		my_gtk_init();

char			*progname;
GtkBuilder		*glade_xml;

void
usage()
{
    fprintf(stderr, "usage: %s [-h] [-v]\n", progname);
    exit(1);
}

void version_info()
{
    fprintf(stderr, "RDP Session Manager version %.2g\n", VERSION);
    exit(1);
}

int
main(int argc, char *argv[])
{
    int		i, ch;

    progname = argv[0];
    for (i = strlen(progname) - 1; i >= 0 && progname[i] != '/'; --i);
    progname += (i + 1);

    while ((ch = getopt(argc, argv, "vh")) != -1)
    {
	switch(ch)
	{
	    case 'h':
		usage();
	    case 'v':
		version_info();
	    default:
		usage();
	}
    }
    gtk_init(&argc, &argv);
#if TESTMODE == 1
    glade_xml = gtk_builder_new_from_file("rdp_manager.glade");
#else
    glade_xml = gtk_builder_new_from_string(glade_data, strlen(glade_data));
#endif
    gtk_builder_connect_signals(glade_xml, NULL);
    my_gtk_init();
    gtk_main();
}
