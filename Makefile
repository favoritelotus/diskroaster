CC ?= cc
CFLAGS ?= -O2 -Wall
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

TARGET = testdisk
SRC = testdisk.c
LIBS = -lpthread
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)
	@echo "Installed $(TARGET) to $(DESTDIR)$(BINDIR)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean

