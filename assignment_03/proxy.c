#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include "parse.h"
#include "csapp.h" 
#include <sys/select.h>

#define LISTEN_QUEUE_NUM 100
#define MAX_BUF_SIZE 100000
#define MAX_PORT_LEN 6
#define LOG_MSG_LEN 100

typedef struct s_pool{
    int maxfd;
    fd_set read_set;
    fd_set write_set;
    fd_set ready_set;
    int nready;
    int maxi;
    int clientfd[FD_SETSIZE];
    rio_t clientrio[FD_SETSIZE];
} pool;

void init_pool(int, pool *);
void add_client(int, pool *);
void check_clients(pool *);

void init_pool(int listenfd, pool *p)
{
    int i;
    p->maxi = -1;
    for (i = 0; i < FD_SETSIZE; i++) {
        p->clientfd[i] = -1;
    }   
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);
}
/*************************************************************
 * FUNCTION NAME: sigchld_handler                                         
 * PARAMETER: 1) int s: not used                                              
 * PURPOSE:  deallocating all resource that an child hold by calling waitpid
 ************************************************************/
void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

/*************************************************************
 * FUNCTION NAME: get_in_addr                                         
 * PARAMETER: 1)struct sockaddr *sa: an input socket address 
 * PURPOSE: return an ip address from an struct sockaddr 
 ************************************************************/
void *get_in_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);   
}

/*************************************************************
 * FUNCTION NAME: get_in_port                                         
 * PARAMETER: 1)struct sockaddr *sa: an input socket address
 * PURPOSE: return an port number from an struct sockaddr
 ************************************************************/
unsigned short get_in_port(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
        return ((struct sockaddr_in*)sa)->sin_port;
    return 0;
}

int main (int argc, char *argv[])
{
    char proxy_port_num[MAX_PORT_LEN];
    int sockfd, connfd, px_sockfd;
    int sock_binary_opt = 1;
    int child_pid;
    int rv, numbytes = 0;
    char s[INET6_ADDRSTRLEN];
    char log[LOG_MSG_LEN];
    char recv_buf[MAX_BUF_SIZE];
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    struct sigaction sa;
    socklen_t sin_size;
    HTTP_REQUEST* http_request;
    static int count = 0;

    /* check input parameters are valid */
    if (argc == 2)
        strncpy(proxy_port_num, argv[1], sizeof proxy_port_num);
    else
    {
        fprintf(stderr, "usage: argument number incorrect\n");
        exit(1);
    }

    if (atoi(proxy_port_num) < 1024 || atoi(proxy_port_num) > 65535)
    {
        fprintf(stderr, "usage: unavaliable port");
        exit(1);
    }

   
    /* bind, listen to the port */ 
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, proxy_port_num, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }

        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sock_binary_opt, sizeof(int)) == -1)
        {
            perror("setsockopt");
            continue;
        }

        if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("bind");
            continue;
        }

        break;
    }

    if(p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    if(listen(sockfd, LISTEN_QUEUE_NUM) == -1)
    {
        perror("listen");
        exit(1);
    }

    /* add the handler to the SIGCHLD for deallocating child's resource when it is terminated */ 
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if(sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    /* multi-client proxing while loop */
    child_pid =  1;
    while(1)
    {
        /* parent process - 1) accpet 2) log an clinet address 3) fork */
        if (child_pid != 0)
        {
            sin_size = sizeof(client_addr);
            connfd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);

            if(connfd == -1)
            {
                perror("accept");
                close(sockfd);
                exit(1);
            }

            FILE *fp = fopen("proxy.log", "a");
            memset(log, 0, sizeof(log));
            memset(s, 0, sizeof(s));
            inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr*)&client_addr), s, sizeof s);
            fprintf(fp, "%02d %s:%05u\n", count, s, get_in_port((struct sockaddr*)&client_addr));
            count = count + 1;
            fclose(fp);

            child_pid = fork();
        }

        /* parent proces - close connected socket to an client */
        if(child_pid != 0)
            close(connfd);

        /* child process - proxing 1) receive an http request from an client 2) parse it and extract an server address and an port number 3) send the request to a server 4) receive the response from a server and send it to an server */
        if(child_pid == 0)
        {
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
                    close(connfd);
                    close(px_sockfd);
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
    }

    close(connfd);
    return 0;
}



