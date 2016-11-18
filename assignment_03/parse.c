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
    free(http_req);
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
 * PARAMETER: 1)input_http_req_msg: a http request message from a client 
 * PURPOSE: parse the input http request message and load the host address and the port number from it 
 ************************************************************/
HTTP_REQUEST* parse_http_request(char *input_http_req_msg)
{
    char *host_line, *url, *port, *end;
    char *save_ptr[3];
    char *host_addr;
    char *host_port;
    char *http_req_msg_header;
    HTTP_REQUEST* http_req = malloc(sizeof(HTTP_REQUEST));

    host_line = strstr(input_http_req_msg, "Host:");
    url = strstr(host_line, "Host:");
    url = url + 6;
    end = strchr(url, '\r');
    *end = '\0';
    
    if((port = strchr(url, ':')) != NULL)
    {
        *port = '\0';
        http_req->host_addr = calloc(strlen(url), sizeof(char)); 
        http_req->host_port = calloc(strlen(port + 1), sizeof(char)); 
        memcpy(http_req->host_addr, url, strlen(url));
        memcpy(http_req->host_port, port + 1, strlen(port + 1));
        *port = ':';
    }
    else
    {
        http_req->host_addr = calloc(strlen(url), sizeof(char));
        http_req->host_port = calloc(3, sizeof(char));
        memcpy(http_req->host_addr, url, strlen(url));
        memcpy(http_req->host_port, "80", sizeof(char) * 2);
    }
    *end = '\r'; 
    return http_req;
}

/* debugging */
//int main(void)
//{
//    char test1[100] = "GET / HTTP/1.0\r\nHost: www.w3.org\r\nOption:blabla\r\n\r\n";
//    char test2[100] = "GET /new.html HTTP/1.0\r\nHost: www.w3.org:60\r\nOption:blabla\r\n\r\n";
//    HTTP_REQUEST *http_req1 = parse_http_request(test1);
//    HTTP_REQUEST *http_req2 = parse_http_request(test2);
//    print_http_req(http_req1);
//    print_http_req(http_req2);
//
//    return 0;
//}
//
