/* Reference * 1. http://csapp.cs.cmu.edu/2e/ics2/code/conc/echoservers.c * (code related to use select function) */

#include "csapp.h"
#include "protocol.h" 
#include <semaphore.h>
#include <stdbool.h>
#include <sys/time.h>
#include <getopt.h>
#include <arpa/inet.h>

#define LISTEN_QUEUE_NUM 100
#define MAX_BUF_SIZE 10000 
#define MAX_PORT_LEN 6
#define LOG_MSG_LEN 100
#define WORKER_THREAD_NUM 1 
#define ACCEPT_NUM 10000 
#define MAX_CHILD_NUM 100

typedef struct { /* represents a pool of connected descriptors */ //line:conc:echoservers:beginpool
    int maxfd;        /* largest descriptor in read_set */   
    fd_set read_set;  /* set of all active descriptors */
    fd_set ready_set; /* subset of descriptors ready for reading  */
    int nready;       /* number of ready descriptors from select */   
    int maxi;         /* highwater index into child array */
    int clientfd[FD_SETSIZE];    /* set of active descriptors */
    rio_t clientrio[FD_SETSIZE]; /* set of active read buffers */
} pool; 

void init_pool(int listenfd, pool *p);
void* supernode_work(void *);
int enqueue_task(int, conn_info_t);
conn_info_t dequeue_task(int);
bool is_valid_chd(int);
bool add_fileinfo(file_info_t *);
node_info_t *search_file(char *);

int byte_cnt = 0; /* counts total bytes received by server */
pthread_cond_t *empty_arr; /* condition variable to wake up worker threads when input comes */
pthread_mutex_t *mutex_arr; /* mutext for prevent racing condition between the acceptor thread and the worker thread (put/get into/from the queue) */
int *count_arr; /* managing how many number of tasks in the worker thread queue */
conn_info_t **accept_queue; /* worker threads's queue */
static int is_port = 0;
static char buf[MAXBUF];
static int my_id; //my id number
static node_info_t childinfo_arr[MAX_CHILD_NUM]; //array consisted of child information
static node_info_t sn_neighbor_info; //neighborhood supernode's infomation
static file_info_t fileinfo_arr[MAX_FILEINFO_NUM]; //array consisted of file information
static int child_num = 0; //holding child number
static int file_num = 0; //holding file number
static int port; //listen port

/*************************************************************
 * FUNCTION NAME: search_file                                         
 * PARAMETER: 1)filename: name of a file which will be searched                                              
 * PURPOSE: search a file with a name 'filename' in 'fileinfo_arr' and return it(or NULL)  
 ************************************************************/
node_info_t* search_file(char *filename)
{
    int i, child_id = -1;

    for (i = 0; i < file_num; i++) {

        write_log("filename=%s fileinfo_arr=%s\n", filename, fileinfo_arr[i].name);

        if(strcmp(filename, fileinfo_arr[i].name) == 0)
        {
            return &fileinfo_arr[i].node_info;
        }
    }

    fprintf(stderr, "supernode: search_file no matching child info to given id\n");

    return NULL;
}

/*************************************************************
 * FUNCTION NAME: add_fileinfo                                         
 * PARAMETER: 1)fileinfo: information of a file to be inserted to 'fileinfo_arr'                                              
 * PURPOSE: insert file_info_t' structure into 'fileinfo_arr" 
 ************************************************************/
bool add_fileinfo(file_info_t *file_info)
{
    //TODO: check the validity of a file (duplicat name, wrong id, etc..)
    fileinfo_arr[file_num].id= file_info->id;
    fileinfo_arr[file_num].size = file_info->size;
    fileinfo_arr[file_num].node_info.ip = file_info->node_info.ip;
    fileinfo_arr[file_num].node_info.port = file_info->node_info.port;
    memcpy(fileinfo_arr[file_num].name, file_info->name, NAME_MAX);

    file_num++;

    return true;
}

/*************************************************************
 * FUNCTION NAME: is_valid_chd                                         
 * PARAMETER: 1)id: id to be tested whether it is valid or not                                              
 * PURPOSE: check whether 'id' is one of child id of a supernode
 ************************************************************/
bool is_valid_chd(int id)
{
    int i;

    if(id < 0)
        return false;

    for (i = 0; i < child_num; i++) {
        write_log("IS_VALID_CHD: id=%d childinfo_arr[i].id=%d\n", id, childinfo_arr[i].id);
        if(id == childinfo_arr[i].id)
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
        write_log("worker thread's queue is full\n");
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
    int listenfd, connfd, num_thread, i, enqueue_result; 
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
    static int sn_neighbor_ip; 
    static int sn_neighbor_port;
    
    sn_neighbor_info.id = -1;

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
                write_log("super| %s: ", long_options[option_index].name);
                if(optarg)
                    write_log("%s", optarg);
                write_log("\n");

                if(strcmp("s_ip", long_options[option_index].name) == 0)
                {
                    if(inet_aton(optarg, &sn_neighbor_info.ip) == 0)
                    {
                        fprintf(stderr, "Unvalid s_ip address\n");
                        return 0;
                    }
                }

                if(strcmp("s_port", long_options[option_index].name) == 0)
                {
                    sn_neighbor_info.port = atoi(optarg);
                }

                break;

            case 'p':
                is_port = 1;
                port = atoi(optarg);
                write_log("super| my_port: %s\n", optarg);
                break;

            case '?':
                fprintf(stderr, "usage: %s -p [port] --s_ip [ip] --s_port [port]\n", argv[0]);
                return 0;

            default:
                write_log("param_opt: %d\n", param_opt);
                fprintf(stderr, "error while reading user input arguments\n");
                return 0;
        }
    }

    if(is_port == 0 || (sn_neighbor_ip == 0 ^ sn_neighbor_port == 0))
    {
        fprintf(stderr, "usage: %s -p [port] --s_ip [ip] --s_port [port]\n", argv[0]);
        return 0;
    }

    //do connection setup with another super-node
    if(sn_neighbor_ip && sn_neighbor_port)
    {
        char *temp_send_buf = (char *)malloc(sizeof(kaza_hdr_t) + sizeof(node_info_t));
        kaza_hdr_t *temp_hdr = (kaza_hdr_t *)temp_send_buf; 
        uint16_t *temp_port = (uint16_t *)(temp_send_buf + sizeof(kaza_hdr_t));
        int sn_neighbor_fd;

        write_log("mine: port=%d, sn_neighbor: ip=%s, port=%u\n", port, inet_ntoa(sn_neighbor_info.ip), (unsigned)sn_neighbor_info.port); 

        temp_hdr->id = my_id;
        temp_hdr->msg_type = HELLO_FROM_SUP_TO_SUP;
        temp_hdr->total_len = sizeof(kaza_hdr_t) + sizeof(uint16_t);
        *temp_port = port;

        sn_neighbor_fd = Open_clientfd(inet_ntoa(sn_neighbor_info.ip), sn_neighbor_info.port);
        send(sn_neighbor_fd, temp_send_buf, temp_hdr->total_len, 0);

        if(recv(sn_neighbor_fd, temp_hdr, sizeof(kaza_hdr_t), 0) == sizeof(kaza_hdr_t))
        {
            if(temp_hdr->msg_type = HELLO_FROM_SUP_TO_SUP)
            {
                sn_neighbor_info.id = temp_hdr->id; 
                write_log("supernode: start connection with another supernode success\n");
            }
            else
                fprintf(stderr, "supernode: unvalid msg from another supernode\n"); 
        }
        else
            fprintf(stderr, "supernode: unvalid msg from another supernode\n");

        Close(sn_neighbor_fd);
        free(temp_send_buf);
    }

    //initialize a working thread number;
    num_thread = WORKER_THREAD_NUM;

    //id allocation 
    srand(time(NULL));
    my_id = rand(); 

    //child info initialization
    for (i = 0; i < MAX_CHILD_NUM; i++) 
        childinfo_arr[i].id = -1;

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

    //creating worker threads
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

        /* If listening descriptor ready, add new child to pool */
        if (FD_ISSET(listenfd, &pool.ready_set)) { 
            connfd = accept(listenfd, (SA *)&clientaddr, &clientlen); 

            if(connfd < 0)
            {
                perror("main thread: accept failed\n");
                continue;
            }

            /*1)enqueue chlid's connection information 
              2)distribute it to worker threads*/
            enqueue_info.fd = connfd;
            enqueue_info.node_info.ip = clientaddr.sin_addr;
            enqueue_info.node_info.port = clientaddr.sin_port;

            enqueue_result = enqueue_task(worker_index, enqueue_info); 

            if(enqueue_result == -1)
            {
                fprintf(stderr, "supernode id %d: queue is full\n", my_id);
                continue;
                /* TODO: implement load handling when child request are too many */
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
 * PARAMETER: 1)args: worker thread index(not used in this project because I used only one worker thread)                                              
 * PURPOSE: worker thread does following jobs 1) receive a message from a supernode or a child 2) do appropriate job based on information decribed in the message

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
    uint16_t *recv_port = (uint16_t *)(recv_buf + sizeof(kaza_hdr_t));

    worker_index = *(int *)(args);

    /*worker thread jobs*/
    while(1)
    {
        //worker thread dequeues a task from the waiting queue
        if(!is_working)
        {
            //supernode do some jobs  when it received all data
            if(has_data)
            {
                switch(msg_type)
                {
                    //hello from a child
                    case HELLO_FROM_CHD_TO_SUP:
                        write_log("supernode id %d: msg id %d: HELLO_FROM_CHD_TO_SUP\n", my_id, msg_id);
                        childinfo_arr[child_num].id = msg_id;
                        childinfo_arr[child_num].ip = recv_conninfo.node_info.ip; 
                        childinfo_arr[child_num].port = *recv_port; 
                        write_log("supernode id %d: ip=%s, port=%d\n", my_id, inet_ntoa(recv_conninfo.node_info.ip), recv_conninfo.node_info.port); 
                        child_num++;

                        send_hdr = (kaza_hdr_t *)send_buf;
                        send_hdr->id = my_id; 
                        send_hdr->total_len = sizeof(kaza_hdr_t);
                        send_hdr->msg_type = HELLO_FROM_SUP_TO_CHD;

                        send(connfd, send_buf, send_hdr->total_len, 0);
                        break;

                    //hello from a supernode
                    case HELLO_FROM_SUP_TO_SUP:
                        write_log("supernode id %d: HELLO_FROM_SUP_TO_SUP\n", my_id);
                        
                        if(sn_neighbor_info.id != -1)//already has a neighbor super node
                            break;

                        sn_neighbor_info.id = msg_id;
                        sn_neighbor_info.ip = recv_conninfo.node_info.ip;
                        sn_neighbor_info.port = *recv_port; 

                        send_hdr = (kaza_hdr_t *)send_buf;
                        send_hdr->id = my_id; 
                        send_hdr->total_len = sizeof(kaza_hdr_t);
                        send_hdr->msg_type = HELLO_FROM_SUP_TO_SUP;

                        send(connfd, send_buf, send_hdr->total_len, 0);

                        break;

                    //file information request from a child
                    case FILEINFO_FROM_CHD_TO_SUP:
                        write_log("supernode id %d msg_id %d: FILEINFO_FROM_CHD_TO_SUP\n",my_id, msg_id);
                        //valid child 
                        if(is_valid_chd(msg_id))
                        {
                            recv_fileinfo = (file_info_t *)(recv_buf + sizeof(kaza_hdr_t));
                            
                            write_log("supernode id %d msg_id %d: recv_file name=%s recv_file size=%zu\n", my_id, msg_id, recv_fileinfo->name, recv_fileinfo->size);
                            recv_fileinfo->node_info.ip = recv_conninfo.node_info.ip;
                            //file info add success - 1) save file info 2) propagate to another supernode 
                            if(add_fileinfo(recv_fileinfo)) // 1)
                            {
                                send_hdr = (kaza_hdr_t *)send_buf;
                                send_hdr->id = my_id; 
                                send_hdr->total_len = sizeof(kaza_hdr_t);
                                send_hdr->msg_type = FILEINFO_OKAY_FROM_SUP_TO_CHD;

                                send(connfd, send_buf, send_hdr->total_len, 0);

                                if(sn_neighbor_info.id != -1) // 2)
                                {

                                    write_log("mine: port=%d, sn_neighbor: ip=%s, port=%u\n", port, inet_ntoa(sn_neighbor_info.ip), (unsigned)sn_neighbor_info.port); 

                                    int temp_fd = Open_clientfd(inet_ntoa(sn_neighbor_info.ip), sn_neighbor_info.port);
                                    send_hdr = (kaza_hdr_t *)send_buf;
                                    send_hdr->id = sn_neighbor_info.id; 
                                    send_hdr->total_len = sizeof(kaza_hdr_t) + sizeof(file_info_t);
                                    send_hdr->msg_type = FILEINFOSHR_FROM_SUP_TO_SUP;
                                    memcpy(send_buf + sizeof(kaza_hdr_t), recv_fileinfo, sizeof(file_info_t));
                                    write_log("filename=%s\n", recv_fileinfo->name);
                                    send(temp_fd, send_buf, send_hdr->total_len, 0);

                                    //TODO: recv 0x51 or 0x52  
                                    Close(temp_fd);
                                }
                            }
                            //file information add fail
                            else //not used
                            {
                                send_hdr = (kaza_hdr_t *)send_buf;
                                send_hdr->id = my_id; 
                                send_hdr->total_len = sizeof(kaza_hdr_t);
                                send_hdr->msg_type = FILEINFO_FAIL_FROM_SUP_TO_CHD;

                                send(connfd, send_buf, send_hdr->total_len, 0);
                            }
                        }
                        //unvalid child (unused now - add_fileinfo return true)
                        else
                            fprintf(stderr, "supernode: unvalid child file info request \n");
                        break;

                    //search query from a child
                    case SEARCHQRY_FROM_CHD_TO_SUP:
                        write_log("supernode id %d: SEARCHQRY_FROM_CHD_TO_SUP\n", my_id);
                        char temp_filename[NAME_MAX];
                        node_info_t *temp_nodeinfo;

                        //valid child
                        if(is_valid_chd(msg_id))
                        {
                            memset(temp_filename, 0, sizeof(temp_filename));
                            memcpy(temp_filename, recv_buf + sizeof(kaza_hdr_t), msg_size - sizeof(kaza_hdr_t));

                            //file exists
                            if((temp_nodeinfo = search_file(temp_filename)) != NULL)
                            {

                                write_log("supernode id %d: ip=%s, port=%d\n",my_id, inet_ntoa(temp_nodeinfo->ip), temp_nodeinfo->port); 

                                send_hdr = (kaza_hdr_t *)send_buf;
                                send_hdr->id = my_id; 
                                send_hdr->total_len = sizeof(kaza_hdr_t) + sizeof(node_info_t);
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

                    //file information share message from a supernode
                    case FILEINFOSHR_FROM_SUP_TO_SUP:
                        write_log("supernode id %d: FILEINFOSHR_FROM_SUP_TO_SUP\n", my_id);
                        recv_fileinfo = (file_info_t *)(recv_buf + sizeof(kaza_hdr_t));
                        write_log("supernode id %d: filename=%s\n", my_id, recv_fileinfo->name);
                        //file info add success 
                        if(add_fileinfo(recv_fileinfo)) 
                        {
                            send_hdr = (kaza_hdr_t *)send_buf;
                            send_hdr->id = my_id; 
                            send_hdr->total_len = sizeof(kaza_hdr_t);
                            send_hdr->msg_type = FILEINFOSHR_OKAY_FROM_SUP_TO_SUP;

                            send(connfd, send_buf, send_hdr->total_len, 0);

                        }
                        //file info add fail (not used)
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

            //get a task from the waiting queue 
            recv_conninfo = dequeue_task(worker_index);
            connfd = recv_conninfo.fd;
            is_working = true;
        }
        //worker thread has a task to handle
        else
        {
            //receive from the child
            cnt = recv(connfd, recv_buf + numbytes, MAX_BUF_SIZE, 0);
            if(cnt > 0)
            {
                int temp_msg_size;
                if(is_first)
                {
                    recv_hdr = (kaza_hdr_t *)recv_buf;
                    msg_id = recv_hdr->id;
                    msg_size = recv_hdr->total_len;
                    temp_msg_size = msg_size;
                    msg_type = recv_hdr->msg_type;
                    is_first = false;

                    temp_msg_size -= cnt;
                    numbytes += cnt;

                    has_data = true; 
                }
                /* mostly, used for data send and recv */
                else
                {
                    temp_msg_size -= cnt;
                    numbytes += cnt;
                }

                if(temp_msg_size <= 0)
                {
                    //write_log("supernode id %d: recv temp_msg_size=%d\n", my_id, temp_msg_size);
                    is_working = false;
                }
            }
            else if(cnt == -1)
            {
                perror("worker thread recv -1 from the child");
                is_working = false;
            }
            else if(cnt == 0)
                is_working = false;
        }
    }
}
