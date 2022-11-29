CC = gcc
TOOLCHAIN := arm-linux-gnueabihf-

BIN_NAME=wb-modbus-scanner

all: $(BIN_NAME)


$(BIN_NAME): scanner.c modbus_crc.c
	$(TPATH)$(TOOLCHAIN)$(CC) $(CFLAGS) $^ -o $(BIN_NAME)

clean:
	-@rm -f $(BIN_NAME)

.PHONY: clean all install

