OBJS = proxy.o parse.o csapp.o
SRCS = proxy.c parse.c csapp.c
CC = gcc
CFLAGS = -o
TARGET = proxy
OPTION = -lpthread

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(TARGET) $(OBJS) $(OPTION)

proxy.o : parse.h proxy.c
	gcc -c proxy.c

parse.o: parse.h parse.c
	gcc -c parse.c
	
csapp.o: csapp.h csapp.c
	gcc -c csapp.c

clean:	
	rm proxy proxy.o parse.o csapp.o

