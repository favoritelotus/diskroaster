CC ?= cc
CFLAGS ?= -O2 -Wall
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

TARGET = diskroaster
SRC = diskroaster.c
LIBS = -lpthread
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)
	sh ./man/install_man_page.sh

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	sh ./man/uninstall_man_page.sh

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean

