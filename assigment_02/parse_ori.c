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
    
HTTP_REQUEST* parse_http_request(char *input_http_request, size_t request_len)
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
    char *header_body;
    int i;
    size_t header_body_len, start;
    HTTP_REQUEST* http_req = malloc(sizeof(HTTP_REQUEST));

    request_first_line = strtok_r(input_http_request, "\r\n", &save_ptr[0]);
    header_body_len = request_len - strlen(request_first_line) - 2;
    header_body = strtok_r(NULL, "\0", &save_ptr[0]) + 1;
    
    method = strtok_r(request_first_line, " ", &save_ptr[1]);
    url = strtok_r(NULL, " ", &save_ptr[1]);
    version = strtok_r(NULL, " ", &save_ptr[1]);
  
    //hhyeo printf debugging
    printf("method: %s url: %s version: %s header_body: %s", method, url, version, header_body);

    if((url = strstr(url, "http://")) != NULL)
    {
        url  = url + strlen("http://");
        if(strchr(url, ':') != NULL)
        {
            host_addr = strtok_r(url, ":", &save_ptr[2]);
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
    }

    if(target != NULL)
    {
        http_req->request_msg_len = strlen(method) + strlen(target) + strlen(version) + header_body_len + sizeof(char) * 6; 
        http_req->request_msg = malloc(http_req->request_msg_len);
        memset(http_req->request_msg, 0, http_req->request_msg_len);

        start = 0;
        memcpy(http_req->request_msg + start, method, strlen(method));
        start += strlen(method);
        memcpy(http_req->request_msg + start, " /", sizeof(char) * 2);
        start += sizeof(char) * 2;
        memcpy(http_req->request_msg + start, target, strlen(target));
        start += strlen(target);
        memcpy(http_req->request_msg + start, " ", sizeof(char));
        start += sizeof(char);
        memcpy(http_req->request_msg + start, version, strlen(version));
        start += strlen(version);
        memcpy(http_req->request_msg + start, "\r\n", sizeof(char) * 2);
        start += sizeof(char) * 2;
        memcpy(http_req->request_msg + start, header_body, header_body_len);
    }
    else
    {
        http_req->request_msg_len = strlen(method) + strlen(version) + header_body_len + sizeof(char) * 6; 
        http_req->request_msg = malloc(http_req->request_msg_len);
        memset(http_req->request_msg, 0, http_req->request_msg_len);
        start = 0;
        memcpy(http_req->request_msg + start, method, strlen(method));
        start += strlen(method);
        memcpy(http_req->request_msg + start, " /", sizeof(char) * 2);
        start += sizeof(char) * 2;
        memcpy(http_req->request_msg + start, " ", sizeof(char));
        start += sizeof(char);
        memcpy(http_req->request_msg + start, version, strlen(version));
        start += strlen(version);
        memcpy(http_req->request_msg + start, "\r\n", sizeof(char) * 2);
        start += sizeof(char) * 2;
        memcpy(http_req->request_msg + start, header_body, header_body_len);
    }
    return http_req;
}

//int main(void)
//{
//    char test1[100] = "GET http://www.yahoo.com/ HTTP/1.0\r\nHost:www.w3.org\r\n\r\n";
//    char test2[100] = "GET http://www.yahoo.com:80/new.html HTTP/1.0\r\nHost:www.w3.org\r\n\r\n";
//    HTTP_REQUEST *http_req1 = parse_http_request(test1, strlen(test1) + 1);
//    HTTP_REQUEST *http_req2 = parse_http_request(test2, strlen(test2) + 1);
//    print_http_req(http_req1);
//    print_http_req(http_req2);
//
//    return 0;
//}

