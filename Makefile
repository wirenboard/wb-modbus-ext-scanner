PREFIX=/usr

VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')
BIN_NAME=wb-modbus-scanner

ifeq ($(DEB_BUILD_GNU_TYPE),$(DEB_HOST_GNU_TYPE))
	CC=gcc
	confflags=--build=$(DEB_HOST_GNU_TYPE)
else
	CC=$(DEB_HOST_GNU_TYPE)-gcc
	confflags=--build=$(DEB_BUILD_GNU_TYPE) --host=$(DEB_HOST_GNU_TYPE)
endif

CFLAGS=-Wall -Wextra -D "VERSION=\"$(VERSION)\""

ifeq ($(USE_SYSTEM_LIBS),1)
	LIBS += -lserialport
else
	CFLAGS += -Ilibserialport
	LIBS += -Llibserialport/.libs -lserialport -static
endif

all: $(BIN_NAME)

libserialport/.libs/libserialport.a:
	cd libserialport && ./autogen.sh && ./configure $(confflags) --enable-static=yes
	$(MAKE) -C libserialport

$(BIN_NAME): scanner.c modbus_crc.c $(if $(USE_SYSTEM_LIBS),,libserialport/.libs/libserialport.a)
	$(CC) $(CFLAGS) scanner.c modbus_crc.c -o $@ $(LIBS)

install:
	install -Dm755 $(BIN_NAME) -t $(DESTDIR)$(PREFIX)/bin

clean:
	-@rm -f $(BIN_NAME)
	$(MAKE) -C libserialport clean

.PHONY: clean all install
