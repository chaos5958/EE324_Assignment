#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "parse.h"

/*************************************************************
 * FUNCTION NAME: free_http_req                                         
 * PARAMETER: 1) http_req: an HTTP_REQUEST structure to be freed 
 * PURPOSE: free inside data of an HTTP_REQUEST structure
 ************************************************************/
void free_http_req(HTTP_REQUEST *http_req)
{
    free(http_req->host_addr);
    free(http_req->host_port);
}


/*************************************************************
 * FUNCTION NAME: print_http_req                                         
 * PARAMETER: 1) http_req: an HTTP_REQUEST structure to be printed out                                              
 * PURPOSE: print inside data of an HTTP_REQUEST structure
 ************************************************************/
void print_http_req(HTTP_REQUEST *http_req)
{
    printf("============================\n");
    printf("host_addr: %s\n", http_req->host_addr);
    printf("host_port: %s\n", http_req->host_port);
    printf("============================\n");
}
   
/*************************************************************
 * FUNCTION NAME: parse_http_request                                         
 * PARAMETER: 1)input_http_req_msg: a http request message from a client 2)msg_len: length of the request message                                                                    
 * PURPOSE: parse the input http request message and load the host address and the port number from it 
 ************************************************************/
HTTP_REQUEST* parse_http_request(char *input_http_req_msg, size_t msg_len)
{
    char *host_line;
    char *save_ptr[3];
    char *host_addr;
    char *host_port;
    char *http_req_msg;
    char *http_req_msg_header;
    HTTP_REQUEST* http_req = malloc(sizeof(HTTP_REQUEST));

    http_req_msg = malloc(sizeof(char) * msg_len + sizeof(char));
    memcpy(http_req_msg, input_http_req_msg, msg_len);

    http_req_msg_header = strstr(http_req_msg, "\r\n") + 2;
    host_line = strtok_r(http_req_msg_header, "\r\n", &save_ptr[0]); 

    strtok_r(host_line, " ", &save_ptr[1]);
    host_addr = strtok_r(NULL, "\0", &save_ptr[1]);

    if(strchr(host_addr, ':') != NULL)
    {
        strtok_r(host_addr, ":", &save_ptr[2]);
        host_port = strtok_r(NULL, "\0", &save_ptr[2]);

        http_req->host_addr = malloc(strlen(host_addr) + sizeof(char));
        http_req->host_port = malloc(strlen(host_port) + sizeof(char));
        memset(http_req->host_addr, 0, strlen(host_addr) + sizeof(char));
        memset(http_req->host_port, 0, strlen(host_port) + sizeof(char));
        memcpy(http_req->host_addr, host_addr, strlen(host_addr));
        memcpy(http_req->host_port, host_port, (strlen(host_port)));
    }
    else
    {
        http_req->host_addr = malloc(strlen(host_addr) + sizeof(char));
        http_req->host_port = malloc(sizeof(char) * 3);
        memset(http_req->host_addr, 0, strlen(host_addr) + sizeof(char));
        memset(http_req->host_port, 0, sizeof(char) * 3);
        memcpy(http_req->host_addr, host_addr, strlen(host_addr));
        memcpy(http_req->host_port, "80", sizeof(char) * 2);
    }
    
    return http_req;
}

/* debugging */
//int main(void)
//{
//    char test1[100] = "GET / HTTP/1.0\r\nHost: www.w3.org\r\nOption:blabla\r\n\r\n";
//    char test2[100] = "GET /new.html HTTP/1.0\r\nHost: www.w3.org:80\r\nOption:blabla\r\n\r\n";
//    HTTP_REQUEST *http_req1 = parse_http_request(test1, strlen(test1));
//    HTTP_REQUEST *http_req2 = parse_http_request(test2, strlen(test2) + 1);
//    print_http_req(http_req1);
//    print_http_req(http_req2);
//
//    return 0;
//}

