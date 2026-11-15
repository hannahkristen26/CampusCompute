CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -O2 -pthread
LDFLAGS = -pthread -lrt
TARGET  = campuscompute
SRC     = main.c util.c scheduler.c logger.c ipc.c banker.c workers.c limits.c
OBJ     = $(SRC:.c=.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c campuscompute.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) all

clean:
	rm -f $(OBJ) $(TARGET) campuscompute.log
