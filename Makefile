CC		= gcc
LD		= gcc
CFLAGS		= -std=gnu11 -Wall -g

LDFLAGS		=
DEFS		=

all:	sendfile recvfile

sendfile: sendfile.c
	$(CC) $(DEFS) $(CFLAGS) $(LIB) sendfile.c -o sendfile

recvfile: recvfile.c
	$(CC) $(DEFS) $(CFLAGS) $(LIB) recvfile.c -o recvfile

clean:
	rm -f *.o
	rm -f *~
	rm -f core.*
	rm -f sendfile
	rm -f recvfile

