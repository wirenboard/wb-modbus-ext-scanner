PREFIX=/usr

BIN_NAME=wb-modbus-scanner
W32_CROSS=i686-w64-mingw32
VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')
W32_BIN_NAME=$(BIN_NAME)_$(VERSION).exe

ifeq ($(DEB_BUILD_GNU_TYPE),$(DEB_HOST_GNU_TYPE))
	CC=gcc
else
	CC=$(DEB_HOST_GNU_TYPE)-gcc
endif

CFLAGS=-D "VERSION=\"$(VERSION)\""

all: $(BIN_NAME)

$(BIN_NAME): scanner.c modbus_crc.c
	$(CC) $(CFLAGS) $^ -o $(BIN_NAME) -lserialport

libserialport/.libs/libserialport.a:
	cd libserialport && ./autogen.sh && ./configure --host $(W32_CROSS) --enable-static=yes
	make -C libserialport

$(W32_BIN_NAME): scanner.c modbus_crc.c libserialport/.libs/libserialport.a
	$(W32_CROSS)-gcc $(CFLAGS) scanner.c modbus_crc.c $(CC_FLAGS) -I libserialport -D_WIN32_WINNT=0x0600 -mconsole -static -L libserialport/.libs/ -lserialport -lsetupapi -l ws2_32 -o $(W32_BIN_NAME)
	$(W32_CROSS)-strip --strip-unneeded $(W32_BIN_NAME)

win32: $(W32_BIN_NAME)

install:
	install -Dm755 $(BIN_NAME) -t $(DESTDIR)$(PREFIX)/bin

clean:
	-@rm -f $(BIN_NAME)

.PHONY: clean all install
