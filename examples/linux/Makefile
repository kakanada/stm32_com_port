# Сборка примера для Linux (Ubuntu): make  ->  ./example_linux /dev/ttyACM0
CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
ROOT     = ../..
INC      = -I$(ROOT)/common -I$(ROOT)/host -I.. -I../monitor
SRC      = main_linux.c ../monitor/example_monitor.c \
           $(ROOT)/host/scom_host.c $(ROOT)/host/scom_host_posix.c \
           $(ROOT)/host/scom_host_win32.c \
           $(ROOT)/common/scom_frame.c $(ROOT)/common/scom_crc32.c

example_linux: $(SRC) ../example_types.h ../monitor/example_monitor.h
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@ -pthread

clean:
	rm -f example_linux

.PHONY: clean
