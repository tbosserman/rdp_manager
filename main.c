#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "rdp_xml.h"

#define TESTMODE	0

extern void		my_gtk_init();

GtkBuilder		*glade_xml;

int
main(int argc, char *argv[])
{
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
