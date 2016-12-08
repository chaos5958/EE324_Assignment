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
#include <dirent.h>
#include <sys/stat.h>

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
void* child_backgroud_work(void *);
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
static int sn_connect_ip = 0;
static int sn_connect_port = 0;
static int sn_connect_fd = 0;
static char buf[MAXBUF];
static struct sockaddr_in sn_connect;
static int sn_connect_id = -1;
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
        {"s_ip", required_argument, &sn_connect_ip, 1},
        {"s_port", required_argument, &sn_connect_port, 1},
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
                    if(inet_aton(optarg, &sn_connect.sin_addr) == 0)
                    {
                        fprintf(stderr, "Unvalid s_ip address\n");
                        return 0;
                    }
                }

                if(strcmp("s_port", long_options[option_index].name) == 0)
                {
                    sn_connect.sin_port = atoi(optarg);
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

    if(is_port == 0 || (sn_connect_ip == 0 ^ sn_connect_port == 0))
    {
        fprintf(stderr, "usage: %s -p [port] --s_ip [ip] --s_port [port]\n", argv[0]);
        return 0;
    }

    //DEBUG
    printf("mine: port=%d, sn_connect: ip=%s, port=%u\n", port, inet_ntoa(sn_connect.sin_addr), (unsigned)sn_connect.sin_port); 

    //id allocation 
    srand(time(NULL));
    my_id = rand(); 

    //do connection setup with another super-node
    //TODO: test
    if(sn_connect_ip && sn_connect_port)
    {
        char *temp_buf = malloc(sizeof(kaza_hdr_t) + sizeof(file_info_t));
        kaza_hdr_t *temp_hdr = (kaza_hdr_t *)temp_buf;
        DIR *dir; 
        struct dirent *ent;
        file_info_t *temp_fileinfo;
        struct stat st;

        sn_connect_fd = Open_clientfd(inet_ntoa(sn_connect.sin_addr), sn_connect.sin_port);

        temp_hdr->id = my_id;
        temp_hdr->msg_type = HELLO_FROM_SUP_TO_SUP;
        temp_hdr->total_len = sizeof(kaza_hdr_t);

        send(sn_connect_fd, temp_buf, sizeof(kaza_hdr_t), 0);

        recv(sn_connect_fd, temp_hdr, sizeof(kaza_hdr_t), 0); 
        if(temp_hdr->msg_type = HELLO_FROM_SUP_TO_SUP)
        {
            sn_connect_id = temp_hdr->id; 
            printf("child: start connection with a supernode success\n");
        }
        else
        {
            fprintf(stderr, "child: unvalid msg from a supernode\n"); 
            free(temp_buf);
            return -1;
        }

       
        //TODO: test (only skeleton done)
        //'/data' directory search and send file info
        if((dir = opendir("./data")) != NULL)
        {
            temp_fileinfo = (file_info_t *)(temp_buf + sizeof(kaza_hdr_t));

            while((ent = readdir(dir)) != NULL)
            {
                temp_hdr->msg_type = FILEINFO_FROM_CHD_TO_SUP;
                temp_hdr->total_len = sizeof(kaza_hdr_t) + sizeof(file_info_t);
                memcpy(temp_fileinfo->name, ent->d_name, sizeof(ent->d_name));
                stat(ent->d_name, &st);
                temp_fileinfo->size = st.st_size; 
                temp_fileinfo->id = my_id;
                temp_fileinfo->node_info.ip = /*TODO*/;
                temp_fileinfo->node_info.port = port;

                send(sn_connect_fd, temp_buf, sizeof(kaza_hdr_t) + sizeof(file_info_t), 0);

                printf("temp_hfileinfo->name: %s\n", temp_fileinfo->name);

                recv(sn_connect_fd, temp_buf, sizeof(kaza_hdr_t), 0);
                //file info request success
                if(temp_hdr->msg_type = FILEINFO_OKAY_FROM_SUP_TO_CHD)
                {
                }
                //file info request fail
                else
                {
                }
            }
        }
        close(sn_connect_fd);
        free(temp_buf);
    }


    //initialize a working thread number;
    num_thread = WORKER_THREAD_NUM;
    
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
        int thread_id = pthread_create(&pthread_arr[i], NULL, &child_backgroud_work, (void *)&ids[i]);
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

//TODO: add these threads in main function 
//handle file request from other childs
void *child_backgroud_work(void *args)
{
    //TODO
}

//handle user command like "get 12.txt asdf.txt 
void *child_io_work(void *args)
{
   //TODO 
}

