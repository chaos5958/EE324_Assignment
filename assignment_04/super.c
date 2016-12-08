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
#define MAX_CLIENT_NUM 100

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
int enqueue_task(int, conn_info_t);
conn_info_t dequeue_task(int);
bool check_valid_client(int);
bool add_fileinfo(file_info_t *);
node_info_t *search_file(char *);

int byte_cnt = 0; /* counts total bytes received by server */
pthread_cond_t *empty_arr; /* condition variable to wake up worker threads when input comes */
pthread_mutex_t *mutex_arr; /* mutext for prevent racing condition between the acceptor thread and the worker thread (put/get into/from the queue) */
int *count_arr; /* managing how many number of tasks in the worker thread queue */
conn_info_t **accept_queue; /* worker threads's queue */
static int is_port = 0;
static int sn_neighbor_ip = 0;
static int sn_neighbor_port = 0;
static int sn_neighbor_fd = 0;
static char buf[MAXBUF];
static struct sockaddr_in sn_neighbor;
static int sn_neighbor_id = -1;
static int my_id;
static int clientid_arr[MAX_CLIENT_NUM];
static file_info_t fileinfo_arr[MAX_FILEINFO_NUM];
static int client_num = 0;
static int file_num = 0;

node_info_t* search_file(char *filename)
{
    int i;

    for (i = 0; i < file_num; i++) {
        if(strcmp(filename, fileinfo_arr[i].name) == 0)
            return &fileinfo_arr[i].node_info;
    }

    return NULL;
}

bool add_fileinfo(file_info_t *file_info)
{
    //TODO: check the validity of a file (duplicat name, wrong id, etc..)
    fileinfo_arr[file_num].node_info.ip = file_info->node_info.ip;
    fileinfo_arr[file_num].node_info.port = file_info->node_info.port;
    fileinfo_arr[file_num].id= file_info->id;
    fileinfo_arr[file_num].size = file_info->size;
    memcpy(fileinfo_arr[file_num].name, file_info->name, NAME_MAX);

    return true;
}

bool check_valid_client(int id)
{
    int i;

    if(id < 0)
        return false;

    for (i = 0; i < MAX_CLIENT_NUM; i++) {
        if(id == clientid_arr[i])
            return true;
    }

    return false;
}
/*************************************************************
 * FUNCTION NAME: enqueue_task                                         
 * PARAMETER: 1)index: worker thread's index 2)connf: accepted file descriptor to be enqueued                                              
 * PURPOSE: enqueue a task into a worker thread queue 
 ************************************************************/
int enqueue_task(int index, conn_info_t conn_info)
{
    pthread_mutex_lock(&mutex_arr[index]);

    if(count_arr[index] == ACCEPT_NUM)
    {
        printf("worker thread's queue is full\n");
        pthread_mutex_unlock(&mutex_arr[index]);
        return -1;
    }

    count_arr[index]++; 
    accept_queue[index][count_arr[index]].node_info = conn_info.node_info; 
    accept_queue[index][count_arr[index]].fd = conn_info.fd;

    pthread_cond_signal(&empty_arr[index]);
    pthread_mutex_unlock(&mutex_arr[index]);

    return 0;
}

/*************************************************************
 * FUNCTION NAME: dequeue_task                                         
 * PARAMETER: 1)index: worker thread's index                                              
 * PURPOSE: dequeue a task from a worker thread queue
 ************************************************************/
conn_info_t dequeue_task(int index)
{
    conn_info_t dequeue_conn_info;

    pthread_mutex_lock(&mutex_arr[index]);
    while (count_arr[index] == -1) {
        pthread_cond_wait(&empty_arr[index], &mutex_arr[index]); 
    }
    dequeue_conn_info = accept_queue[index][count_arr[index]];
    count_arr[index]--;
    pthread_mutex_unlock(&mutex_arr[index]);

    return dequeue_conn_info; 
}

int main(int argc, char **argv)
{
    int listenfd, connfd, port, num_thread, i, enqueue_result; 
    int *ids;
    int worker_index = 0;
    int count = 0;
    int param_opt, option_index = 0;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
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
    //TODO: test
    if(sn_neighbor_ip && sn_neighbor_port)
    {
        kaza_hdr_t *temp_hdr = (kaza_hdr_t *)malloc(sizeof(kaza_hdr_t));
        sn_neighbor_fd = Open_clientfd(inet_ntoa(sn_neighbor.sin_addr), sn_neighbor.sin_port);

        temp_hdr->id = my_id;
        temp_hdr->msg_type = HELLO_FROM_SUP_TO_SUP;
        temp_hdr->total_len = sizeof(kaza_hdr_t);

        send(sn_neighbor_fd, temp_hdr, sizeof(kaza_hdr_t), 0);

        if(recv(sn_neighbor_fd, temp_hdr, sizeof(kaza_hdr_t), 0) == sizeof(kaza_hdr_t))
        {
            if(temp_hdr->msg_type = HELLO_FROM_SUP_TO_SUP)
            {
                sn_neighbor_id = temp_hdr->id; 
                printf("supernode: start connection with another supernode success\n");
            }
            else
                fprintf(stderr, "supernode: unvalid msg from another supernode\n"); 
        }
        else
            fprintf(stderr, "supernode: unvalid msg from another supernode\n");

        Close(sn_neighbor_fd);
    }

    //initialize a working thread number;
    num_thread = WORKER_THREAD_NUM;

    //id allocation 
    srand(time(NULL));
    my_id = rand(); 

    //client info initialization
    for (i = 0; i < MAX_CLIENT_NUM; i++) 
        clientid_arr[i] = -1;

    //initializing data used in the acceptor thread
    empty_arr = (pthread_cond_t *)malloc(num_thread * sizeof(pthread_cond_t));
    mutex_arr = (pthread_mutex_t *)malloc(num_thread * sizeof(pthread_mutex_t));
    count_arr = (int *)malloc(num_thread * sizeof(int));
    pthread_arr = (pthread_t *)malloc(num_thread * sizeof(pthread_t));
    ids = (int *)malloc(num_thread * sizeof(int));
    accept_queue = (conn_info_t **)malloc(num_thread * sizeof(conn_info_t *));

    for (i = 0; i < num_thread; i++) {
        ids[i] = i;
        pthread_cond_init(&empty_arr[i], NULL);
        pthread_mutex_init(&mutex_arr[i], NULL);
        accept_queue[i] = (conn_info_t *)malloc(ACCEPT_NUM * sizeof(conn_info_t));
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
    listenfd = Open_listenfd(port);
    init_pool(listenfd, &pool); 

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    /* the accpetor thread job */
    while (1) {
        /* Wait for listening/connected descriptor(s) to become ready - timeout 5 seconds to check_clients frequently*/
        conn_info_t enqueue_info;
        pool.ready_set = pool.read_set;
        pool.nready = select(pool.maxfd+1, &pool.ready_set, NULL, NULL, &timeout);

        /* If listening descriptor ready, add new client to pool */
        if (FD_ISSET(listenfd, &pool.ready_set)) { 
            connfd = accept(listenfd, (SA *)&clientaddr, &clientlen); 

            printf("supernode: listenfd is active\n");

            if(connfd < 0)
            {
                perror("main thread: accept failed\n");
                continue;
            }
            enqueue_info.fd = connfd;
            enqueue_info.node_info.ip = clientaddr.sin_addr.s_addr;
            enqueue_info.node_info.port = clientaddr.sin_port;

            /*distribute accepted file descriptors to the worker threads*/
            enqueue_result = enqueue_task(worker_index, enqueue_info); 

            if(enqueue_result == -1)
            {
                fprintf(stderr, "supernode id %d: queue is full\n", my_id);
                continue;
                /* TODO: implement load handling when client request are too many */
                /* conceptually 1) create new thread or 2) send error message to user(try again) */
            }

            /* increase worker thread index to balance load */
            worker_index  = (worker_index + 1) % num_thread;
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
 * FUNCTION NAME: supernode_work                                         
 * PARAMETER: 1)args: worker thread index                                              
 * PURPOSE: worker thread does following jobs 1) receive an http request from an client 2) parse it and extract an server address and an port number 3) send the request to a server 4) receive the response from a server and send it to an server 

 ************************************************************/
void *supernode_work(void *args)
{
    int numbytes = 0, cnt = 0;
    int  msg_size = 0, msg_id = 0, msg_type;
    int connfd = 0, px_sockfd = 0;
    int rv;
    int worker_index;
    char recv_buf[MAXBUF];
    char send_buf[MAXBUF];
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    bool is_working = false, is_first = true, has_data = false;
    kaza_hdr_t *recv_hdr;
    kaza_hdr_t *send_hdr;
    file_info_t *recv_fileinfo;
    conn_info_t recv_conninfo;

    worker_index = *(int *)(args);

    /*worker thread jobs*/
    while(1)
    {
        //worker thread dequeue a task from the waiting queue
        if(!is_working)
        {
            //supernode received data and handle it
            if(has_data)
            {
                switch(msg_type)
                {
                    case HELLO_FROM_CHD_TO_SUP:
                        printf("supernode id %d: HELLO_FROM_CHD_TO_SUP\n", my_id);
                        clientid_arr[client_num] = msg_id;
                        client_num++;

                        send_hdr = (kaza_hdr_t *)send_buf;
                        send_hdr->id = my_id; 
                        send_hdr->total_len = sizeof(kaza_hdr_t);
                        send_hdr->msg_type = HELLO_FROM_SUP_TO_CHD;

                        send(connfd, send_buf, send_hdr->total_len, 0);
                        break;

                    case HELLO_FROM_SUP_TO_SUP:
                        printf("supernode id %d: HELLO_FROM_SUP_TO_SUP\n", my_id);
                        if(sn_neighbor_id != -1)
                            fprintf(stderr, "supernode: overwrite neighbor supernode\n");

                        sn_neighbor_id = msg_id;
                        sn_neighbor.sin_addr.s_addr = recv_conninfo.node_info.ip;
                        sn_neighbor.sin_port = recv_conninfo.node_info.port;
                        send_hdr = (kaza_hdr_t *)send_buf;
                        send_hdr->id = my_id; 
                        send_hdr->total_len = sizeof(kaza_hdr_t);
                        send_hdr->msg_type = HELLO_FROM_SUP_TO_SUP;

                        send(connfd, send_buf, send_hdr->total_len, 0);
                        break;

                    case FILEINFO_FROM_CHD_TO_SUP:
                        printf("supernode id %d: FILEINFO_FROM_CHD_TO_SUP\n", my_id);
                        //valid client 
                        if(check_valid_client(msg_id))
                        {
                            recv_fileinfo = (file_info_t *)(buf + sizeof(kaza_hdr_t));
                            //file info add success - 1) save file info 2) propagate to another supernode 
                            if(add_fileinfo(recv_fileinfo))
                            {
                                send_hdr = (kaza_hdr_t *)send_buf;
                                send_hdr->id = my_id; 
                                send_hdr->total_len = sizeof(kaza_hdr_t);
                                send_hdr->msg_type = FILEINFO_OKAY_FROM_SUP_TO_CHD;

                                send(connfd, send_buf, send_hdr->total_len, 0);

                                //another supernode is connected
                                if(sn_neighbor_id != -1)
                                {
                                    int temp_fd = Open_clientfd(inet_ntoa(sn_neighbor.sin_addr), sn_neighbor.sin_port);

                                    send_hdr = (kaza_hdr_t *)send_buf;
                                    send_hdr->id = sn_neighbor_id; 
                                    send_hdr->total_len = sizeof(kaza_hdr_t);
                                    send_hdr->msg_type = FILEINFOSHR_FROM_SUP_TO_SUP;
                                    memcpy(send_buf + sizeof(kaza_hdr_t), recv_fileinfo, sizeof(file_info_t));
                                    send(temp_fd, send_buf, send_hdr->total_len, 0);

                                    //TODO: recv 0x51 or 0x52  
                                }
                            }
                            //file info add fail
                            else
                            {
                                send_hdr = (kaza_hdr_t *)send_buf;
                                send_hdr->id = my_id; 
                                send_hdr->total_len = sizeof(kaza_hdr_t);
                                send_hdr->msg_type = FILEINFO_FAIL_FROM_SUP_TO_CHD;

                                send(connfd, send_buf, send_hdr->total_len, 0);
                            }
                        }
                        //unvalid client
                        else
                            fprintf(stderr, "supernode: unvalid client file info request \n");
                        break;

                    case SEARCHQRY_FROM_CHD_TO_SUP:
                        printf("supernode id %d: SEARCHQRY_FROM_CHD_TO_SUP\n", my_id);
                        char temp_filename[NAME_MAX];
                        node_info_t *temp_nodeinfo;

                        if(check_valid_client(msg_id))
                        {
                            memcpy(temp_filename, recv_buf + sizeof(kaza_hdr_t), msg_size - sizeof(kaza_hdr_t));

                            //file exists
                            if((temp_nodeinfo = search_file(temp_filename)) != NULL)
                            {
                                send_hdr = (kaza_hdr_t *)send_buf;
                                send_hdr->id = my_id; 
                                send_hdr->total_len = sizeof(kaza_hdr_t);
                                send_hdr->msg_type = SEARCHQRY_OKAY_FROM_SUP_TO_CHD;
                                memcpy(send_buf  + sizeof(kaza_hdr_t), temp_nodeinfo, sizeof(node_info_t)); 

                                send(connfd, send_buf, send_hdr->total_len, 0);

                            }
                            //no file
                            else
                            {
                                send_hdr = (kaza_hdr_t *)send_buf;
                                send_hdr->id = my_id; 
                                send_hdr->total_len = sizeof(kaza_hdr_t);
                                send_hdr->msg_type = SEARCHQRY_FAIL_FORM_SUP_TO_CHD;

                                send(connfd, send_buf, send_hdr->total_len, 0);
                            }
                        }
                        break;

                    case FILEINFOSHR_FROM_SUP_TO_SUP:
                        printf("supernode id %d: FILEINFOSHR_FROM_SUP_TO_SUP\n", my_id);
                        recv_fileinfo = (file_info_t *)(buf + sizeof(kaza_hdr_t));
                        //file info add success - 1) save file info 2) propagate to another supernode 
                        if(add_fileinfo(recv_fileinfo))
                        {
                            send_hdr = (kaza_hdr_t *)send_buf;
                            send_hdr->id = my_id; 
                            send_hdr->total_len = sizeof(kaza_hdr_t);
                            send_hdr->msg_type = FILEINFOSHR_OKAY_FROM_SUP_TO_SUP;

                            send(connfd, send_buf, send_hdr->total_len, 0);

                        }
                        //file info add fail
                        else
                        {
                            send_hdr = (kaza_hdr_t *)send_buf;
                            send_hdr->id = my_id; 
                            send_hdr->total_len = sizeof(kaza_hdr_t);
                            send_hdr->msg_type = FILEINFOSHR_FAIL_FROM_SUP_TO_SUP;

                            send(connfd, send_buf, send_hdr->total_len, 0);
                        }
                        break;

                    default:
                        /* wrong msg_type */
                        fprintf(stderr, "supernode: recv wrong msg_type\n");
                }
            }

            //initialize the working thread
            memset(recv_buf, 0, sizeof(recv_buf));
            if(connfd != -1)
            {
                close(connfd);
                connfd = -1;
            }
            cnt = 0;
            numbytes = 0;
            is_first = true;
            has_data = false;

            //get the work from the waiting queue 
            recv_conninfo = dequeue_task(worker_index);
            connfd = recv_conninfo.fd;
            is_working = true;
        }
        //worker thread has a task to handle
        else
        {
            //receive from the client
            cnt = recv(connfd, recv_buf + numbytes, MAX_BUF_SIZE, 0);
            if(cnt > 0)
            {
                if(is_first)
                {
                    recv_hdr = (kaza_hdr_t *)recv_buf;
                    msg_id = recv_hdr->id;
                    msg_size = recv_hdr->total_len;
                    msg_type = recv_hdr->msg_type;
                    is_first = false;
                    printf("cnt: %d, msg_size: %d\n", cnt, msg_size);

                    msg_size -= cnt;
                    numbytes += cnt;
                    printf("cnt: %d, msg_size: %d\n", cnt, msg_size);

                    has_data = true; 
                }
                /* mostly, used for data send and recv */
                else
                {
                    msg_size -= cnt;
                    numbytes += cnt;
                }

                if(msg_size <= 0)
                {
                    fprintf(stdout, "supernode: recv msg_dize <= 0\n");
                    is_working = false;
                }
            }
            else if(cnt == -1)
            {
                perror("worker thread recv -1 from the client");
                is_working = false;
            }
            else if(cnt == 0)
                is_working = false;
        }
    }
}
