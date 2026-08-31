CC ?= cc
CFLAGS += -Wall -Wextra -pthread
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man8

TARGET = diskroaster
SRC = utils.c disk.c workers.c main.c
LIBS = -lpthread

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)

install: $(TARGET)
	@echo "Installing to $(PREFIX)"
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)

	@os=$$(uname -s); \
	case $$os in \
		Linux) \
			MAN_SRC="./man/diskroaster.8.linux"; \
			;; \
		FreeBSD) \
			MAN_SRC="./man/diskroaster.8.freebsd"; \
			;; \
		*) \
			echo "Error: Unsupported OS ($$os), aborting..."; \
			exit 1; \
			;; \
	esac; \
	if [ -f "$$MAN_SRC" ]; then \
		echo "Installing man page ..."; \
		install -m 0644 "$$MAN_SRC" $(DESTDIR)$(MANDIR)/diskroaster.8; \
		gzip -f $(DESTDIR)$(MANDIR)/diskroaster.8; \
		if [ "$$os" = "Linux" ]; then \
			mandb --quiet || true; \
		fi; \
	fi
	@echo "Installation complete"

uninstall:
	@echo "Removing from $(PREFIX)"
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(MANDIR)/diskroaster.8.gz
	@os=$$(uname -s); \
	if [ "$$os" = "Linux" ]; then \
		mandb --quiet || true; \
	fi
	@echo "Uninstallation complete"

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
