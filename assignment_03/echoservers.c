/* Reference
 * 1. http://csapp.cs.cmu.edu/2e/ics2/code/conc/echoservers.c
 * (code related to use select function)
 */

#include "csapp.h"
#include "parse.h"

#define LISTEN_QUEUE_NUM 100
#define MAX_BUF_SIZE 100000
#define MAX_PORT_LEN 6
#define LOG_MSG_LEN 100


typedef struct { /* represents a pool of connected descriptors */ //line:conc:echoservers:beginpool
    int maxfd;        /* largest descriptor in read_set */   
    fd_set read_set;  /* set of all active descriptors */
    fd_set ready_set; /* subset of descriptors ready for reading  */
    int nready;       /* number of ready descriptors from select */   
    int maxi;         /* highwater index into client array */
    int clientfd[FD_SETSIZE];    /* set of active descriptors */
    rio_t clientrio[FD_SETSIZE]; /* set of active read buffers */
} pool; //line:conc:echoservers:endpool
/* $end echoserversmain */
void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p);
/* $begin echoserversmain */

int byte_cnt = 0; /* counts total bytes received by server */

int main(int argc, char **argv)
{
    int listenfd, connfd, port; 
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    static pool pool; 

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);
    init_pool(listenfd, &pool); //line:conc:echoservers:initpool
    while (1) {
        /* Wait for listening/connected descriptor(s) to become ready */
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);

        /* If listening descriptor ready, add new client to pool */
        if (FD_ISSET(listenfd, &pool.ready_set)) { //line:conc:echoservers:listenfdready
            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:conc:echoservers:accept
            add_client(connfd, &pool); //line:conc:echoservers:addclient
        }

        /* Echo a text line from each ready connected descriptor */ 
        check_clients(&pool); //line:conc:echoservers:checkclients
    }
}
/* $end echoserversmain */

/* $begin init_pool */
void init_pool(int listenfd, pool *p) 
{
    /* Initially, there are no connected descriptors */
    int i;
    p->maxi = -1;                   //line:conc:echoservers:beginempty
    for (i=0; i< FD_SETSIZE; i++)  
        p->clientfd[i] = -1;        //line:conc:echoservers:endempty

    /* Initially, listenfd is only member of select read set */
    p->maxfd = listenfd;            //line:conc:echoservers:begininit
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set); //line:conc:echoservers:endinit
}
/* $end init_pool */

/* $begin add_client */
void add_client(int connfd, pool *p) 
{
    int i;
    p->nready--;
    for (i = 0; i < FD_SETSIZE; i++)  /* Find an available slot */
        if (p->clientfd[i] < 0) { 
            /* Add connected descriptor to the pool */
            p->clientfd[i] = connfd;                 //line:conc:echoservers:beginaddclient
            Rio_readinitb(&p->clientrio[i], connfd); //line:conc:echoservers:endaddclient

            /* Add the descriptor to descriptor set */
            FD_SET(connfd, &p->read_set); //line:conc:echoservers:addconnfd

            /* Update max descriptor and pool highwater mark */
            if (connfd > p->maxfd) //line:conc:echoservers:beginmaxfd
                p->maxfd = connfd; //line:conc:echoservers:endmaxfd
            if (i > p->maxi)       //line:conc:echoservers:beginmaxi
                p->maxi = i;       //line:conc:echoservers:endmaxi
            break;
        }
    if (i == FD_SETSIZE) /* Couldn't find an empty slot */
        app_error("add_client error: Too many clients");
}
/* $end add_client */

/* $begin check_clients */
void check_clients(pool *p) 
{
    int i, connfd, n;
    char buf[MAXLINE]; 
    rio_t rio;

    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        /* If the descriptor is ready, echo a text line from it */
        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) { 
            p->nready--;
            if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
                byte_cnt += n; //line:conc:echoservers:beginecho
                printf("Server received %d (%d total) bytes on fd %d\n", 
                        n, byte_cnt, connfd);
                Rio_writen(connfd, buf, n); //line:conc:echoservers:endecho
            }

            /* EOF detected, remove descriptor from pool */
            else { 
                Close(connfd); //line:conc:echoservers:closeconnfd
                FD_CLR(connfd, &p->read_set); //line:conc:echoservers:beginremove
                p->clientfd[i] = -1;          //line:conc:echoservers:endremove
            }
        }
    }
}


/* child process - proxing 1) receive an http request from an client 2) parse it and extract an server address and an port number 3) send the request to a server 4) receive the response from a server and send it to an server */
void *child_proxy(void *args)
{
    int numbytes = 0;
    int connfd = *(int *)args;
    int rv;
    char recv_buf[MAX_BUF_SIZE];
    HTTP_REQUEST* http_request;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    int px_sockfd;
    //Todo: use while
    //recv a http-request from an client
    if((numbytes += recv(connfd, recv_buf + numbytes, MAX_BUF_SIZE, 0)) == -1)
    {
        perror("proxy: receive from client");
        exit(1);
    }
    //printf("%s", recv_buf);

    //client's http-request ends
    if(strstr(recv_buf, "\r\n\r\n") != NULL)
    {
        http_request = parse_http_request(recv_buf, numbytes);
        //print_http_req(http_request);

        //connect to a server
        if((rv = getaddrinfo(http_request->host_addr, http_request->host_port, &hints, &servinfo))  != 0)
        {
            close(connfd);
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
            free_http_req(http_request);
            exit(-1);
        }

        for(p = servinfo; p !=  NULL; p = p->ai_next)
        {
            if((px_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            {
                perror("proxy| client: socket");
                continue;
            }

            if (connect(px_sockfd, p->ai_addr, p->ai_addrlen) == -1){
                close (px_sockfd);
                perror("pxoxy| clinet: connect");

                if (p == p->ai_next)
                    break;
                continue;
            }
            break;
        }

        if(p == NULL){
            close(connfd);
            fprintf(stderr, "proxy| clinet: failed to connect\n");
            free_http_req(http_request);
            exit (-1);
        }
        //printf("connection success\n");

        //send the http-request to a server
        if((send (px_sockfd, recv_buf, sizeof(recv_buf), 0) == -1))
        {
            close(connfd); close(px_sockfd);
            perror ("proxy| send");
            free_http_req(http_request);
            exit (-1);
        }
        free_http_req(http_request);

        //printf("send success\n");

        //receive the http-response from a server
        while(1)
        {
            if((numbytes = recv(px_sockfd, recv_buf, sizeof(recv_buf), 0)) == -1)
            {
                close(connfd);
                close(px_sockfd);
                perror("recv");
                exit(-1);
            }
            //printf("proxy recv from server: %s", recv_buf);

            if(numbytes == 0)
                break;

            if((send(connfd, recv_buf, numbytes, 0)) == -1)
            {
                close(connfd);
                close(px_sockfd);
                perror("send");
                exit(-1);
            }
        }
        //close all connection and terminate 
        close(connfd);
        close(px_sockfd);
        return 0;
    }
}
