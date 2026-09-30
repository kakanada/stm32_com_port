# Сборка Linux-примера: make  ->  ./linux_example /dev/ttyACM0
CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
INC      = -I../common -I../linux -I.
SRC      = linux_example.c ../linux/scom_linux.c ../common/scom_frame.c ../common/scom_crc32.c

linux_example: $(SRC) example_types.h
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@ -pthread

clean:
	rm -f linux_example

.PHONY: clean
