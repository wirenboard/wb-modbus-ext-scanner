PREFIX=/usr

VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')
BIN_NAME=wb-modbus-scanner

ifeq ($(DEB_BUILD_GNU_TYPE),$(DEB_HOST_GNU_TYPE))
	CC=gcc
else
	CC=$(DEB_HOST_GNU_TYPE)-gcc
endif

CFLAGS=-Wall -Wextra -D "VERSION=\"$(VERSION)\""

all: $(BIN_NAME)

libserialport/.libs/libserialport.a:
	cd libserialport && ./autogen.sh && ./configure --host=$(DEB_HOST_GNU_TYPE) --enable-static=yes
	$(MAKE) -C libserialport

$(BIN_NAME): scanner.c modbus_crc.c
	$(CC) $(CFLAGS) $^ -o $@ -lserialport

install:
	install -Dm755 $(BIN_NAME) -t $(DESTDIR)$(PREFIX)/bin

clean:
	-@rm -f $(BIN_NAME)
	$(MAKE) -C libserialport clean

.PHONY: clean all install
