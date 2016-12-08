OBJS1 = super.o csapp.o
OBJS2 = child.o csapp.o
SRCS = super.c csapp.c
CC = gcc
CFLAGS = -o
TARGET1 = super 
TARGET2 = child
OPTION = -lpthread

all : $(OBJS1) $(OBJS2)
	$(CC) $(CFLAGS) $(TARGET1) $(OBJS1) $(OPTION)
	$(CC) $(CFLAGS) $(TARGET2) $(OBJS2) $(OPTION)

super.o: super.c
	gcc -c super.c 

child.o: child.c
	gcc -c child.c

csapp.o: csapp.h csapp.c
	gcc -c csapp.c

clean:	
	rm super child super.o child.o csapp.o

