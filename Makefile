
CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2

TARGET = process-monitor
OBJS = main.o process.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

main.o: main.c process.h
	$(CC) $(CFLAGS) -c main.c

process.o: process.c process.h
	$(CC) $(CFLAGS) -c process.c

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
