# Сборка примера на Linux: make  ->  ./host_example /dev/ttyACM0
# На Windows используйте CMakeLists.txt (Visual Studio / MinGW).
CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
INC      = -I../common -I../host -I.
SRC      = host_example.c ../host/scom_host.c ../host/scom_host_posix.c \
           ../host/scom_host_win32.c ../common/scom_frame.c ../common/scom_crc32.c

host_example: $(SRC) example_types.h
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@ -pthread

clean:
	rm -f host_example

.PHONY: clean
