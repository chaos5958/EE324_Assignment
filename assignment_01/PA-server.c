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
#include <stdbool.h>

#define MAXLINE 100
#define SERVER_ID 0x08
#define LISTENQ 100

static int file_index = 0;
static int server_seq_num = 0;

/* data structure for containg data from client in memory */
typedef struct recv_data 
{
    char *data;
    struct recv_data *next_data;
}RECV_DATA;

int main (int argc, char **argv)
{
    int listenfd, connfd, port;
    char *host, buf[MAXLINE], filename[10];
    struct hostent *hp;
    struct sockaddr_in serveraddr, clientaddr;
    PA_HEADER *pack_header;
    int input_command;
    int optval = 1;
    int clientlen;
    int recv_bytes;
    RECV_DATA *recv_data, *start_data, *temp;
    FILE *fp;
    bool is_first_recv = true;

    int i = 0;

    if (argc != 2)
    {
        fprintf (stderr, "Invalid argument number");
        exit (-1);
    }

    port = atoi(argv[1]);

    /* creating a socket */
    if ((listenfd = socket (AF_INET, SOCK_STREAM, 0)) < 1)
    {
        fprintf (stderr, "Socket allocation failed");
        exit (-1);
    }

    /* set socket options */
    if (setsockopt (listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof (int)) < 0)
    {
        fprintf (stderr, "Socket option setting failed");
        exit (-1);
    }

    /* set a server address */
    bzero ((char *) &serveraddr, sizeof (serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons ((unsigned short) port);
    
    /* bind a socket */
    if (bind (listenfd, (struct sockaddr*) &serveraddr, sizeof (serveraddr)) < 0)
    {
        fprintf (stderr, "Socket bind failed");
        exit (-1);
    }

    /* listen a socket */
    if (listen (listenfd, LISTENQ) < 0)
    {
        fprintf (stderr, "Socket listen failed");
        exit (-1);
    }

    /* packet header initialization */
    pack_header = (PA_HEADER *) malloc (sizeof (PA_HEADER));
    bzero (pack_header, sizeof (PA_HEADER));

    while (1)
    {
        connfd = accept (listenfd, (struct sockaddr *) &clientaddr, &clientlen);

        /* connection setup stage 1 - recv client hello */
        recv (connfd, pack_header, sizeof (PA_HEADER), 0);
        if (pack_header->command != 0x0001)
        {
            fprintf (stderr, "Connection setting failed");
            continue;
        }

        /* connection setup stage 2 - send server hello */
        server_seq_num = rand() % INITIAL_SEQ_NUM_MAX;
        pack_header->ver = PA_VERSION;
        pack_header->user_id = SERVER_ID;
        pack_header->len = sizeof (PA_HEADER);
        pack_header->command = 0x0002;
        pack_header->seq_num = server_seq_num;

        send (connfd, pack_header, sizeof (PA_HEADER), 0);
        server_seq_num++;

        /* communication start */
        while (1)
        {
            bzero (pack_header, sizeof (PA_HEADER));
            recv_bytes = recv (connfd, pack_header, sizeof (PA_HEADER), 0);

            if (recv_bytes == 0)
                break;
            else if (recv_bytes < 0)
            {
                fprintf (stderr, "recv failed");
                continue; 
            }

            switch (pack_header->command)
            {
                /* save data in memory */
                case 0x0003:
                    if (is_first_recv)
                    {
                        recv_data = (RECV_DATA *) malloc (sizeof (RECV_DATA));
                        bzero (recv_data, sizeof (RECV_DATA));
                        start_data = recv_data;
                        is_first_recv = false;
                    }
                    else
                    {
                        recv_data->next_data = (RECV_DATA *) malloc (sizeof (RECV_DATA));
                        bzero (recv_data->next_data, sizeof (RECV_DATA));
                        recv_data = recv_data->next_data;
                    }

                    recv_data->data = (char *) malloc (pack_header->len - sizeof (PA_HEADER));
                    recv (connfd, recv_data->data, 
                            pack_header->len - sizeof (PA_HEADER), 0);
                    //printf ("recv data: %s len: %zu seq_num: %u\n", recv_data->data, pack_header->len - sizeof (PA_HEADER), pack_header->seq_num);
                    break;

                /* move all data in memory to a file system */
                case 0x0004:
                    if (start_data == NULL)
                    {
                        fprintf (stdout, "deliver data first\n");
                        continue;
                    }

                    sprintf (filename, "server%d", file_index);
                    
                    fp = fopen (filename, "w");
                    while (start_data != NULL)
                    {
                        temp = start_data;
                        start_data = start_data->next_data;
                        fputs (temp->data, fp);
                        free (temp->data);
                        free (temp);
                    }
                    fclose (fp);

                    is_first_recv = true;
                    file_index++;
                    break;


                case 0x0005:
                    //*********TODO********//
                    //* to be implemented *//
                    //*********************//
                    fprintf (stdout, "this command is not implemented yet");
                    break;

                default:
                    fprintf (stderr, "invalid command");
                    continue;
            }
        }
    }

    return 0;
}
