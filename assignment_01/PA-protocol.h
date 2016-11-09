/* Protocol Specification */
#include <stdint.h>

#define PA_VERSION 0X04
#define INITIAL_SEQ_NUM_MAX 10000

typedef struct PA_header {
    uint8_t ver;
    uint8_t user_id;
    uint16_t seq_num;
    uint16_t len;
    int16_t command;
} PA_HEADER;


    
