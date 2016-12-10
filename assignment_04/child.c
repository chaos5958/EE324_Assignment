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
//bool check_valid_client(int);
//bool add_fileinfo(file_info_t *);
node_info_t *search_file(char *);

int byte_cnt = 0; /* counts total bytes received by server */
pthread_cond_t *empty_arr; /* condition variable to wake up worker threads when input comes */
pthread_mutex_t *mutex_arr; /* mutext for prevent racing condition between the acceptor thread and the worker thread (put/get into/from the queue) */
int *count_arr; /* managing how many number of tasks in the worker thread queue */
conn_info_t **accept_queue; /* worker threads's queue */
static int is_port = 0;
static char buf[MAXBUF];
static node_info_t sn_connect_info;
static int my_id;

/*
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
*/

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
    static int sn_connect_ip = 0;
    static int sn_connect_port = 0;
    
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
                write_log("child| %s: ", long_options[option_index].name);
                if(optarg)
                    write_log("%s", optarg);
                write_log("\n");

                if(strcmp("s_ip", long_options[option_index].name) == 0)
                {
                    if(inet_aton(optarg, &sn_connect_info.ip) == 0)
                    {
                        fprintf(stderr, "Unvalid s_ip address\n");
                        return 0;
                    }
                }

                if(strcmp("s_port", long_options[option_index].name) == 0)
                {
                    sn_connect_info.port = atoi(optarg);
                }
                break;

            case 'p':
                is_port = 1;
                port = atoi(optarg);
                write_log("child| my_port: %s\n", optarg);
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

    if(is_port == 0 || sn_connect_ip == 0 || sn_connect_port == 0)
    {
        fprintf(stderr, "usage: %s -p [port] --s_ip [ip] --s_port [port]\n", argv[0]);
        return 0;
    }

    //id allocation 
    srand(time(NULL));
    my_id = rand(); 

    //do connection setup with another super-node
    //TODO: test
    if(sn_connect_ip && sn_connect_port)
    {
        write_log("mine: port=%d, sn_connect: ip=%s, port=%u\n", port, inet_ntoa(sn_connect_info.ip), (unsigned)sn_connect_info.port); 

        char *temp_buf = malloc(sizeof(kaza_hdr_t) + sizeof(file_info_t));
        kaza_hdr_t *temp_hdr = (kaza_hdr_t *)temp_buf;
        uint16_t *temp_port = (uint16_t *)(temp_buf + sizeof(kaza_hdr_t));
        DIR *dir; 
        struct dirent *ent;
        file_info_t *temp_fileinfo;
        struct stat st;
        int sn_connect_fd; 
        
        write_log("mine: port=%d, sn_connect: ip=%s, port=%u\n", port, inet_ntoa(sn_connect_info.ip), (unsigned)sn_connect_info.port); 

        temp_hdr->id = my_id;
        temp_hdr->msg_type = HELLO_FROM_CHD_TO_SUP;
        temp_hdr->total_len = sizeof(kaza_hdr_t) + sizeof(uint16_t);
        *temp_port = port; 

        sn_connect_fd = Open_clientfd(inet_ntoa(sn_connect_info.ip), sn_connect_info.port);
        if(sn_connect_ip < 0)
        {
            fprintf(stderr, "child id %d: cannot connect to the supernode\n", my_id);
            return -1;
        }
        send(sn_connect_fd, temp_buf, temp_hdr->total_len, 0);
        recv(sn_connect_fd, temp_hdr, sizeof(kaza_hdr_t), 0); 
        Close(sn_connect_fd);

        if(temp_hdr->msg_type = HELLO_FROM_SUP_TO_CHD)
        {
            sn_connect_info.id = temp_hdr->id; 
            write_log("child id %d: start connection with a supernode %d success\n", my_id, sn_connect_info.id);
        }
        else
        {
            fprintf(stderr, "child id %d: unvalid msg from a supernode\n", my_id); 
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
                write_log("child id %d: file name=%s\n", my_id, ent->d_name);
                if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                    continue;

                temp_hdr->id = my_id;
                temp_hdr->msg_type = FILEINFO_FROM_CHD_TO_SUP;
                temp_hdr->total_len = sizeof(kaza_hdr_t) + sizeof(file_info_t);
                memcpy(temp_fileinfo->name, ent->d_name, sizeof(ent->d_name));
                stat(ent->d_name, &st);
                temp_fileinfo->size = st.st_size; 
                temp_fileinfo->id = my_id;


                sn_connect_fd = Open_clientfd(inet_ntoa(sn_connect_info.ip), sn_connect_info.port);
                if(sn_connect_ip < 0)
                {
                    fprintf(stderr, "child id %d: cannot connect to the supernode\n", my_id);
                    return -1;
                }
                write_log("1\n");
                send(sn_connect_fd, temp_buf, sizeof(kaza_hdr_t) + sizeof(file_info_t), 0);
                write_log("1_2\n");
                recv(sn_connect_fd, temp_buf, sizeof(kaza_hdr_t), 0);
                Close(sn_connect_fd);

                write_log("2\n");
                //file info request success
                if(temp_hdr->msg_type = FILEINFO_OKAY_FROM_SUP_TO_CHD)
                {
                    write_log("child id %d: file %s info transfer success\n", my_id, temp_fileinfo->name); 
                }
                //file info request fail
                else
                {
                    //TODO: resend? 
                    fprintf(stderr, "child id %d: FILEINFO_FAIL_FROM_SUP_TO_CHD\n", my_id);
                }
                write_log("3\n");
            }
        }
        write_log("4\n");
        free(temp_buf);
    }
    
    write_log("child id %d: file transfer finish\n", my_id);

    //initialize a working thread number;
    num_thread = WORKER_THREAD_NUM;

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

            write_log("supernode: listenfd is active\n");

            if(connfd < 0)
            {
                perror("main thread: accept failed\n");
                continue;
            }
            enqueue_info.fd = connfd;
            enqueue_info.node_info.ip = clientaddr.sin_addr;
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
    kaza_hdr_t *recv_hdr = (kaza_hdr_t *)recv_buf;
    kaza_hdr_t *send_hdr = (kaza_hdr_t *)send_buf;
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
                    char file_relative_path[PATH_MAX]; 
                    char file_name[NAME_MAX];
                    FILE *fp;
                    int fd;
                    struct stat stat_buf;

                    case FILEREQ_FROM_FROM_CHD_TO_CHD:
                    
                        memset(file_name, 0, sizeof(file_name));
                        memset(file_relative_path, 0, sizeof(file_relative_path));
                        memcpy(file_name, recv_buf + sizeof(kaza_hdr_t), msg_size - sizeof(kaza_hdr_t));
                        sprintf(file_relative_path, "./data/%s", file_name); 

                        fp = fopen(file_relative_path, "r");
                        if(fp == NULL)
                        {
                            fprintf(stderr, "client id %d: dosen't have file %s\n", my_id, file_relative_path);
                            break;
                        }
                        fd = fileno(fp);
                        fstat(fd, &stat_buf); 
                        
                        send_hdr = (kaza_hdr_t *)send_buf;
                        send_hdr->id = my_id;
                        send_hdr->total_len = sizeof(kaza_hdr_t) + stat_buf.st_size;
                        
                        send(connfd, send_buf, sizeof(kaza_hdr_t), 0);

                        int numbytes;
                        while(1)
                        {
                            numbytes = fread(send_buf, sizeof(char), sizeof(send_buf), fp);

                            if(numbytes > 0)
                                send(connfd, send_buf, numbytes, 0);
                            else if(numbytes == 0)
                                break;
                            else
                            {
                                fprintf(stderr, "child id %d: read returns <0\n", my_id);
                                break;
                            }
                        }

                        fclose(fp);
                        break;

                    default:
                        fprintf(stderr, "child id %d: unvalid msg_type\n", my_id);
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
                    write_log("cnt: %d, msg_size: %d\n", cnt, msg_size);

                    msg_size -= cnt;
                    numbytes += cnt;
                    write_log("cnt: %d, msg_size: %d\n", cnt, msg_size);

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
                    write_log("supernode: recv msg_dize <= 0\n");
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

//handle user command like "get 12.txt asdf.txt 
void *child_io_work(void *args)
{
    char io_buf[CHILD_IO_MAX_LEN];
    char *ori_filename;
    char *dest_filename;
    bool io_work = true; 
    char *token;
    int token_num = 0;

    while(io_work)
    {
        memset(io_buf, 0, sizeof(io_buf));
        memset(ori_filename, 0, sizeof(ori_filename));
        memset(dest_filename, 0, sizeof(dest_filename));
        scanf("%s", io_buf); 

        token = strtok(io_buf, " ");
        token_num++;

        if(token == NULL || strcmp(token, "get") == 0)
            goto io_error;

        while(token != NULL)
        {
            if(token_num == 1)
                ori_filename = strtok(NULL, " ");
            else if(token_num == 2)
                dest_filename = strtok(NULL, " ");
            else 
                goto io_error;

            token_num++;
        }

        if(token_num != 3)
            goto io_error;
        
        write_log("client id %d: get %s %s\n", my_id, ori_filename, dest_filename);

        int sn_connect_fd = Open_clientfd(inet_ntoa(sn_connect_info.ip), sn_connect_info.port);
        if(sn_connect_fd < 0)
        {
            fprintf(stderr, "child id %d: fail to connect supernode %d\n", my_id, sn_connect_info.id);
            continue;
        }

        kaza_hdr_t *kaza_hdr = (kaza_hdr_t *)io_buf;
        kaza_hdr->id = my_id;
        kaza_hdr->total_len = sizeof(kaza_hdr_t) + strlen(ori_filename) + 1;
        kaza_hdr->msg_type = SEARCHQRY_FROM_CHD_TO_SUP;
        memcpy(io_buf + sizeof(kaza_hdr_t), ori_filename, strlen(ori_filename) + 1);

        send(sn_connect_fd, io_buf, kaza_hdr->total_len, 0);
        recv(sn_connect_fd, io_buf, sizeof(kaza_hdr), 0); 

        Close(sn_connect_fd);         

        if(kaza_hdr->msg_type == SEARCHQRY_OKAY_FROM_SUP_TO_CHD)
        {
            node_info_t *chd_conn_info= (node_info_t*)(io_buf + sizeof(kaza_hdr_t));
            int chd_conn_fd = Open_clientfd(inet_ntoa(chd_conn_info->ip), chd_conn_info->port); 
            char file_relative_path[PATH_MAX]; 

            if(chd_conn_info < 0)
            {
                fprintf(stderr, "child id %d: fail to connect to a childnode %d\n", my_id, chd_conn_info->id);
                continue;
            }

            kaza_hdr->id = my_id;
            kaza_hdr->total_len = sizeof(kaza_hdr_t) + strlen(ori_filename) + 1;
            kaza_hdr->msg_type = FILEINFO_FROM_CHD_TO_SUP; 
            memcpy(io_buf + sizeof(kaza_hdr_t), ori_filename, strlen(ori_filename) + 1);

            send(chd_conn_fd, io_buf, kaza_hdr->total_len, 0);
            recv(chd_conn_fd, io_buf, sizeof(kaza_hdr_t), 0); 
                        
            if(kaza_hdr->msg_type == FILEINFO_OKAY_FROM_SUP_TO_CHD)
            {
                int numbytes, file_size = 0;
                file_size = kaza_hdr->total_len - sizeof(kaza_hdr_t);
                memset(file_relative_path, 0, sizeof(file_relative_path));
                sprintf(file_relative_path, "./download/%s", dest_filename);

                FILE *fp = fopen(file_relative_path, "w");
                if(fp == NULL)
                {
                    fprintf(stderr, "child id %d: fail to open  %s\n", my_id, file_relative_path);
                    Close(chd_conn_fd);
                    continue;
                }

                while(1)
                {
                    numbytes = recv(chd_conn_fd, io_buf, sizeof(io_buf), 0);

                    if(numbytes > 0)
                        fwrite(io_buf, sizeof(char), numbytes, fp);
                    else if(numbytes == 0)
                        break;
                    else
                    {
                        fprintf(stderr, "child id %d: recv returns < 0\n", my_id);
                        break;
                    }

                    file_size -= numbytes;
                }

                Close(chd_conn_fd);
                fclose(fp);
                if(file_size != 0)
                    fprintf(stderr, "child id %d: recv file size wrong %d bytes\n", my_id, file_size);

            }
            else
            {
                fprintf(stderr, "child id %d: file %s doesn't exists (supernode wrong)\n", my_id, ori_filename);
                Close(chd_conn_fd);
                continue;
            }

        }
        else
            write_log("child id %d: no matching file %s\n", my_id, ori_filename); 

        continue;

io_error:
        fprintf(stderr, "child id %d: wrong I/O input ex) get [dest-file name] [ori-file name]\n", my_id);
        continue;
    }
}

