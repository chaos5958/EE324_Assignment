OBJS = super.o csapp.o
SRCS = super.c csapp.c
CC = gcc
CFLAGS = -o
TARGET1 = super 
OPTION = -lpthread

$(TARGET1) : $(OBJS)
	$(CC) $(CFLAGS) $(TARGET1) $(OBJS) $(OPTION)

super.o: super.c
	gcc -c super.c 

csapp.o: csapp.h csapp.c
	gcc -c csapp.c

clean:	
	rm super super.o csapp.o

