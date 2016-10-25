#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "parse.h"

void free_http_req(HTTP_REQUEST *http_req)
{
    free(http_req->host_addr);
    free(http_req->host_port);
    free(http_req->request_msg);
}

void print_http_req(HTTP_REQUEST *http_req)
{
    printf("============================\n");
    printf("host_addr: %s\n", http_req->host_addr);
    printf("host_port: %s\n", http_req->host_port);
    printf("request_msg: %s\n", http_req->request_msg);
    printf("============================\n");
}
    
HTTP_REQUEST* parse_http_request(char *input_http_request)
{
    char *request_first_line;
    char *method;
    char *url;
    char *version;
    char *target;
    char *save_ptr[4];
    char *host_addr;
    char *host_port;
    char *request_msg;
    char *temp;
    int i;
    HTTP_REQUEST* http_req = malloc(sizeof(HTTP_REQUEST));

    request_first_line = strtok_r(input_http_request, "\r\n", &save_ptr[0]);
    method = strtok_r(request_first_line, " ", &save_ptr[1]);
    url = strtok_r(NULL, " ", &save_ptr[1]);
    version = strtok_r(NULL, " ", &save_ptr[1]);
  
    printf("method: %s url: %s version %s\n", method, url, version);

    if(strchr(url, ':') != NULL)
    {
        temp = strchr(url, ':');
        temp = temp + 1;

        host_addr = url;
        strtok_r(temp, ":", &save_ptr[2]);
        host_port = strtok_r(NULL, "/", &save_ptr[2]);
        target = strtok_r(NULL, "/", &save_ptr[2]);

        http_req->host_addr = malloc(strlen(host_addr) + sizeof(char));
        http_req->host_port = malloc(strlen(host_port) + sizeof(char));
        memset(http_req->host_addr, 0, strlen(host_addr) + sizeof(char));
        memset(http_req->host_port, 0, strlen(host_port) + sizeof(char));
        memcpy(http_req->host_addr, host_addr, strlen(host_addr));
        memcpy(http_req->host_port, host_port, (strlen(host_port)));
    }
    else
    {
        host_addr = strtok_r(url, "/", &save_ptr[3]);
        target = strtok_r(NULL, "/", &save_ptr[3]);

        http_req->host_addr = malloc(strlen(host_addr) + sizeof(char));
        http_req->host_port = malloc(sizeof(char) * 3);
        memset(http_req->host_addr, 0, strlen(host_addr) + sizeof(char));
        memset(http_req->host_port, 0, sizeof(char) * 3);
        memcpy(http_req->host_addr, host_addr, strlen(host_addr));
        memcpy(http_req->host_port, "80", sizeof(char) * 2);
    }

    http_req->request_msg = malloc(strlen(method) + strlen(target) + sizeof(char) * 3);
    memset(http_req->request_msg, 0, strlen(method) + strlen(target) + sizeof(char) * 2);
    memcpy(http_req->request_msg, method, strlen(method));
    memcpy(http_req->request_msg + strlen(method), " /", sizeof(char) * 2);
    memcpy(http_req->request_msg + strlen(method) + sizeof(char) * 2, target, strlen(target));
    return http_req;
}

int main(void)
{
    char test[100] = "GET http://www.yahoo.com:80/new.html HTTP/1.0\r\n\r\n";
    HTTP_REQUEST *http_req = parse_http_request(test);
    print_http_req(http_req);

    return 0;
}

