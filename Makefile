CC = gcc

BIN_NAME=wb-modbus-scanner

all: $(BIN_NAME)

DEB_VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')

$(BIN_NAME): scanner.c modbus_crc.c
	$(CC) $(CFLAGS) $^ -o $(BIN_NAME) -D "VERSION=\"$(DEB_VERSION)\""

clean:
	-@rm -f $(BIN_NAME)

.PHONY: clean all install

