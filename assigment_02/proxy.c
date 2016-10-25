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

#define LISTEN_QUEUE_NUM 100
#define MAX_REQ_SIZE 10000
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
    int sockfd, connfd;
    int sock_binary_opt = 1;
    int child_pid;
    int rv;
    char s[INET6_ADDRSTRLEN];
    char req_buf[MAX_REQ_SIZE];
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    struct sigaction sa;
    socklen_t sin_size;

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
            int servfd;
            int numbytes = 0;

            if((numbytes += recv(connfd, req_buf + numbytes, MAX_REQ_SIZE, 0)) == -1)
            {
                perror("proxy: receive from client");
                exit(1);
            }

            if(strstr(req_buf, "\r\n\r\n") != NULL)
            {
                //parse host, port
                //connect to a server
                //send data to a server
                //receive data from a server
            }

            return 0;
        }
    }

    return 0;
}
            

    
