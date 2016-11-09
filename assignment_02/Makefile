OBJS = proxy.o parse.o
SRCS = proxy.c parse.c
CC = gcc
CFLAGS = -o
TARGET = proxy

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(TARGET) $(OBJS)

proxy.o : parse.h proxy.c
	gcc -c proxy.c

parse.o: parse.h parse.c
	gcc -c parse.c
	
clean:	
	rm proxy proxy.o parse.o

