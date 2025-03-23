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

libserialport-$(W32_CROSS):
	curl -L https://github.com/sigrokproject/libserialport/releases/download/libserialport-0.1.2/libserialport-0.1.2.tar.gz | tar xzv
	mv libserialport-0.1.2 $@

libserialport-$(W32_CROSS)/.libs/libserialport.a: libserialport-$(W32_CROSS)
	cd $< && ./configure --host $(subst libserialport-,,$<) --enable-static=yes
	make -C $<

$(W32_BIN_NAME): scanner.c modbus_crc.c libserialport-$(W32_CROSS)/.libs/libserialport.a
	$(W32_CROSS)-gcc $(CFLAGS) scanner.c modbus_crc.c $(CC_FLAGS) -I libserialport-$(W32_CROSS) -D_WIN32_WINNT=0x0600 -mconsole -static -L libserialport-$(W32_CROSS)/.libs/ -lserialport -lsetupapi -l ws2_32 -o $(W32_BIN_NAME)
	$(W32_CROSS)-strip --strip-unneeded $(W32_BIN_NAME)

win32: $(W32_BIN_NAME)

install:
	install -Dm755 $(BIN_NAME) -t $(DESTDIR)$(PREFIX)/bin

clean:
	-@rm -f $(BIN_NAME)

.PHONY: clean all install
