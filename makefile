CC = gcc
CFLAGS = -Wall -Wextra -std=c11

all: test_log

test_log: test_log.o log.o
	$(CC) $(CFLAGS) -o test_log test_log.o log.o

test_log.o: test_log.c log.h
	$(CC) $(CFLAGS) -c test_log.c

log.o: log.c log.h
	$(CC) $(CFLAGS) -c log.c

clean:
	rm -f *.o test_log