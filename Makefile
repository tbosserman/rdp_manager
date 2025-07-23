CFLAGS = -Wall -O -g $(shell pkg-config --cflags gtk+-3.0)
#CFLAGS != pkg-config --cflags gtk+-3.0
LDLIBS != pkg-config --libs gtk+-3.0
LDLIBS += -lcrypto
LDFLAGS=-Wl,--export-dynamic

XARCH=$(shell arch)
ifeq ($(XARCH),x86_64)
    ARCH=amd64
else ifeq ($(XARCH),aarch64)
    ARCH=arm64
endif

TARGET=rdp_manager.$(ARCH)
all: $(TARGET) noip2.$(ARCH)

$(TARGET): main.o callbacks.o entries.o crypto.o netmon.o ping_dns.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LDLIBS)

main.o: main.c rdp_xml.h version.h

callbacks.o: callbacks.c rdp_manager.h crypto.h version.h

entries.o: entries.c rdp_manager.h

crypto.o: crypto.c crypto.h

netmon.o: netmon.c ping_dns.h

ping_dns.o: ping_dns.c ping_dns.h

rdp_xml.h: rdp_manager.glade
	./gen_hdr.sh

noip2.$(ARCH): noip2.o
	$(CC) -o $@ $^

noip2.o: noip2.c


clean:
	$(RM) *.o $(TARGET) noip2.$(ARCH)

dpkg:
	./dpkg.sh
