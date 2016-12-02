/* Parts of this code is referenced from http://csapp.cs.cmu.edu/2e/ics2/code/conc/echoservers.c*/

/* $begin echoserversmain */
#include "csapp.h"
#include "protocol.h" 
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <inttypes.h>
#include <arpa/inet.h>

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

static int is_port = 0;
static int sn_neighbor_ip = 0;
static int sn_neighbor_port = 0;
static int sn_neighbor_fd = 0;
static int node_id = 0;
static char buf[MAXBUF];

int byte_cnt = 0; /* counts total bytes received by server */

int send_kaza_hdr(int fd, int msg_type)
{
    int send_result;

    kaza_hdr_t *hdr;
    hdr = (kaza_hdr_t *)buf;
    hdr->total_len = sizeof(kaza_hdr_t);
    hdr->id= node_id;
    hdr->msg_type = msg_type;

    Rio_writen(fd, buf, hdr->total_len);
}

int main(int argc, char **argv)
{
    int listenfd, connfd, port; 
    int param_opt, option_index;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in client_addr, sn_neighbor;
    static pool pool; 

    //super-node takes user inputs
    static struct option long_options[] = {
        {"s_ip", required_argument, &sn_neighbor_ip, 1},
        {"s_port", required_argument, &sn_neighbor_port, 1},
        {"my_port", required_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    while((param_opt = getopt_long(argc, argv, "p:", long_options, &option_index)) != -1)
    {
        switch(param_opt)
        {
            case 0:
                if(long_options[option_index].flag == 0)
                    break;
                printf("super| %s: ", long_options[option_index].name);
                if(optarg)
                    printf("%s", optarg);
                printf("\n");
                
                if(strcmp("s_ip", long_options[option_index].name) == 0)
                {
                    if(inet_aton(optarg, &sn_neighbor.sin_addr) == 0)
                    {
                        fprintf(stderr, "Unvalid s_ip address\n");
                        return 0;
                    }
                }

                if(strcmp("s_port", long_options[option_index].name) == 0)
                {
                    sn_neighbor.sin_port = atoi(optarg);
                }

                break;

            case 'p':
                is_port = 1;
                port = atoi(optarg);
                printf("super| my_port: %s\n", optarg);
                break;

            case '?':
                fprintf(stderr, "usage: %s -p [port] --s_ip [ip] --s_port [port]\n", argv[0]);
                return 0;

            default:
                printf("param_opt: %d\n", param_opt);
                fprintf(stderr, "error while reading user input arguments\n");
                return 0;
        }
    }

    if(is_port == 0 || (sn_neighbor_ip == 0 ^ sn_neighbor_port == 0))
    {
        fprintf(stderr, "usage: %s -p [port] --s_ip [ip] --s_port [port]\n", argv[0]);
        return 0;
    }

    //DEBUG
    printf("mine: port=%d, sn_neighbor: ip=%s, port=%u\n", port, inet_ntoa(sn_neighbor.sin_addr), (unsigned)sn_neighbor.sin_port); 
    

    //do connection setup with another super-node
    /*TODO*/  
    if(sn_neighbor_ip && sn_neighbor_port)
    {
        sn_neighbor_fd = Open_clientfd(inet_ntoa(sn_neighbor.sin_addr), sn_neighbor.sin_port);
        send_kaza_hdr(sn_neighbor_fd, HELLO_FROM_SUP_TO_SUP);
        //Close(sn_neighbor_fd);
    }

    //do connection setup to serve as a server  
    listenfd = Open_listenfd(port);
    init_pool(listenfd, &pool); //line:conc:echoservers:initpool
    while (1) {
	/* Wait for listening/connected descriptor(s) to become ready */
	pool.ready_set = pool.read_set;
	pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);

	/* If listening descriptor ready, add new client to pool */
	if (FD_ISSET(listenfd, &pool.ready_set)) { //line:conc:echoservers:listenfdready
	    connfd = Accept(listenfd, (SA *)&client_addr, &clientlen); //line:conc:echoservers:accept
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
    bool is_hdr = true;
    rio_t rio;
    kaza_hdr_t *hdr;

    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
	connfd = p->clientfd[i];
	rio = p->clientrio[i];

	/* If the descriptor is ready, echo a text line from it */
	if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) { 
	    p->nready--;
        //debug
        printf("check_clients: connfd %d\n", connfd);
        byte_cnt = 0;
        while(1)
        {
           n = Rio_readnb(&rio, buf + byte_cnt, MAXLINE);
           if(n == 0)
               break;
           else
               byte_cnt += n;
        }
        printf("check_clients: while end\n");
        hdr = (kaza_hdr_t *)buf;
        if(hdr->msg_type == HELLO_FROM_SUP_TO_SUP)
        {
            sn_neighbor_fd = rio.rio_fd;
            fprintf(stdout, "HELLO_FROM_SUP_TO_SUP recv\n");
        }

	    /* EOF detected, remove descriptor from pool */
		Close(connfd); //line:conc:echoservers:closeconnfd
		FD_CLR(connfd, &p->read_set); //line:conc:echoservers:beginremove
		p->clientfd[i] = -1;          //line:conc:echoservers:endremove
	}
    }
}
/* $end check_clients */

