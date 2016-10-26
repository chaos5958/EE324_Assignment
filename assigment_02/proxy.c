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

#define LISTEN_QUEUE_NUM 100
#define MAX_BUF_SIZE 10000
#define MAX_PORT_LEN 6
void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void *get_in_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);   
}

int main (int argc, char *argv[])
{
    char proxy_port_num[MAX_PORT_LEN];
    int sockfd, connfd, px_sockfd;
    int sock_binary_opt = 1;
    int child_pid;
    int rv, numbytes = 0;
    char s[INET6_ADDRSTRLEN];
    char recv_buf[MAX_BUF_SIZE];
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    struct sigaction sa;
    socklen_t sin_size;
    HTTP_REQUEST* http_request;

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

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if(sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    child_pid =  1;
    while(1)
    {
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

            inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr*)&client_addr), s, sizeof s);
            fprintf(stdout, "server: got connection from %s\n", s);
            child_pid = fork();
        }

        // parent proces
        if(child_pid != 0)
            close(connfd);

        // child process
        if(child_pid == 0)
        {
            if((numbytes += recv(connfd, recv_buf + numbytes, MAX_BUF_SIZE, 0)) == -1)
            {
                perror("proxy: receive from client");
                exit(1);
            }
            printf("income string: %s", recv_buf);

            if(strstr(recv_buf, "\r\n\r\n") != NULL)
            {
                //parse host, port**
                //connect to a server**
                //send data to a server**
                //receive data from a server**

                printf("rnrn loop enter\n");

                http_request = parse_http_request(recv_buf, numbytes);
                print_http_req(http_request);

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
                printf("connection success\n");

                if((send (px_sockfd, http_request->request_msg, http_request->request_msg_len, 0) == -1))
                {
                    close(connfd);
                    close(px_sockfd);
                    perror ("proxy| send");
                    free_http_req(http_request);
                    exit (-1);
                }
                free_http_req(http_request);

                printf("send success\n");
                while(1)
                {
                    memset(recv_buf, 0, sizeof recv_buf);

                    if((numbytes = recv(px_sockfd, recv_buf, sizeof(recv_buf), 0)) == -1)
                    {
                        close(connfd);
                        close(px_sockfd);
                        perror("recv");
                        exit(-1);
                    }
                    printf("proxy recv from server: %s", recv_buf);

                    if(numbytes == 0)
                        break;

                    if((send(connfd, recv_buf, sizeof(recv_buf), 0)) == -1)
                    {
                        close(connfd);
                        close(px_sockfd);
                        perror("send");
                        exit(-1);
                    }
                }
                close(connfd);
                close(px_sockfd);
                return 0;
            }
        }
    }

    return 0;
}
            

    
