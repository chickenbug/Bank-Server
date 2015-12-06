CC = gcc
OFLAGS = -g -pthread -o
CFLAGS = -g -c 
COMPILEO = $(CC) $(OFLAGS)
COMPILEC = $(CC) $(CFLAGS)

all: server client

server: server.o
	$(COMPILEO) server server.o
server.o: server.c
	$(COMPILEC) server.c
client: client.o
	$(COMPILEO) client client.o
client.o: client.c
	$(COMPILEC) client.c

clean:
	rm -f *.o server client vault