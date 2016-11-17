/* Reference
 * 1. http://csapp.cs.cmu.edu/2e/ics2/code/conc/echoservers.c
 * (code related to use select function)
 */

#include "csapp.h"
#include "parse.h"
#include <semaphore.h>
#include <stdbool.h>

#define LISTEN_QUEUE_NUM 100
#define MAX_BUF_SIZE 100000
#define MAX_PORT_LEN 6
#define LOG_MSG_LEN 100
#define WORKER_THREAD_NUM 4
#define ACCEPT_NUM 10000 

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
void check_clients(pool *p, int);
void* child_proxy(void *);
int enqueue_task(int, int);
int dequeue_task(int);
/* $begin echoserversmain */

int byte_cnt = 0; /* counts total bytes received by server */
pthread_cond_t *empty_arr;
pthread_mutex_t *mutex_arr;
int *count_arr;
int **accept_queue;

int enqueue_task(int index, int connfd)
{
    int trylock_result;

    trylock_result = pthread_mutex_trylock(&mutex_arr[index]);
    if(trylock_result == -1)
        return -2;

    if(count_arr[index] == ACCEPT_NUM)
    {
        printf("worker thread's queue is full\n");
        pthread_mutex_unlock(&mutex_arr[index]);
        return -2;
    }
    count_arr[index]++; 
    accept_queue[index][count_arr[index]] = connfd; 
    pthread_cond_signal(&empty_arr[index]);
    pthread_mutex_unlock(&mutex_arr[index]);

    printf("ENQUEUE: connfd %d to thread %d\n", connfd, index);

    return 0;
}

int dequeue_task(int index)
{
    int dequeue_fd;

    printf("DEQUEUE: thread %d start \n", index);
    pthread_mutex_lock(&mutex_arr[index]);
    while (count_arr[index] == -1) {
        pthread_cond_wait(&empty_arr[index], &mutex_arr[index]); 
    }
    dequeue_fd = accept_queue[index][count_arr[index]];
    count_arr[index]--;
    pthread_mutex_unlock(&mutex_arr[index]);
    printf("DEQUEUE: thread %d end\n", index);

    return dequeue_fd; 
}

int main(int argc, char **argv)
{
    int listenfd, connfd, port, num_thread, i, enqueue_result; 
    int *ids;
    int worker_index = 0;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t *pthread_arr;
    static pool pool; 

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    if (argc == 3)
        num_thread = atoi(argv[2]);
    else
        num_thread = WORKER_THREAD_NUM; 

    empty_arr = (pthread_cond_t *)malloc(num_thread * sizeof(pthread_cond_t));
    mutex_arr = (pthread_mutex_t *)malloc(num_thread * sizeof(pthread_mutex_t));
    count_arr = (int *)malloc(num_thread * sizeof(int));
    pthread_arr = (pthread_t *)malloc(num_thread * sizeof(pthread_t));
    ids = (int *)malloc(num_thread * sizeof(int));
    accept_queue = (int **)malloc(num_thread * sizeof(int *));

    for (i = 0; i < num_thread; i++) {
        ids[i] = i;
        pthread_cond_init(&empty_arr[i], NULL);
        pthread_mutex_init(&mutex_arr[i], NULL);
        accept_queue[i] = (int *)malloc(ACCEPT_NUM * sizeof(int));
        count_arr[i] = -1;
    }

    for (i = 0; i < num_thread; i++) {
        int thread_id = pthread_create(&pthread_arr[i], NULL, &child_proxy, (void *)&ids[i]);
        if(thread_id == -1)
        {
            printf("pthread_create fail\n");
            return -1;
        }
    }

    /* TODO: socket non blocking option 
     *       check whether select finds backlog
     *       */
        
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
            /* TODO: LOG information */
            enqueue_result = enqueue_task(worker_index, connfd); 
            //            FILE *fp = fopen("proxy.log", "a");
            //            memset(log, 0, sizeof(log));
            //            memset(s, 0, sizeof(s));
            //            inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr*)&client_addr), s, sizeof s);
            //            fprintf(fp, "%02d %s:%05u\n", count, s, get_in_port((struct sockaddr*)&client_addr));
            //            count = count + 1;
            //            fclose(fp);
            //
            printf("enqueue result: %d\n", enqueue_result);
            if(enqueue_result == -2)
                add_client(connfd, &pool);

            worker_index  = (worker_index + 1) % num_thread;
        }
        check_clients(&pool, worker_index); //line:conc:echoservers:checkclients
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
void check_clients(pool *p, int worker_index) 
{
    int i, connfd, n, enqueue_result;
    char buf[MAXLINE]; 
    rio_t rio;

    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        /* If the descriptor is ready, echo a text line from it */
        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) { 
            p->nready--;
            enqueue_result = enqueue_task(worker_index, connfd);

            if(enqueue_result == 0)
            {
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1;
            }

            //            if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
            //                byte_cnt += n; //line:conc:echoservers:beginecho
            //                printf("Server received %d (%d total) bytes on fd %d\n", 
            //                        n, byte_cnt, connfd);
            //                Rio_writen(connfd, buf, n); //line:conc:echoservers:endecho
            //            }
            //
            //            /* EOF detected, remove descriptor from pool */
            //            else { 
            //                Close(connfd); //line:conc:echoservers:closeconnfd
            //                FD_CLR(connfd, &p->read_set); //line:conc:echoservers:beginremove
            //                p->clientfd[i] = -1;          //line:conc:echoservers:endremove
            //            }
        }
    }
}


/* child process - proxing 1) receive an http request from an client 2) parse it and extract an server address and an port number 3) send the request to a server 4) receive the response from a server and send it to an server */
void *child_proxy(void *args)
{
    int numbytes = 0, numbytes_serv = 0;
    int cnt = 0;
    int connfd = 0, px_sockfd = 0;
    int rv;
    int worker_index;
    char recv_buf[MAX_BUF_SIZE];
    HTTP_REQUEST* http_request = NULL;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    bool is_working = false;

    worker_index = *(int *)(args);

    printf("thread %d start\n", worker_index);
    while(1)
    {
find_work:
        if(!is_working)
        {
            //initialize the working thread
            memset(recv_buf, 0, sizeof(recv_buf));
            if(http_request != NULL)
            {
                free_http_req(http_request);
                http_request = NULL;
            }

            if(connfd != 0)
            {
                close(connfd);
                connfd = 0;
            }
            if(px_sockfd != 0)
            {
                close(px_sockfd);
                px_sockfd = 0;
            }
            cnt = 0;
            numbytes = 0;
            numbytes_serv = 0;

            //get the work from the waiting queue 
            connfd = dequeue_task(worker_index);
            is_working = true;
        }
        else
        {
            printf("thread %d: handle fd %d\n", worker_index, connfd);

            cnt = recv(connfd, recv_buf + numbytes, MAX_BUF_SIZE, 0);
            if(cnt > 0)
                numbytes += cnt;
            else if(cnt == -1)
            {
                perror("worker thread recv from the client");
                is_working = false;
            }
            else if(cnt == 0)
                is_working = false;

            printf("thread receive %d bytes \n", numbytes);

            //client's http-request ends
            if(strstr(recv_buf, "\r\n\r\n") != NULL)
            {
                http_request = parse_http_request(recv_buf);

                if(http_request == NULL)
                {
                    is_working = false;
                    goto find_work;
                }

                //connect to a server
                if((rv = getaddrinfo(http_request->host_addr, http_request->host_port, &hints, &servinfo))  != 0)
                {
                    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
                    goto find_work;
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
                    fprintf(stderr, "proxy| clinet: failed to connect\n");
                    is_working = false;
                    goto find_work;
                }
                
                printf("CONNECTION: thread %d connection success\n", worker_index);

                //send the http-request to a server
                if((send (px_sockfd, recv_buf, sizeof(recv_buf), 0) == -1))
                {
                    perror ("proxy| send");
                    is_working = false;
                    goto find_work;
                }

                //receive the http-response from a server
                while(1)
                {
                    cnt = recv(px_sockfd, recv_buf, sizeof(recv_buf), 0);
                    if(cnt > 0)
                    {
                        if((send(connfd, recv_buf, cnt, 0)) == -1)
                        {
                            perror("worker thread send to the client");
                            is_working = false;
                            goto find_work;
                        }
                    }
                    else if(cnt == -1)
                    {   
                        perror("worker thread recv from server");
                        break;
                    }
                    else if(cnt == 0)
                        break;
                }

                printf("RECEIVE_SERVER: thread %d get %d bytes\n", worker_index, numbytes_serv);

                is_working = false;
                printf("thread %d: terminate fd %d\n", worker_index, connfd);
            }
        }
    }
}
