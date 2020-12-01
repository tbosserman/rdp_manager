CFLAGS = -Wall -O -g $(shell pkg-config --cflags gtk+-3.0)
#CFLAGS != pkg-config --cflags gtk+-3.0
LDLIBS != pkg-config --libs gtk+-3.0
LDFLAGS=-Wl,--export-dynamic

all: rdp_manager

rdp_manager: main.o callbacks.o entries.o
	$(CC) -o rdp_manager $^ $(LDFLAGS) $(LDLIBS)

main.o: main.c rdp_xml.h version.h

callbacks.o: callbacks.c rdp_manager.h version.h

entries.o: entries.c rdp_manager.h

rdp_xml.h: rdp_manager.glade
	./gen_hdr.sh

clean:
	$(RM) *.o rdp_manager rdp_xml.h
