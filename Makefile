TARGET ?= zbitxd
OWNER ?= $(TARGET)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
STATEDIR ?= /var/lib/$(OWNER)
SHAREDIR ?= $(PREFIX)/share/$(OWNER)
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
FFTOBJ = ft8_lib/.build/fft/kiss_fft.o ft8_lib/.build/fft/kiss_fftr.o
HEADERS = $(wildcard *.h)
CFLAGS = -I.
LIBS = -lwiringPi -lasound -lm -lfftw3 -lfftw3f -pthread -lncurses -lsqlite3 -lsystemd ft8_lib/libft8.a
ifdef SBITX_UNUSED
## remove and print unused code
CFLAGS += -ffunction-sections -fdata-sections
LIBS += -Wl,--gc-sections,--print-gc-sections
endif
ifdef SBITX_DEBUG
CFLAGS += -ggdb3 -fsanitize=address
LIBS += -fsanitize=address
endif
CC = gcc
LINK = gcc
STRIP = strip

$(TARGET): create_configure.h $(OBJECTS) ft8_lib/libft8.a
	$(LINK) $(LFLAGS) -o $(TARGET) $(OBJECTS) $(FFTOBJ) $(LIBPATH) $(LIBS)

.c.o: $(HEADERS)
	$(CC) -c $(CFLAGS) $(DEBUGFLAGS) $(INCPATH) -o $@ $<

create_configure.h:
	$(shell echo "#define STATEDIR \"$(STATEDIR)\"" > configure.h) 
	$(shell echo "#define SHAREDIR \"$(SHAREDIR)\"" >> configure.h) 

ft8_lib/libft8.a:
ifdef SBITX_DEBUG
	$(MAKE) FT8_DEBUG=1 -C ft8_lib
else
	$(MAKE) -C ft8_lib
endif

clean:
	-rm -f configure.h
	-rm -f $(OBJECTS)
	-rm -f *~ core *.core
	-rm -f $(TARGET)
	$(MAKE) -C ft8_lib clean

adduser:
	-adduser --system --group --home $(DESTDIR)/$(STATEDIR) --disabled-password $(OWNER)
	-adduser $(OWNER) audio
	-adduser $(OWNER) gpio

install: adduser
	install -D --mode=755 $(TARGET) $(DESTDIR)/$(BINDIR)/$(TARGET)
	install -d --owner=$(OWNER) --group=$(OWNER) $(DESTDIR)/$(SHAREDIR)/web
	install -m 644 --owner=$(OWNER) --group=$(OWNER) web/* $(DESTDIR)/$(SHAREDIR)/web
	install -d --owner=$(OWNER) --group=$(OWNER) $(DESTDIR)/$(STATEDIR)
	install -m 644 --owner=$(OWNER) --group=$(OWNER) data/default_hw_settings.ini $(DESTDIR)/$(STATEDIR)
	install -m 644 --owner=$(OWNER) --group=$(OWNER) data/default_settings.ini $(DESTDIR)/$(STATEDIR)
	install -d $(DESTDIR)/$(PREFIX)/lib/systemd/system/
	install -m 644 systemd/zbitxd.service $(DESTDIR)/$(PREFIX)/lib/systemd/system
	ln -sf /var/lib/zbitxd/grids.txt /usr/local/share/zbitxd/web/grids.txt
ifeq ("$(wildcard $(DESTDIR)/$(STATEDIR)/sbitx.db)","")
	$(shell sqlite3 $(DESTDIR)/$(STATEDIR)/sbitx.db < data/create_db.sql)
endif

uninstall:
	rm -f $(DESTDIR)/$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)/$(SHAREDIR)
	rm -f $(DESTDIR)/$(STATEDIR)/default_hw_settings.ini
	rm -f $(DESTDIR)/$(STATEDIR)/default_settings.ini
	rm -f $(DESTDIR)/$(PREFIX)/lib/systemd/system/zbitxd.service

.PHONY: adduser create_configure.h clean install uninstall
