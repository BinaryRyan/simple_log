CC = gcc
CFLAGS = -Wall -Wextra -std=c11

all: simple_log

simple_log: simple_log.o log.o
	$(CC) $(CFLAGS) -o simple_log simple_log.o log.o

simple_log.o: simple_log.c log.h
	$(CC) $(CFLAGS) -c simple_log.c

log.o: log.c log.h
	$(CC) $(CFLAGS) -c log.c

clean:
	rm -f *.o simple_log
	rm -rf ./*.log