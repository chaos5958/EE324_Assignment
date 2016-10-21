#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "PA-protocol.h"

#define MAXLINE 100
#define CLIENT_ID 0x08
#define MAXFILENAME 20

static int client_seq_num = 0;

int send_data (char * file_name, int client_f);

/* get appropraite IP address (IPV4 IPV6) */
void *get_in_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET){
	return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main (int argc, char **argv)
{
    int clientfd;
    char *host, buf[MAXLINE], *port;
    struct hostent *hp;
    struct sockaddr_in serveraddr;
    struct addrinfo hints, *servinfo, *p;
    char file_name[MAXFILENAME];
    PA_HEADER *pack_header;
    int input_command, rv;
    char s[INET6_ADDRSTRLEN];
    
    if (argc != 3)
    {
        fprintf (stderr, "Invalid argument number");
        exit (-1);
    }

    host = argv[1];
    port = argv[2];
    
    /* get server address information */
    bzero (&hints, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo (host, port, &hints, &servinfo)) != 0)
    {
        fprintf (stderr, "getaddrinfo:%s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        /* creating a client socket */
        if ((clientfd = socket (p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror ("client: socket");
            continue;
        }

        /* connect to a server */
        if (connect (clientfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close (clientfd);
            perror ("client: connect");

            if (p == p->ai_next){
                break;
            }

            continue;
        }
        break;

    }

    if (p == NULL)
    {
        fprintf (stderr, "client: failed to connect\n");
        exit (-1);
    }
    
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf ("client: connecting to %s\n", s);
    freeaddrinfo (servinfo);


    /* Initailize a sequence number */
    client_seq_num = rand() % INITIAL_SEQ_NUM_MAX;

    /* connection setup */
    pack_header = (PA_HEADER *) malloc (sizeof (PA_HEADER));
    pack_header->ver = PA_VERSION;
    pack_header->user_id = CLIENT_ID;
    pack_header->len = sizeof (PA_HEADER);
    pack_header->command = 0x0001;
    pack_header->seq_num = client_seq_num;

    /* connection setup stage 1 - send client hello */
    send (clientfd, pack_header, sizeof (PA_HEADER), 0);
    client_seq_num++;
    
    /* connection setup stage 2 - recv server hello */
    recv (clientfd, pack_header, sizeof (PA_HEADER), 0);
    if (pack_header->command != 0x0002)
    {
        fprintf (stderr, "connection setup failed - application layer");
        exit (-1);
    }

    /* communication start */
    while (1)
    {
        printf ("type a command: ");
        scanf ("%d", &input_command);

        switch (input_command)
        {
            // data delivery command
            case 0x003:  
                printf ("type a file name: ");
                scanf ("%s", file_name);
                
                if (send_data (file_name, clientfd) < 0)
                {
                    fprintf(stderr, "send data failed\n");
                    continue;
                }
                break;

            // data store command
            case 0x004:
                pack_header->command = 0x004;
                send (clientfd, pack_header, sizeof (PA_HEADER), 0);
                client_seq_num++;
                break;

            // error command 
            case 0x005:
                //*********TODO********//
                //* to be implemented *//
                //*********************//
                fprintf (stdout, "this command is not implemented yet\n");
                break;

            // invalid commands
            default: 
                fprintf (stdout, "invalid command\n");
                continue;
        }
    }                

    return 0;
}

/* send a packet containg data read from a file*/
int send_data (char *file_name, int client_fd)
{
    FILE *fp;
    char *packet, data[MAXLINE];
    PA_HEADER header;
    ssize_t send_bytes;

    fp = fopen(file_name, "r");
    if (fp == NULL)
    {
        fprintf (stdout, "file name invalid\n");
        return -1;
    }

    header.ver = PA_VERSION;
    header.user_id = CLIENT_ID;
    header.command = 0x0003;
    header.seq_num = client_seq_num;

    while (1)
    {
        if (fgets (data, MAXLINE, fp) == NULL)
            break;
        packet = (char *) malloc (sizeof (PA_HEADER) + strlen (data) + 1);
        bzero (packet, sizeof (PA_HEADER) + strlen (data) + 1);
        header.len = sizeof (PA_HEADER) + strlen (data) + 1;
        header.seq_num = client_seq_num;
        bcopy (&header, packet, sizeof (PA_HEADER));
        bcopy (&data, packet + sizeof (PA_HEADER), strlen (data) + 1);
        
        printf ("data: %s, length: %zu\n", data, strlen (data));
        send_bytes = send (client_fd, packet, sizeof (PA_HEADER) + strlen (data) + 1, 0);
        client_seq_num++;

        if (send_bytes == 0)
            break; 
        else if (send_bytes < 0)
        {
            fclose (fp);
            return -1;
        }
        
    }

    fclose (fp);
    return 0;
}

