/* Reference
 * 1. http://csapp.cs.cmu.edu/2e/ics2/code/conc/echoservers.c
 * (code related to use select function)
 */

#include "csapp.h"
#include "protocol.h" 
#include <semaphore.h>
#include <stdbool.h>
#include <sys/time.h>
#include <getopt.h>

#define LISTEN_QUEUE_NUM 100
#define MAX_BUF_SIZE 10000 
#define MAX_PORT_LEN 6
#define LOG_MSG_LEN 100
#define WORKER_THREAD_NUM 1 
#define ACCEPT_NUM 10000 

typedef struct { /* represents a pool of connected descriptors */ //line:conc:echoservers:beginpool
    int maxfd;        /* largest descriptor in read_set */   
    fd_set read_set;  /* set of all active descriptors */
    fd_set ready_set; /* subset of descriptors ready for reading  */
    int nready;       /* number of ready descriptors from select */   
    int maxi;         /* highwater index into client array */
    int clientfd[FD_SETSIZE];    /* set of active descriptors */
    rio_t clientrio[FD_SETSIZE]; /* set of active read buffers */
} pool; 

void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p, int*, int);
void* supernode_work(void *);
int enqueue_task(int, int);
int dequeue_task(int);

int byte_cnt = 0; /* counts total bytes received by server */
pthread_cond_t *empty_arr; /* condition variable to wake up worker threads when input comes */
pthread_mutex_t *mutex_arr; /* mutext for prevent racing condition between the acceptor thread and the worker thread (put/get into/from the queue) */
int *count_arr; /* managing how many number of tasks in the worker thread queue */
int **accept_queue; /* worker threads's queue */
static int is_port = 0;
static int sn_neighbor_ip = 0;
static int sn_neighbor_port = 0;
static int sn_neighbor_fd = 0;
static int node_id = 0;
static char buf[MAXBUF];

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

/*************************************************************
 * FUNCTION NAME: enqueue_task                                         
 * PARAMETER: 1)index: worker thread's index 2)connf: accepted file descriptor to be enqueued                                              
 * PURPOSE: enqueue a task into a worker thread queue 
 ************************************************************/
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

    return 0;
}

/*************************************************************
 * FUNCTION NAME: dequeue_task                                         
 * PARAMETER: 1)index: worker thread's index                                              
 * PURPOSE: dequeue a task from a worker thread queue
 ************************************************************/
int dequeue_task(int index)
{
    int dequeue_fd;

    pthread_mutex_lock(&mutex_arr[index]);
    while (count_arr[index] == -1) {
        pthread_cond_wait(&empty_arr[index], &mutex_arr[index]); 
    }
    dequeue_fd = accept_queue[index][count_arr[index]];
    count_arr[index]--;
    pthread_mutex_unlock(&mutex_arr[index]);

    return dequeue_fd; 
}

int main(int argc, char **argv)
{
    int listenfd, connfd, port, num_thread, i, enqueue_result; 
    int *ids;
    int worker_index = 0;
    int count = 0;
    int param_opt, option_index = 0;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr, sn_neighbor;
    pthread_t *pthread_arr;
    static pool pool; 
    struct timeval timeout;
    char s[INET_ADDRSTRLEN]; 
    
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
    if(sn_neighbor_ip && sn_neighbor_port)
    {
        sn_neighbor_fd = Open_clientfd(inet_ntoa(sn_neighbor.sin_addr), sn_neighbor.sin_port);
        send_kaza_hdr(sn_neighbor_fd, HELLO_FROM_SUP_TO_SUP);
        //TODO: receiving ID from neighbor 
        Close(sn_neighbor_fd);
    }

    //initialize a working thread number;
    num_thread = WORKER_THREAD_NUM;

    //initializing data used in the acceptor thread
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

    //creating worker threadds
    for (i = 0; i < num_thread; i++) {
        int thread_id = pthread_create(&pthread_arr[i], NULL, &supernode_work, (void *)&ids[i]);
        if(thread_id == -1)
        {
            perror("thread creation failed");
            return -1;
        }
    }

    //listen 
    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);
    init_pool(listenfd, &pool); 

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    /* the accpetor thread job */
    while (1) {
        /* Wait for listening/connected descriptor(s) to become ready - timeout 5 seconds to check_clients frequently*/
        pool.ready_set = pool.read_set;
        pool.nready = select(pool.maxfd+1, &pool.ready_set, NULL, NULL, &timeout);

        /* If listening descriptor ready, add new client to pool */
        if (FD_ISSET(listenfd, &pool.ready_set)) { 
            connfd = accept(listenfd, (SA *)&clientaddr, &clientlen); 

            if(connfd < 0)
            {
                perror("main thread: accept failed\n");
                continue;
            }

            /*distribute accepted file descriptors to the worker threads*/
            enqueue_result = enqueue_task(worker_index, connfd); 

            /*if the acceptor thread failed to enqueue, put into the pool*/
            if(enqueue_result == -2)
            {
                printf("queue fail\n");
                add_client(connfd, &pool);
            }
            /* log 1)count 2)client address 3)client port */
            FILE *fp = fopen("proxy.log", "a");
            memset(s, 0, sizeof(s));
            inet_ntop(clientaddr.sin_family, &clientaddr.sin_addr, s, sizeof s);
            fprintf(fp, "%02d %s:%05u\n", count, s, clientaddr.sin_port);
            count = count + 1;
            fclose(fp);

            /* increase worker thread index to balance load */
            worker_index  = (worker_index + 1) % num_thread;
        }
        if(enqueue_result == -2)
        {
            check_clients(&pool, &worker_index, num_thread); 
        }
    }

    /* retrive all woker threads's resource */
    for (i = 0; i < num_thread; i++) {
       pthread_join(pthread_arr[i], NULL); 
    }

    return 0;
}

/*************************************************************
 * FUNCTION NAME: init_pool                                         
 * PARAMETER: 1)listenfd: listen file descriptor for the acceptor thread 2) p: target to be initalized                                              
 * PURPOSE: initialize the pool structure 
 ************************************************************/
void init_pool(int listenfd, pool *p) 
{
    /* Initially, there are no connected descriptors */
    int i;
    p->maxi = -1;                   
    for (i=0; i< FD_SETSIZE; i++)  
        p->clientfd[i] = -1;        

    /* Initially, listenfd is only member of select read set */
    p->maxfd = listenfd;            
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set); 
}

/*************************************************************
 * FUNCTION NAME: add_client                                         
 * PARAMETER: 1)connfd: accepted file descriptor 2) p: pool wich will include a new client
 * PURPOSE: add client who connects with connfd to the pool struct 
 * REFERENCE: CMU library
 ************************************************************/
void add_client(int connfd, pool *p) 
{
    int i;
    p->nready--;
    for (i = 0; i < FD_SETSIZE; i++)  /* Find an available slot */
        if (p->clientfd[i] < 0) { 
            /* Add connected descriptor to the pool */
            p->clientfd[i] = connfd;                 
            Rio_readinitb(&p->clientrio[i], connfd); 

            /* Add the descriptor to descriptor set */
            FD_SET(connfd, &p->read_set); 

            /* Update max descriptor and pool highwater mark */
            if (connfd > p->maxfd) 
                p->maxfd = connfd; 
            if (i > p->maxi)      
                p->maxi = i; 
            break;
        }
    if (i == FD_SETSIZE) /* Couldn't find an empty slot */
        app_error("add_client error: Too many clients");
}

/*************************************************************
 * FUNCTION NAME: check_clients                                         
 * PARAMETER: 1)p: pool which the acceptor thread will traverse and check some conditions 2): worker_index: worker thread's index                                              
 * PURPOSE: accpetor thread will check whether there are not yet distributed tasks in the pool  
 ************************************************************/
void check_clients(pool *p, int *worker_index, int num_thread) 
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
            enqueue_result = enqueue_task(*worker_index, connfd);

            if(enqueue_result == 0)
            {
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1;
            }

            *worker_index = (*worker_index + 1) % num_thread;
        }
    }
}

/*************************************************************
 * FUNCTION NAME: supernode_work                                         
 * PARAMETER: 1)args: worker thread index                                              
 * PURPOSE: worker thread does following jobs 1) receive an http request from an client 2) parse it and extract an server address and an port number 3) send the request to a server 4) receive the response from a server and send it to an server 
 
 ************************************************************/
void *supernode_work(void *args)
{
    int numbytes = 0, cnt = 0;
    int connfd = 0, px_sockfd = 0;
    int rv;
    int worker_index;
    char recv_buf[MAXBUF];
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    bool is_working = false;

    worker_index = *(int *)(args);

    /*worker thread jobs*/
    while(1)
    {
        //worker thread dequeue a task from the waiting queue
        if(!is_working)
        {
            //initialize the working thread
            memset(recv_buf, 0, sizeof(recv_buf));
            if(connfd != -1)
            {
                close(connfd);
                connfd = -1;
            }
            cnt = 0;
            numbytes = 0;

            //get the work from the waiting queue 
            connfd = dequeue_task(worker_index);
            is_working = true;
        }
        //worker thread has a task to handle
        else
        {
            //receive from the client
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
        }
    }
}
