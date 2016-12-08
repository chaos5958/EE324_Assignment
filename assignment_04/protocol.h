#include <linux/limits.h>

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

//client configuration
#define MAX_CLIENTFILE_NUM 100 
#define MAX_FILEINFO_NUM MAX_CLIENTFILE_NUM*100//static configuration of maximum file numbers (allows around 100 per a client)

typedef struct _kaza_hdr_t {
    int total_len;
    int id;
    int msg_type;
} kaza_hdr_t;

typedef struct _node_info_t {
    uint32_t ip;
    uint16_t port;
} node_info_t;

typedef struct _file_info_t {
    char name[NAME_MAX];
    size_t size;
    int id;
    node_info_t node_info;
} file_info_t;

typedef struct _conn_info_t {
    int fd;
    node_info_t node_info;
} conn_info_t;

