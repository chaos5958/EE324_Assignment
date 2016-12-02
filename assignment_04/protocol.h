#include <linux/limits.h>

#define HELLO_FROM_CHD_TO_SUP 0x10
#define HELLO_FROM_SUP_TO_CHD 0x11
#define HELLO_FROM_SUP_TO_SUP 0x12
#define FILEINFO_FROM_CHD_TO_SUP 0x20
#define FILEINFO_OKAY_FROM_SUP_TO_CHD 0x21
#define FILEINDO_FAIL_FROM_SUP_TO_CHD 0x22
#define SEARCHQRY_FROM_CHD_TO_SUP 0x30
#define SEARCHQRY_OKAY_FROM_SUP_TO_CHD 0x31
#define SEARCHQRY_FAIL_FORM_SUP_TO_CHD 0x32
#define FILEREQ_FROM_FROM_CHD_TO_CHD 0x40
#define FILEREQ_OKAY_FROM_CHD_TO_CHD 0x41
#define FILEREQ_FAIL_FROM_CHD_TO_CHD 0x42
#define FILEINFOSHR_FROM_SUP_TO_SUP 0x50
#define FILEINFOSHR_OKAY_FROM_SUP_TO_SUP 0x51
#define FILEINFOSHR_FAIL_FROM_SUP_TO_SUP 0x52

typedef struct _kaza_hdr_t {
    int total_len;
    int id;
    int msg_type;
} kaza_hdr_t;

typedef struct _file_info_t {
    char name[NAME_MAX];
    size_t size;
} file_info_t;

typedef struct _loc_info_t {
    uint32_t ip;
    uint16_t port;
} location_info_t;
