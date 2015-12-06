CC = gcc
OFLAGS = -g -pthread -o
CFLAGS = -g -c 
COMPILEO = $(CC) $(OFLAGS)
COMPILEC = $(CC) $(CFLAGS)

all: server client

server: server.o
	$(COMPILEO) server server.o
server.o: server.c server.h
	$(COMPILEC) server.c server.h
client: client.o
	$(COMPILEO) client client.o
client.o: client.c client.h
	$(COMPILEC) client.c client.h

clean:
	rm -f *.o server client vault