#include <linux/limits.h>
#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>

#define HELLO_FROM_CHD_TO_SUP 0x10
#define HELLO_FROM_SUP_TO_CHD 0x11
#define HELLO_FROM_SUP_TO_SUP 0x12
#define FILEINFO_FROM_CHD_TO_SUP 0x20
#define FILEINFO_OKAY_FROM_SUP_TO_CHD 0x21
#define FILEINFO_FAIL_FROM_SUP_TO_CHD 0x22
#define SEARCHQRY_FROM_CHD_TO_SUP 0x30
#define SEARCHQRY_OKAY_FROM_SUP_TO_CHD 0x31
#define SEARCHQRY_FAIL_FORM_SUP_TO_CHD 0x32
#define FILEREQ_FROM_FROM_CHD_TO_CHD 0x40
#define FILEREQ_OKAY_FROM_CHD_TO_CHD 0x41
#define FILEREQ_FAIL_FROM_CHD_TO_CHD 0x42
#define FILEINFOSHR_FROM_SUP_TO_SUP 0x50
#define FILEINFOSHR_OKAY_FROM_SUP_TO_SUP 0x51
#define FILEINFOSHR_FAIL_FROM_SUP_TO_SUP 0x52
#define CHILD_IO_MAX_LEN (NAME_MAX*2 + 5)

#define IS_DEBUGMODE 1

//child configuration
#define MAX_CHILD_NUM 100 
#define MAX_FILEINFO_NUM MAX_CHILD_NUM*100//static configuration of maximum file numbers (allows around 100 per a client)
#define IO_QUEUE_SIZE 10 
#define write_log(format, args...)  \
    if(IS_DEBUGMODE) {              \
        printf(format, ## args);    \
    }                                    

typedef struct _kaza_hdr_t {
    int total_len;
    int id;
    int msg_type;
} kaza_hdr_t;

typedef struct _node_info_t {
    int id;
    struct in_addr ip;
    uint16_t port;
} node_info_t;

typedef struct _node_addr_t {
    struct in_addr ip;
    uint16_t port;
} node_addr_t;

typedef struct _file_info_t {
    char name[NAME_MAX];
    size_t size;
    int id;
    node_info_t node_info;
} file_info_t;

typedef struct _conn_info_t {
    int fd;
    node_addr_t node_info;
} conn_info_t;

typedef struct _io_info_t {
    char ori_name[NAME_MAX];
    char dest_name[NAME_MAX];
} io_info_t;













