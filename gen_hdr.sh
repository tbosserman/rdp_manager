#!/bin/sh

echo 'static char *glade_data =' > rdp_xml.h
sed 's/\\/\\\\/g;s/"/\\"/g;s/^.*$/    "&\\n"/' \
    rdp_manager.glade >> rdp_xml.h
echo '    ;' >> rdp_xml.h
