#include <stdio.h>

typedef struct http_req{
    char *host_addr;
    char *host_port;
    char *request_msg;
    size_t request_msg_len;
} HTTP_REQUEST;

void free_http_req(HTTP_REQUEST *http_req);
void print_http_req(HTTP_REQUEST *http_req);
HTTP_REQUEST* parse_http_request(char *input_http_request, size_t request_len);



